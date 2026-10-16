#!/usr/bin/env python3
import argparse
from pathlib import Path
import struct
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--compiler',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--validator')
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
declarations=['#pragma once','#include <cstdint>']
for name,file,stage in [('kFsr3GazeCrop','fsr3_gaze_crop.comp','comp'),
                       ('kFsr3GazeVertex','fsr3_gaze_composite.vert','vert'),
                       ('kFsr3GazeComposite','fsr3_gaze_composite.frag','frag')]:
    target=args.output/(file+'.spv')
    subprocess.run([args.compiler,'-V','--target-env','vulkan1.2','-S',stage,'-Os',
                    str(Path(__file__).with_name(file)),'-o',str(target)],check=True,capture_output=True)
    if args.validator:subprocess.run([args.validator,'--target-env','vulkan1.2',str(target)],check=True)
    data=target.read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
    declarations += ['inline constexpr std::uint32_t %s[]={\n%s\n};'%
        (name,',\n'.join(','.join('0x%08xu'%word for word in words[i:i+12])for i in range(0,len(words),12)))]
(args.output/'fsr3_gaze_spv.h').write_text('\n'.join(declarations)+'\n')
