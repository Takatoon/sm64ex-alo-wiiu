"""Validate five Swag NPCs using real native tables and the geo interpreter.

Usage: python3 tools/tests/check_dynos_actor_animations.py /path/to/swag.zip
No pack assets are copied into the source tree. Requires gcc/g++.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
from check_dynos_npcs import native_signature

ROOT=Path(__file__).resolve().parents[2]
flags=['-I.','-Iinclude','-Isrc','-DAVOID_UB=1','-DVERSION_US',
       '-DUSE_SYSTEM_MALLOC=1','-DEXTERNAL_DATA=1','-DF3DEX_GBI_2',
       '-DF3DEX_GBI_2E','-DHIGH_FPS_PC','-ffunction-sections','-fdata-sections']
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary);objects=[]
    for source in ['geo_layout','graph_node','graph_node_manager','math_util']:
        obj=directory/(source+'.o');objects.append(str(obj))
        subprocess.run(['gcc',*flags,'-D_LANGUAGE_C','-c','src/engine/'+source+'.c','-o',str(obj)],cwd=ROOT,check=True)
    exe=directory/'probe'
    # Native C tables contain hexadecimal s16 values; retain their C conversion.
    subprocess.run(['g++',*flags,'-D_LANGUAGE_C_PLUS_PLUS','-std=c++17','-Wno-narrowing',
                    'tools/tests/dynos_actor_animation_probe.cpp','src/pc/dynos/dynos_model.cpp',
                    *objects,'-Wl,--gc-sections','-lm','-o',str(exe)],cwd=ROOT,check=True)
    with zipfile.ZipFile(Path(sys.argv[1]).resolve()) as archive:
        for actor,folder in [('black_bobomb_geo','bobomb'),('bobomb_buddy_geo','bobomb'),
                             ('chuckya_geo','chuckya'),('skeeter_geo','skeeter'),('scuttlebug_geo','scuttlebug')]:
            binary=directory/(actor+'.bin')
            binary.write_bytes(archive.read("SwagSkeleton95's Replacements/"+actor+'.bin'))
            expected=directory/'parents';expected.write_text(' '.join(map(str,native_signature(folder,actor))))
            subprocess.run([str(exe),str(binary),str(expected),actor],cwd=ROOT,check=True)
