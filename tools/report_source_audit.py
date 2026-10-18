"""Build a local, searchable report from the bounded live source audit."""
import collections,csv,datetime,html,json
from pathlib import Path

BASE=Path(__file__).resolve().parents[1]/'build/source-audit'
catalog=json.loads((BASE/'catalog.json').read_text(encoding='utf-8'))
notes={
    'Kiryuu':'kiryuu.id → kiryuu.io bağlantısından v7.kiryuu.to adresi doğrulandı. Sitenin herkese açık hx-post arama formu, yeni bölüm kartları ve okuyucu resimleri kullanılıyor.',
    'Night Scans':'nightsup.net → qimanga.com. Sitenin arama API’si, alternatif adlar, cursor ile tam bölüm kataloğu ve herkese açık okuyucu verileri uyarlandı.',
    'WebToons':'Yeni .webtoon_list arama kartları, .detail_list_link bölüm kartları ve mutlak/göreli sayfalama bağlantıları düzeltildi. Her arama sonucu için gereksiz ek sayfa isteği kaldırıldı.',
    'Manhwa Top':'Normal arama sayfası ve noktalama farklarını kabul eden eşleştirme çalışıyor. Bölümler için sitenin güncel /ajax/chapters/ POST isteği kullanılıyor.',
    'ReadMangaToday':'Ana sayfa eski manga içeriği gösteriyor ancak Manga List bağlantısı secret-class sayfasındaki PREMAN123 oyun/bahis içeriğine yönlendi. Kullanılabilir güncel katalog doğrulanamadı.',
    'Manga Buddy':'Arama/okuyucu Next.js verisine uyarlandı. İlk 50 bölüm yerine tam API kataloğu alınıyor. Resimler için yeni sitenin Referer adresi gerekli: https://comizy.io/.',
    'Manga Pill':'Yeni arama kartları ve img.js-page okuyucu seçicileri düzeltildi.',
    'Asura Scans':'asuracomic.net → asurascans.com. Herkese açık arama API’si ve Astro okuyucusu güncellendi.',
    'Rizz Comic':'WordPress araması yerine sitenin /Index/live_search isteği kullanılıyor.',
    'Manga Hub':'Arama en fazla 3 sonuç sayfasıyla sınırlandı; sonraki sayfa hatası ilk sonuçları silmiyor. HTML resimleri okunuyor. İlk canlı denemede 6 sayfa URL’si alındı; sonraki denemelerde bölüm sayfası Cloudflare 403 / WebView2 zaman aşımı verdi. Kararlı okuma doğrulanamadı.',
    'Kangaryu Team':'Yeni katalog uyarlaması ayrıca denendi: Grand blue için 87 bölüm ve 41 sayfa adresi bulundu; resim sunucusu HTTP 404 döndürdü. HTTP/HTTPS denemeleri sonucu değiştirmedi. Deneysel uyarlama teslim edilmedi; kaynak devre dışı kaldı.',
    'MangaDex V5':'One Piece araması ve bölüm API’si çalıştı; seçilen son İngilizce bölümün at-home resim API’si HTTP 404 döndürdü. Tek bölümdeki bu hata sitenin tüm mangalarının bozuk olduğu anlamına gelmez.',
    'Dynasty Scans':'Katalog denemesinde 120 başlık bulundu; seçilen ilk kayıtta bölüm listesi boş. Tek bölüm/one-shot yapısı ayrıca uyarlama gerektirebilir.',
    'Komga':'Kişisel sunucu adresi ve gerekirse hesap ayarı gerektirir; genel manga sitesi değildir.',
    'Tachidesk':'Kişisel sunucu adresi ve gerekirse hesap ayarı gerektirir; genel manga sitesi değildir.',
    'Test Site':'Yerel geliştirici test sunucusudur (localhost:3456).',
    'Luminous Scans':'Ana sayfa HTTP 200 olsa da yalnızca 2 bayt içerik verdi; kullanılabilir manga kataloğu doğrulanamadı.',
    'Lugnica Scan':'Ana sayfa HTTP 200 olsa da yalnızca 2 bayt içerik verdi; eski API geçerli JSON döndürmedi.',
    'Azure Manga':'Ana sayfa /404.html adresine yönlendi; HTTP 200 geçerli katalog kanıtı değil.',
    'Astral Library':'Alan adı yanıt veriyor ancak incelenen içerik genel WordPress yazıları; eski manga API’si HTTP 400.',
}
repurposed={
    'Immortal Updates':'mortalsgroove.com → weinsteinandmiller.com; satılık alan adı sayfası.',
    'Platinum Crown':'platinumscans.com → scatterkakekzeusslot.com; manga dışı site.',
    'Manga 347':'Manga yerine Çince bahis içeriği açılıyor.',
    'Komikid':'magicly.bio/kaya303 adresine yönleniyor; manga okuyucusu değil.',
    'Scylla Scans':'scyllacomics.xyz → vip123.art; manga dışı site.',
}
rows=[]
for item in catalog:
    stem=f'{item["index"]:03}.json'
    path=BASE/'results'/stem
    if not item['disabled'] and (BASE/'retests'/stem).exists():path=BASE/'retests'/stem
    elif not item['disabled'] and (BASE/'catalog-tests'/stem).exists():
        alternative=json.loads((BASE/'catalog-tests'/stem).read_text(encoding='utf-8'))
        if any(s['operation']=='Arama' and s.get('count',0)>0 for s in alternative.get('stages',[])):path=BASE/'catalog-tests'/stem
    data=json.loads(path.read_text(encoding='utf-8'))
    stages={s['operation']:s for s in data.get('stages',[])}
    search,chapters,pages,first=[stages.get(name,{}) for name in ('Arama','Bölümler','Sayfalar','İlk resim')]
    if item['disabled']:state='Devre dışı — yalnız erişim denendi'
    elif item['name'] in ('Komga','Tachidesk','Test Site'):state='Kişisel / yerel sunucu'
    elif item['name'] in repurposed:state='Adres amacı değişmiş'
    elif first.get('success'):state='Örnek bölüm ve ilk resim doğrulandı'
    elif pages.get('count',0)>0:state='Sayfa listesi var; resim doğrulanmadı'
    elif chapters.get('count',0)>0:state='Bölümler var; okuma hatası'
    elif search.get('count',0)>0:state='Arama var; bölüm listesi doğrulanmadı'
    elif search.get('success'):state='Eşleşme yok; okuyucu doğrulanmadı'
    else:state='Arama / erişim hatası'
    errors=' | '.join(f'{name}: {stage["error"]}' for name,stage in stages.items() if stage.get('error'))
    detail=repurposed.get(item['name'],notes.get(item['name'],''))
    meta=BASE/'pages'/f'Disabled_{item["index"]:03}.json'
    if item['disabled'] and meta.exists():
        observation=json.loads(meta.read_text(encoding='utf-8'))
        if observation.get('final'):detail+=' Son adres: '+observation['final']+'.'
        if observation.get('title'):detail+=' Sayfa başlığı: '+html.unescape(observation['title'][0]).strip()+'.'
    rows.append({'Kaynak':item['name'],'Durum':state,'Dil':item['language'],'Adres':data.get('home',item['home']),
        'Sorgu':data.get('query',''),'Arama':search.get('count',''),'Bölüm':chapters.get('count',''),'Sayfa':pages.get('count',''),
        'İlk resim HTTP':first.get('http',''),'Erişim HTTP':stages.get('Erişim',{}).get('http',''),'Hata':errors,'Not':detail,
        'Deneme zamanı':datetime.datetime.fromtimestamp(path.stat().st_mtime).astimezone().isoformat(timespec='seconds')})
with (BASE/'report.csv').open('w',encoding='utf-8-sig',newline='') as output:
    writer=csv.DictWriter(output,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
counts=dict(collections.Counter(row['Durum'] for row in rows))
(BASE/'summary.json').write_text(json.dumps({'entries':len(rows),'active':sum(not x['disabled'] for x in catalog),'disabled':sum(x['disabled'] for x in catalog),'results':counts},ensure_ascii=False,indent=2),encoding='utf-8')
esc=lambda value:html.escape(str(value),quote=True)
table=''.join('<tr>'+''.join('<td>'+esc(row[key])+'</td>' for key in rows[0])+'</tr>' for row in rows)
cards=''.join('<div><b>'+str(count)+'</b><span>'+esc(state)+'</span></div>' for state,count in counts.items())
options=''.join('<option>'+esc(state)+'</option>' for state in counts)
search_rows=[]
for result in sorted((BASE/'search-tests').glob('*.json')):
    data=json.loads(result.read_text(encoding='utf-8'))
    stage=next((s for s in data.get('stages',[]) if s['operation']=='Arama'),{})
    search_rows.append('<tr>'+''.join('<td>'+esc(value)+'</td>' for value in
        [data.get('name',''),data.get('query',''),stage.get('count',0),stage.get('error','') or ('Sonuç alındı' if stage.get('count',0)>0 else 'Eşleşme yok')])+'</tr>')
search_section='<h2>Son arama doğrulaması</h2><p>10 Ekim 2026 · Aşağıdaki sorgular uygulamanın gerçek motoruyla, herkese açık örnekler kullanılarak denendi. Sayılar manga başlığı sayısıdır; kaynak sayısı değildir. Sıfır eşleşme ile erişim hatası ayrı gösterilir.</p><div class="table"><table><thead><tr><th>Kaynak</th><th>Sorgu</th><th>Başlık</th><th>Sonuç</th></tr></thead><tbody>'+''.join(search_rows)+'</tbody></table></div>' if search_rows else ''
document='''<!doctype html><html lang="tr"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GhostManga — Kaynak kontrolü</title><style>
body{margin:32px;font:15px system-ui;background:#111725;color:#e7edf8}h1{font-size:28px;margin-bottom:8px}p{color:#bbc7db;max-width:1100px;line-height:1.6}.stats{display:flex;flex-wrap:wrap;gap:12px;margin:24px 0}.stats div{padding:16px;background:#1b263a;border-radius:10px;max-width:195px}.stats b{display:block;font-size:25px}.stats span{font-size:13px;color:#bccae0}input,select{background:#202d43;color:#eef3ff;border:1px solid #50617e;padding:12px;border-radius:6px;max-width:95%;margin:0 12px 12px 0}input{width:330px}.table{overflow:auto;max-height:70vh;border:1px solid #34445c}table{border-collapse:collapse;width:100%;font-size:13px}th{position:sticky;top:0;background:#22304b;text-align:left}td,th{padding:12px;border-bottom:1px solid #34445c;min-width:85px;vertical-align:top}td:nth-child(1){font-weight:650;min-width:130px}td:nth-child(2){min-width:180px}td:nth-child(11),td:nth-child(12){min-width:260px;max-width:410px}td{overflow-wrap:anywhere}tr:nth-child(even){background:#162035}a{color:#a9c9ff}#count{margin-left:10px}</style>
<h1>GhostManga · Kaynak kontrolü</h1><p>9 Ekim 2026 · 236 kaynak kaydı: 144 etkin uyarlayıcıda arama, 92 eski/devre dışı kayıtta erişim denendi. Sonuç bulunan örneklerde bölüm ve sayfa listesi; son doğrulama turunda ilk resim dosyası kontrol edildi. Kullanıcı kütüphanesi değiştirilmedi.</p>
<p>Bu rapor test anını gösterir. Tek sorguda eşleşme bulunmaması kaynağın bozuk olduğunu kanıtlamaz. HTTP 200 de manga okuyucusunun çalıştığını kanıtlamaz. Örnek bölüm testi tüm bölümlerin çalıştığı anlamına gelmez. Manga Hub’da ayrıştırıcı düzeltildi, ancak son okumalar site korumasına takıldı. Kangaryu’nun yeni yapısı denendi; resimler 404 verdiği için etkinleştirilmedi.</p>
<div class="stats">'''+cards+'''</div><input id="query" type="search" placeholder="Kaynak, dil veya hata içinde ara" aria-label="Kaynaklarda ara"><select id="state" aria-label="Durum filtresi"><option value="">Tüm durumlar</option>'''+options+'''</select><a href="report.csv">CSV raporunu aç</a><span id="count"></span><div class="table"><table><thead><tr>'''+''.join('<th>'+esc(key)+'</th>' for key in rows[0])+'''</tr></thead><tbody>'''+table+'''</tbody></table></div><script>
const rows=[...document.querySelectorAll('tbody tr')],q=document.getElementById('query'),s=document.getElementById('state');function filter(){let n=0;for(const row of rows){const show=row.textContent.toLocaleLowerCase('tr').includes(q.value.toLocaleLowerCase('tr'))&&(!s.value||row.cells[1].textContent===s.value);row.hidden=!show;if(show)n++}document.getElementById('count').textContent=n+' kaynak'}q.addEventListener('input',filter);s.addEventListener('change',filter);filter();</script></html>'''
# The global source filter applies to the historical table, not the query table.
document=document.replace('<div class="table"><table>', '<div class="table"><table id="source-table">',1)
document=document.replace("document.querySelectorAll('tbody tr')","document.querySelectorAll('#source-table tbody tr')")
document=document.replace('<div class="stats">',search_section+'<h2>Tüm kaynakların deneme geçmişi</h2><div class="stats">',1)
(BASE/'report.html').write_text(document,encoding='utf-8')
print(json.dumps(counts,ensure_ascii=False,indent=2))
