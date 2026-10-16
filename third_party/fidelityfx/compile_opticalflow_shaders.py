#!/usr/bin/env python3
"""Build the official AMD Optical Flow GLSL passes for the FP32 Vulkan path."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
from compile_fsr3_shaders import reflect

PASSES=['prepare_luma_pass','compute_luminance_pyramid_pass',
        'generate_scd_histogram_pass','compute_scd_divergence_pass',
        'compute_optical_flow_advanced_pass_v5','filter_optical_flow_pass_v5',
        'scale_optical_flow_advanced_pass_v5']


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--compiler',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--validator')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    source=['// Generated from AMD FidelityFX SDK 1.1.4 GLSL. Do not edit.',
            '#include <FidelityFX/host/ffx_opticalflow.h>','#include <ffx_shader_blobs.h>',
            '#include <cstring>','namespace {']
    for pass_id,pass_name in enumerate(PASSES):
        name='of_%d'%pass_id
        path=args.output/(name+'.spv')
        command=[args.compiler,'-V','--target-env','vulkan1.2','-S','comp','-Os',
                 '-I'+str(args.sdk/'include/FidelityFX/gpu'),
                 '-DFFX_GLSL=1','-DFFX_GPU=1','-DFFX_HALF=0',
                 '-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT=0',
                 str(args.sdk/'src/backends/vk/shaders/opticalflow'/('ffx_opticalflow_'+pass_name+'.glsl')),
                 '-o',str(path)]
        result=subprocess.run(command,text=True,capture_output=True)
        if result.returncode:raise RuntimeError(name+'\n'+result.stdout+result.stderr)
        if args.validator:subprocess.run([args.validator,'--target-env','vulkan1.2',str(path)],check=True)
        data=path.read_bytes()
        groups=reflect(data)
        code=struct.unpack('<%dI'%(len(data)//4),data)
        source.append('alignas(4) static const uint32_t %s_data[]={\n%s\n};'%
            (name,',\n'.join(','.join('0x%08xu'%w for w in code[i:i+12]) for i in range(0,len(code),12))))
        fields=['reinterpret_cast<const uint8_t*>(%s_data)'%name,str(len(data))]
        fields += [str(len(group))for group in groups]
        for group_id,group in enumerate(groups):
            if not group:fields+=['nullptr']*4;continue
            prefix='%s_g%d'%(name,group_id)
            source.append('static const char* %s_names[]={%s};'%
                (prefix,','.join(json.dumps(item[0])for item in group)))
            fields.append(prefix+'_names')
            for column,suffix in ((1,'bindings'),(2,'counts'),(3,'spaces')):
                source.append('static const uint32_t %s_%s[]={%s};'%
                    (prefix,suffix,','.join(str(item[column])for item in group)))
                fields.append(prefix+'_'+suffix)
        source.append('static const FfxShaderBlob %s_blob={%s};'%(name,','.join(fields)))
    source += ['}',
        'FfxErrorCode NightcapOpticalflowBlob(FfxPass pass, FfxBindStage stage, uint32_t options, FfxShaderBlob* output) {',
        ' if(!output) return FFX_ERROR_INVALID_POINTER;',
        ' if(stage!=FFX_BIND_COMPUTE_SHADER_STAGE || (options&6u)) return FFX_ERROR_INVALID_ARGUMENT;',
        ' const FfxShaderBlob* blob=nullptr; switch(pass){']
    for pass_id in range(len(PASSES)):
        source+=[' case %du: blob=&of_%d_blob; break;'%(pass_id,pass_id)]
    source+=[' } if(!blob)return FFX_ERROR_INVALID_ARGUMENT;',
             ' std::memcpy(output,blob,sizeof(*output));return FFX_OK;','}','']
    (args.output/'opticalflow_shader_blobs.cpp').write_text('\n'.join(source))
    conversion=args.output/'opticalflow_to_motion.spv'
    subprocess.run([args.compiler,'-V','--target-env','vulkan1.2','-S','comp','-Os',
                    str(Path(__file__).with_name('opticalflow_to_motion.comp')),
                    '-o',str(conversion)],check=True,capture_output=True)
    if args.validator:
        subprocess.run([args.validator,'--target-env','vulkan1.2',str(conversion)],check=True)
    data=conversion.read_bytes()
    code=struct.unpack('<%dI'%(len(data)//4),data)
    (args.output/'opticalflow_motion_spv.h').write_text(
        '#pragma once\n#include <cstdint>\ninline constexpr std::uint32_t kOpticalflowToMotion[]={\n'+
        ',\n'.join(','.join('0x%08xu'%w for w in code[i:i+12])for i in range(0,len(code),12))+'\n};\n')
    print('Compiled 7 AMD Optical Flow FP32 shaders')

if __name__=='__main__':main()
