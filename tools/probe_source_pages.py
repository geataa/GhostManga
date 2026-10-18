"""Fetch public diagnostic HTML with normal TLS validation and bounded reads."""
import concurrent.futures,json,re,urllib.request,urllib.error,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/source-audit/pages';OUT.mkdir(exist_ok=True)
def probe(item):
    name,url=item
    try:
        req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36'})
        try: response=urllib.request.urlopen(req,timeout=10)
        except urllib.error.HTTPError as error: response=error
        with response:
            body=response.read(2*1024*1024).decode('utf-8',errors='replace')
            meta={'url':url,'final':response.url,'status':response.status,'bytes':len(body),'title':re.findall(r'<title[^>]*>(.*?)</title>',body,re.S|re.I)[:1]}
        if '--metadata-only' not in sys.argv:
            (OUT/(name+'.html')).write_text(body,encoding='utf-8')
    except Exception as error:meta={'url':url,'error':str(error)}
    (OUT/(name+'.json')).write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf-8')
    print(name,json.dumps(meta,ensure_ascii=False),flush=True)
items=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(probe,items.items()))
