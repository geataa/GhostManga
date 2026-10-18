"""Report only the 18 sources requested on 2026-10-10; no user library data."""
import datetime
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/source-audit'
rows = [
    ('MangaKawaii', 123, 'Kısmi doğrulama', 'V5 arama, bölüm sayfalama ve okuyucu verisi uyarlandı. Eski .io kayıtları .fr adresine taşınıyor. Tarayıcıda yüklenmiş resimler için 32 MiB sınırlı aktarım eklendi. CDN indirmesi ve uzun katalog canlı testte engelle karşılaşıyor.'),
    ('Sad Scans', 113, 'Kısmi doğrulama', '.net /seriler ve /reader yolları uyarlandı. Eksik 20 bölümlük SEO özeti tam katalog kabul edilmiyor. Kullanıcı tarayıcıda açıldığını doğruladı; test bağlantısında 403 ve boş JavaScript ekranı sürüyor.'),
    ('ManhwaHentai', 9, 'Çıkarıldı', 'Eski WordPress manga kataloğu yerine farklı içerik üretim hizmeti dönüyor. Manga araması/bölüm okuyucusu doğrulanamadı.'),
    ('Dragon Tea', 15, 'Kısmi doğrulama', 'Arama adresindeki yanlış www kaldırıldı; /novel/ bölüm yolları tanındı. Normal tarayıcıda Comics kataloğu açılıyor; uygulama denetimi güvenlik engelinde kalıyor.'),
    ('Toonily', 8, 'Doğrulandı', 'Güncel /serie/ okuyucu yolu düzeltildi. Solo Leveling aramasında seri, bölüm, sayfa adresleri ve gerçek görsel indirimi doğrulandı.'),
    ('Evil Flowers', 1, 'Çıkarıldı', 'reader.evilflowers.com DNS çözümlemesi başarısız.'),
    ('FunManga', 3, 'Çıkarıldı', 'Alan adı satış sayfasına yönlendiriyor.'),
    ('Manga Komi', 6, 'Çıkarıldı', 'Geçerli manga kataloğu yerine Loading/yönlendirme sayfası dönüyor.'),
    ('Webtoon.xyz', 7, 'Çıkarıldı', 'HTTP erişimi engelli; normal tarayıcıda da bağlantı sıfırlanıyor.'),
    ('Comic Kiba', 14, 'Çıkarıldı', 'HTTP 200 dönse de Redirecting sayfasından gerçek kataloğa ulaşılamadı.'),
    ('Aqua Manga', 11, 'Çıkarıldı', 'aquascans.com DNS çözümlemesi başarısız.'),
    ('247 Manga', 10, 'Çıkarıldı', 'ww17.247manga.com manga kataloğu yerine park/yönlendirme sayfası döndürüyor.'),
    ('Hiperdex', 17, 'Çıkarıldı', 'Kullanıcının verdiği hiperdex.top adresinde DNS çözümlemesi başarısız.'),
    ('IsekaiScans', 19, 'Çıkarıldı', 'Redirecting sayfasından gerçek kataloğa ulaşılamadı.'),
    ('Kun Manga', 20, 'Çıkarıldı', 'VPN kapalı normal tarayıcı denemesinde kaynak sunucu 522 bağlantı zaman aşımı verdi. Kalıcı kapanma sonucu çıkarılmadı.'),
    ('Leviatan Scans', 21, 'Çıkarıldı', 'lscomic.com bağlantıyı kesiyor veya zaman aşımına düşüyor; çalışan katalog doğrulanamadı.'),
    ('Lily Manga', 23, 'Çıkarıldı', 'Redirecting sayfasından gerçek kataloğa ulaşılamadı.'),
    ('Manga Bob', 25, 'Çıkarıldı', 'ww38.mangabob.com bağlantı zaman aşımı; mevcut kaynak aramasında manga kataloğu bulunamadı.'),
]

now = datetime.datetime.now().astimezone().isoformat(timespec='seconds')
results = []
for name, index, status, note in rows:
    row = dict(name=name, status=status, note=note)
    path = BASE / 'retests' / f'{index:03}.json'
    if path.exists():
        data = json.loads(path.read_text(encoding='utf-8'))
        row['latest_audit'] = {key: data[key] for key in ('query', 'complete', 'stages') if key in data}
        if status == 'Doğrulandı' and not any(s.get('operation') == 'İlk resim' and s.get('success') for s in data.get('stages', [])):
            row['status'] = 'Erişim dalgalı'
            row['note'] += ' Son canlı denemelerde zaman aşımı ve bağlantı kesilmesi görüldü; önceki başarı sürekli erişim garantisi değildir.'
            for previous in sorted((BASE / 'history').glob(f'{index:03}-*.json'), reverse=True):
                past = json.loads(previous.read_text(encoding='utf-8'))
                if any(s.get('operation') == 'İlk resim' and s.get('success') for s in past.get('stages', [])):
                    row['earlier_success'] = past.get('stages', [])
                    break
    results.append(row)
(BASE / 'requested-report.json').write_text(json.dumps({'updated': now, 'sources': results}, ensure_ascii=False, indent=2), encoding='utf-8')

table = []
for row in results:
    stages = row.get('latest_audit', {}).get('stages', [])
    evidence = ' · '.join(f"{s['operation']}: {s.get('count', 0)} ({'başarılı' if s.get('success') else 'başarısız'})" for s in stages)
    if row.get('earlier_success'):
        evidence += ' · Önceki başarılı örnek: ' + ', '.join(f"{s['operation']} {s.get('count',0)}" for s in row['earlier_success'] if s.get('success') and s.get('count'))
    color = 'ok' if row['status'] == 'Doğrulandı' else 'removed' if row['status'] == 'Çıkarıldı' else 'warn'
    table.append(f'<tr><th>{html.escape(row["name"])}</th><td><span class="{color}">{row["status"]}</span></td><td>{html.escape(row["note"])}<small>{html.escape(evidence)}</small></td></tr>')
document = '''<!doctype html><html lang="tr"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GhostManga · İstenen 18 kaynak</title><style>
body{background:#111626;color:#e5e9f2;font:16px/1.55 system-ui,sans-serif;margin:0;padding:36px;max-width:1260px;margin-inline:auto}h1{font-size:30px;margin-bottom:8px}p{color:#b9c2d8}table{width:100%;border-collapse:collapse;margin-top:28px}th,td{padding:17px 14px;border-bottom:1px solid #2d3650;text-align:left;vertical-align:top}th{min-width:145px}thead{background:#1d263c}small{display:block;color:#91a0bd;margin-top:8px}.ok,.warn,.removed{display:inline-block;padding:4px 10px;border-radius:7px;white-space:nowrap}.ok{background:#203c54;color:#a8daff}.warn{background:#423821;color:#f1d48b}.removed{background:#42282e;color:#e8acb4}.note{padding:18px;background:#1c263c;border-radius:12px}a{color:#9ac7ff}@media(max-width:700px){body{padding:16px}table{font-size:13px}th,td{padding:10px 6px}th{min-width:90px}}
</style><h1>İstenen 18 kaynağın durumu</h1>'''
document += f'<p>Son güncelleme: {html.escape(now)} · VPN kapatıldıktan sonra bağlantı denemeleri tekrarlandı.</p>'
document += '<p class="note">4 kaynakta uyarlama yapıldı; canlı doğrulamanın kapsamı ve son bağlantı sonucu aşağıda ayrı gösteriliyor. 14 kaynak katalogdan çıkarıldı. Çıkarılanlar kalıcı olarak kapanmış kabul edilmiyor. Kaynak kodları geri alınabilir; kayıtlı mangalar, okuma ilerlemesi ve indirilen dosyalar silinmedi.</p>'
document += '<table><thead><tr><th>Kaynak</th><th>Sonuç</th><th>Bulgu ve işlem</th></tr></thead><tbody>' + ''.join(table) + '</tbody></table>'
document += '<p>Doğrulama: kaynak ayrıştırıcı testleri, eski adres geçişleri, kilitli bölüm filtresi, eksik katalog koruması, görsel aktarımında bellek sınırı ve derleme. Tarayıcı görsel aktarımının canlı MangaKawaii okuyucusunda uçtan uca doğrulaması tamamlanamadı.</p></html>'
(BASE / 'requested-report.html').write_text(document, encoding='utf-8')
print(BASE / 'requested-report.html')
