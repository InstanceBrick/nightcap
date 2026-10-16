"""Regenerate the checked-in native gaze AA SPIR-V header (glslang required)."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path,help='Nightcap source checkout')
args=parser.parse_args()
folder=args.root/'src/vr'
out=['// Generated from gaze_aa.vert and gaze_aa.frag using glslangValidator 16.2.0.',
     '#pragma once','#include <cstdint>','namespace mocktail::vr {']
with tempfile.TemporaryDirectory(prefix='nightcap-aa-') as temporary:
    variants=[('gaze_aa.vert','Vertex',[]),('gaze_aa.frag','Fragment',[])]
    for preset in ['Low','High']:
        for part,number in [('Edge',0),('Weights',1)]:
            defines=[f'-DNIGHTCAP_SMAA_PASS={number}']
            if preset=='High':defines+=['-DNIGHTCAP_SMAA_HIGH=1']
            variants.append(('gaze_smaa.frag','Smaa'+part+preset,defines))
    variants.append(('gaze_smaa.frag','SmaaNeighborhood',['-DNIGHTCAP_SMAA_PASS=2']))
    for source,label,defines in variants:
        binary=Path(temporary)/(label+'.spv')
        subprocess.run(['glslangValidator','-V',*defines,str(folder/source),'-o',str(binary)],check=True)
        data=binary.read_bytes()
        words=struct.unpack('<'+'I'*(len(data)//4),data)
        out.append(f'inline constexpr std::uint32_t kGazeAa{label}[]={{')
        out += [','.join(f'0x{x:08x}u' for x in words[i:i+8])+',' for i in range(0,len(words),8)]
        out.append('};')
out.append('}')
(folder/'gaze_aa_spv.h').write_text('\n'.join(out)+'\n')
