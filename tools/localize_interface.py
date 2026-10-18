"""Connect known UI literals to the catalog; do not translate user/source data."""
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
header=(root/'src/core/I18n.h').read_text(encoding='utf-8-sig')
known=set()
for key,tr,en in re.findall(r'X\((\w+),\s*L"((?:[^"\\]|\\.)*)",\s*L"((?:[^"\\]|\\.)*)"\)',header):
    known.update((tr,en))
for line in (root/'resources/i18n/interface.tsv').read_text(encoding='utf-8').splitlines():
    if line and not line.startswith('#'):
        key,tr,en=line.split('|');known.update((tr,en))
extended=root/'resources/i18n/extended.tsv'
if extended.exists():
    for line in extended.read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            key,tr,en,*translations=line.split('|');known.update((tr,en))
def decoration(c):return c.isspace() or c in '+‹›' or 0x2190<=ord(c)<=0x2bff or 0x1f000<=ord(c)<=0x1ffff or ord(c)==0xfe0f
def connected(text):
    if text in known:return True
    pos=0
    while pos<len(text) and decoration(text[pos]):pos+=1
    return text[pos:].rstrip() in known
unknown=set();total=0
for path in (root/'src/ui').glob('*.cpp'):
    if path.name in ('Renderer.cpp','Icons.cpp','Widgets.cpp'):continue
    source=path.read_text(encoding='utf-8-sig')
    def replace(match):
        global total
        text=match.group(1)
        if connected(text):
            if re.search(r'\.(?:r?find|compare)\(\s*$',source[max(0,match.start()-30):match.start()]):return match.group()
            # Skip an already wrapped literal. The script is safe to rerun.
            if re.search(r'i18n::(?:Literal|Text)\(\s*$',source[max(0,match.start()-35):match.start()]):return match.group()
            total+=1;return 'i18n::Literal('+match.group()+')'
        if len(text)>3 and not re.search(r'[\\/]|^\[',text):unknown.add(text)
        return match.group()
    updated=re.sub(r'L"((?:[^"\\]|\\.)*)"',replace,source)
    if updated!=source:
        if '#include "core/I18n.h"' not in updated:updated='#include "core/I18n.h"\n'+updated
        path.write_text(updated,encoding='utf-8')
(root/'build/i18n-unmapped.txt').write_text('\n'.join(sorted(unknown)),encoding='utf-8')
print(f'Connected {total} static UI literals. Remaining Turkish literals: {len(unknown)} (build/i18n-unmapped.txt)')
