"""Run the actual C++ adapters against all catalogued sources, three at a time."""
import concurrent.futures
import json
import shutil
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'build/source-audit'
EXE=BASE/'tool/source_audit.exe'
CATALOG=json.loads(subprocess.check_output([str(EXE),'list'],cwd=ROOT,text=True,encoding='utf-8'))
(BASE/'catalog.json').write_text(json.dumps(CATALOG,ensure_ascii=False,indent=2),encoding='utf-8')
(BASE/'results').mkdir(exist_ok=True)

def run_slot(slot):
    folder=BASE/f'slot-{slot}'
    (folder/'portable_data').mkdir(parents=True,exist_ok=True)
    binary=folder/EXE.name
    shutil.copy2(EXE,binary)
    for source in CATALOG[slot::3]:
        output=BASE/'results'/f'{source["index"]:03}.json'
        if output.exists():continue
        result=folder/'result.json'
        result.unlink(missing_ok=True)
        try:
            completed=subprocess.run([str(binary),str(source['index'])],cwd=ROOT,capture_output=True,timeout=150)
            status=completed.returncode
        except subprocess.TimeoutExpired:
            status='timeout'
        record=json.loads(result.read_text(encoding='utf-8')) if result.exists() else dict(source)
        record['exit']=status
        output.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
        print(f'{source["index"]+1}/{len(CATALOG)} {source["name"]}: '+', '.join(f'{s["operation"]}={s.get("count",0)} {s.get("error","")}' for s in record.get('stages',[])),flush=True)

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as workers:
    list(workers.map(run_slot,range(3)))
print('Source audit complete.',flush=True)
