"""Regenerate the FSR 1 FP32 EASU/RCAS shader header with glslangValidator."""
from pathlib import Path
import argparse,struct,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('root',type=Path);args=p.parse_args()
folder=args.root/'src/vr'
out=['// Generated from fsr.frag; AMD reference code is preserved in fsr/.',
     '#pragma once','#include <cstdint>','namespace mocktail::vr {']
with tempfile.TemporaryDirectory(prefix='nightcap-fsr-') as temporary:
    for index,name in enumerate(['Easu','Rcas']):
        binary=Path(temporary)/(name+'.spv')
        subprocess.run(['glslangValidator','-V',f'-DNIGHTCAP_FSR_PASS={index}',str(folder/'fsr.frag'),'-o',str(binary)],check=True)
        data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
        out.append(f'inline constexpr std::uint32_t kFsr{name}[]={{')
        out += [','.join(f'0x{x:08x}u' for x in words[i:i+8])+',' for i in range(0,len(words),8)]
        out.append('};')
out.append('}')
(folder/'fsr_spv.h').write_text('\n'.join(out)+'\n')

with tempfile.TemporaryDirectory(prefix='nightcap-temporal-') as temporary:
    binary=Path(temporary)/'temporal.spv'
    subprocess.run(['glslangValidator','-V',str(folder/'temporal.frag'),'-o',str(binary)],check=True)
    data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    out=['// Generated from temporal.frag.','#pragma once','#include <cstdint>',
         'namespace mocktail::vr {','inline constexpr std::uint32_t kTemporal[]={']
    out += [','.join(f'0x{x:08x}u' for x in words[i:i+8])+',' for i in range(0,len(words),8)]
    out += ['};','}']
    (folder/'temporal_spv.h').write_text('\n'.join(out)+'\n')
