#!/usr/bin/env python3
"""Compile AMD's GLSL upscaler passes and reflect the SPIR-V descriptor ABI.

The runtime uses render-resolution, unjittered motion vectors and FP32 maths.
LDR/HDR, normal/inverted depth, Lanczos and sharpen permutations are explicit.
No generated binaries need to be checked into the source tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

PASSES = [
    'prepare_inputs', 'luma_pyramid', 'shading_change_pyramid',
    'shading_change', 'prepare_reactivity', 'luma_instability',
    'accumulate', 'accumulate', 'rcas', 'debug_view', 'autogen_reactive',
]


def string(words):
    return struct.pack('<%dI' % len(words), *words).split(b'\0', 1)[0].decode()


def reflect(data):
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError('Invalid SPIR-V magic')
    names, decorations, types, constants, variables = {}, {}, {}, {}, []
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        if not count or at + count > len(words):
            raise ValueError('Malformed SPIR-V instruction')
        args = words[at + 1:at + count]
        if opcode == 5:  # OpName
            names[args[0]] = string(args[1:])
        elif opcode == 71:  # OpDecorate
            decorations.setdefault(args[0], {})[args[1]] = args[2:]
        elif opcode in (25, 26, 27, 28, 29, 30, 32):
            types[args[0]] = (opcode, args[1:])
        elif opcode == 43:  # OpConstant
            constants[args[1]] = args[2]
        elif opcode == 59:  # OpVariable
            variables.append(args)
        at += count
    groups = [[] for _ in range(7)]
    for pointer, identifier, storage, *_ in variables:
        decoration = decorations.get(identifier, {})
        if 33 not in decoration:  # no descriptor binding
            continue
        binding = decoration[33][0]
        space = decoration.get(34, (0,))[0]
        if space != 0:
            raise ValueError('Only descriptor set 0 is expected')
        opcode, args = types[pointer]
        if opcode != 32:
            raise ValueError('Descriptor is not a pointer')
        target = args[1]
        count = 1
        if types[target][0] == 28:
            target, length = types[target][1]
            count = constants[length]
        opcode, args = types[target]
        if storage == 2:  # Uniform / block constant buffer
            group = 0
        elif storage == 12:  # StorageBuffer
            group = 3 if 24 in decoration else 4
        elif storage == 0 and opcode == 25:  # UniformConstant / image
            group = 1 if args[5] == 1 else 2
        elif storage == 0 and opcode == 26:
            group = 5
        else:
            raise ValueError('Unsupported descriptor type %r' % ((storage, opcode),))
        name = names.get(identifier, '')
        if not name:
            raise ValueError('Missing descriptor name')
        groups[group].append((name, binding, count, space))
    for group in groups:
        group.sort(key=lambda item: item[1])
    return groups


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--validator')
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    gpu = args.sdk / 'include/FidelityFX/gpu'
    shaders = args.sdk / 'src/backends/vk/shaders/fsr3upscaler'
    declarations, blobs, mappings, hashes = [], {}, [], {}
    for pass_id, pass_name in enumerate(PASSES):
        for inverted in (0, 1):
            for hdr, jittered, half in ((h, j, p) for h in (0, 1) for j in (0, 1)
                                       for p in ((0,) if pass_id==8 else (0, 1))):
                for lanczos in ((0, 1) if pass_id in (6, 7) else (0,)):
                    sharp = int(pass_id == 7)
                    key = 4 | inverted * 16 | hdr * 2 | jittered * 8 | lanczos | sharp * 32 | half * 128
                    name = 'fsr3_%d_%d' % (pass_id, key)
                    path = out / (name + '.spv')
                    defines = {
                        'FFX_GLSL': 1, 'FFX_GPU': 1, 'FFX_HALF': half,
                        'FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF': 0,
                        'FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF': 0,
                        'FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF': half,
                        'FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF': 0,
                        'FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE': 2,
                        'FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE': lanczos,
                        'FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS': 1,
                        'FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT': hdr,
                        'FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS': jittered,
                        'FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH': inverted,
                        'FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING': sharp,
                    }
                    command = [args.compiler, '-V', '--target-env', 'vulkan1.2',
                               '-S', 'comp', '-Os', '-I' + str(gpu)]
                    command += ['-D%s=%s' % item for item in defines.items()]
                    command += [str(shaders / ('ffx_fsr3upscaler_' + pass_name + '_pass.glsl')),
                                '-o', str(path)]
                    compiled = subprocess.run(command, text=True, capture_output=True)
                    if compiled.returncode:
                        raise RuntimeError(name + '\n' + compiled.stdout + compiled.stderr)
                    if args.validator:
                        subprocess.run([args.validator, '--target-env', 'vulkan1.2', str(path)], check=True)
                    data = path.read_bytes()
                    digest = hashlib.sha256(data).hexdigest()
                    groups = reflect(data)
                    if digest not in hashes:
                        hashes[digest] = name
                        code = struct.unpack('<%dI' % (len(data) // 4), data)
                        declarations.append('alignas(4) static const uint32_t %s_data[] = {\n%s\n};' %
                            (name, ',\n'.join(','.join('0x%08xu' % w for w in code[i:i+12])
                                             for i in range(0, len(code), 12))))
                        fields = ['reinterpret_cast<const uint8_t*>(%s_data)' % name, str(len(data))]
                        fields += [str(len(group)) for group in groups]
                        for group_id, group in enumerate(groups):
                            if not group:
                                fields += ['nullptr'] * 4
                                continue
                            prefix = '%s_g%d' % (name, group_id)
                            declarations.append('static const char* %s_names[] = {%s};' %
                                (prefix, ','.join(json.dumps(item[0]) for item in group)))
                            fields.append(prefix + '_names')
                            for column, suffix in ((1, 'bindings'), (2, 'counts'), (3, 'spaces')):
                                declarations.append('static const uint32_t %s_%s[] = {%s};' %
                                    (prefix, suffix, ','.join(str(item[column]) for item in group)))
                                fields.append(prefix + '_' + suffix)
                        declarations.append('static const FfxShaderBlob %s_blob = {%s};' % (name, ','.join(fields)))
                    mappings.append((pass_id, key, hashes[digest]))
    source = ['// Generated from AMD FidelityFX SDK 1.1.4 GLSL. Do not edit.',
              '#include <FidelityFX/host/ffx_fsr3upscaler.h>',
              '#include <ffx_shader_blobs.h>', '#include <cstring>',
              'FfxErrorCode NightcapOpticalflowBlob(FfxPass,FfxBindStage,uint32_t,FfxShaderBlob*);',
              'namespace {'] + declarations + ['}',
              'extern "C" FfxErrorCode ffxGetPermutationBlobByIndex(FfxEffect effect, FfxPass pass, FfxBindStage stage, uint32_t options, FfxShaderBlob* output) {',
              ' if (!output) return FFX_ERROR_INVALID_POINTER;',
              ' if (effect == FFX_EFFECT_OPTICALFLOW) return NightcapOpticalflowBlob(pass,stage,options,output);',
              ' if (effect != FFX_EFFECT_FSR3UPSCALER || stage != FFX_BIND_COMPUTE_SHADER_STAGE || !(options&4u)) return FFX_ERROR_INVALID_ARGUMENT;',
              ' const uint32_t key = (options & (2u|4u|8u|16u|32u|128u)) | ((pass==6u || pass==7u) ? (options&1u) : 0u);',
              ' const FfxShaderBlob* blob = nullptr;', ' switch (pass) {']
    for pass_id in range(len(PASSES)):
        source += [' case %du: switch(key) {' % pass_id]
        for _, key, name in (entry for entry in mappings if entry[0] == pass_id):
            source += [' case %du: blob=&%s_blob; break;' % (key, name)]
        source += [' } break;']
    source += [' }', ' if (!blob) return FFX_ERROR_INVALID_ARGUMENT;',
               ' std::memcpy(output,blob,sizeof(*output)); return FFX_OK;', '}',
               'extern "C" FfxErrorCode ffxIsWave64(FfxEffect effect, uint32_t options, bool& wave64) {',
               ' wave64 = (options & (effect==FFX_EFFECT_OPTICALFLOW?1u:64u))!=0;',
               ' return (effect==FFX_EFFECT_FSR3UPSCALER || effect==FFX_EFFECT_OPTICALFLOW) ? FFX_OK : FFX_ERROR_INVALID_ARGUMENT;', '}', '']
    (out / 'fsr3_shader_blobs.cpp').write_text('\n'.join(source))
    print('Compiled %d FSR 3.1.4 permutations (%d unique shaders)' % (len(mappings), len(hashes)))


if __name__ == '__main__':
    main()
