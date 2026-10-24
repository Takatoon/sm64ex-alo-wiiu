"""Exercise native DynOS loading with user-provided files; never imports assets into source."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build';build.mkdir(exist_ok=True)
exe=build/'dynos-reader'
subprocess.run(['g++','-std=c++17','-O1','-D_LANGUAGE_C_PLUS_PLUS=1',
                '-DF3DEX_GBI_2=1','-DF3DEX_GBI_2E=1','-Iinclude','-Isrc',
                'src/pc/dynos/dynos_model.cpp','tools/tests/dynos_model_test.cpp',
                '-o',str(exe)],cwd=ROOT,check=True)
with tempfile.TemporaryDirectory() as temporary:
    def check(label,data):
        file=Path(temporary)/'model.bin';file.write_bytes(data)
        result=subprocess.run([str(exe),str(file)],capture_output=True,text=True)
        print(('PASS' if result.returncode==0 else 'REJECT')+' '+label+': '+(result.stdout+result.stderr).strip(),flush=True)
    for argument in sys.argv[1:]:
        path=Path(argument)
        if path.suffix.lower()=='.zip':
            with zipfile.ZipFile(path) as archive:
                for name in archive.namelist():
                    if name.endswith('_geo.bin') and 'cap_geo.bin' not in name:
                        check(path.name+'/'+name,archive.read(name))
        else:check(path.name,path.read_bytes())
