"""Second bounded pass for sources without the first test title, or named fixes."""
import concurrent.futures,datetime,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'build/source-audit'
catalog=json.loads((BASE/'catalog.json').read_text(encoding='utf-8'))
search_only='--search-only' in sys.argv
queries=dict(arg.split('=',1) if '=' in arg else (arg,'One Piece') for arg in sys.argv[1:] if arg!='--search-only')
names=list(queries)
results=BASE/('search-tests' if search_only else 'retests' if names else 'catalog-tests');results.mkdir(exist_ok=True)
selected=[]
for item in catalog:
    old=json.loads((BASE/'results'/f'{item["index"]:03}.json').read_text(encoding='utf-8'))
    if (names and item['name'] in names) or (not names and item['name']!='NHentai' and any(s['operation']=='Arama' and s.get('success') and s['count']==0 for s in old.get('stages',[]))):selected.append(item)
def slot(n):
    folder=BASE/f'query-slot-{n}';(folder/'portable_data').mkdir(parents=True,exist_ok=True)
    exe=folder/'source_audit.exe';shutil.copy2(BASE/'tool/source_audit.exe',exe)
    for item in selected[n::3]:
        result=folder/'result.json';result.unlink(missing_ok=True)
        try:status=subprocess.run([str(exe),item['name'],queries[item['name']] if names else '']+(['--search-only'] if search_only else []),cwd=ROOT,capture_output=True,timeout=150).returncode
        except subprocess.TimeoutExpired:status='timeout'
        data=json.loads(result.read_text(encoding='utf-8')) if result.exists() else item.copy();data['exit']=status
        target=results/f'{item["index"]:03}.json'
        if target.exists():
            history=BASE/'history';history.mkdir(exist_ok=True)
            shutil.copy2(target,history/f'{item["index"]:03}-{datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")}.json')
        target.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
        print(item['name']+': '+', '.join(f'{s["operation"]}={s.get("count",0)} {s.get("error","")}' for s in data.get('stages',[])),flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as workers:list(workers.map(slot,range(3)))
