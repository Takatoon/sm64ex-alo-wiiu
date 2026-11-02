"""Regenerate the native actor symbol/name registry from public actor headers."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
names=set()
for path in sorted((ROOT/'actors').glob('*.h')):
    names.update(re.findall(r'extern\s+const\s+GeoLayout\s+(\w+)\s*\[',path.read_text()))
text='/* Actor symbols exported by this public port. Missing optional symbols are weak. */\nextern "C" {\n'
text+=''.join('extern const GeoLayout '+name+'[] __attribute__((weak));\n' for name in sorted(names))
text+='}\nstruct NativeActor { const char *name; const GeoLayout *layout; };\nstatic const NativeActor native_actors[]={\n'
text+='    {"bowser2_geo",bowser_geo_no_shadow},\n'
text+=''.join('    {"'+name+'",'+name+'},\n' for name in sorted(names))+'};\n'
(ROOT/'src/pc/dynos/dynos_actor_registry.inc.h').write_text(text)
print('Registered',len(names),'native actor symbols plus the bowser2 alias')
