from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
from urllib.parse import urlparse,parse_qs
import threading,struct,zlib
counts={};counts_lock=threading.Lock()
def chunk(tag,body):return struct.pack('>I',len(body))+tag+body+struct.pack('>I',zlib.crc32(tag+body))
image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\xff\xff\xff\xff'))+chunk(b'IEND',b'')
class Handler(BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path.startswith('/image/'):
   if 'referer=comizy' in self.path and self.headers.get('Referer')!='https://comizy.io/':
    self.send_response(403);self.end_headers();return
   name=urlparse(self.path).path
   with counts_lock:
    counts[name]=counts.get(name,0)+1;attempt=counts[name]
   failed=(name.endswith('/5.png') and attempt<3) or (name.endswith('/9.png') and 'permanent=1' in self.path)
   corrupt=name.endswith('/6.png') and attempt<3
   self.send_response(503 if failed else 200);self.send_header('Content-Type','text/html' if failed else 'image/png');self.end_headers();self.wfile.write(b'<html>unavailable</html>' if failed else image[:16] if corrupt else image);return
  if self.path.startswith('/redirect'):
   token=parse_qs(urlparse(self.path).query)['token'][0]
   if ('review_redirect='+token) not in self.headers.get('Cookie',''):
    self.send_response(200); self.end_headers(); self.wfile.write(('<html><title>Redirecting...</title><script>document.cookie="review_redirect='+token+';path=/";location.reload();</script></html>').encode()); return
   self.send_response(200); self.end_headers(); self.wfile.write(b'js-navigation-completed'); return
  self.send_response(404 if self.path == '/missing' else 200); self.end_headers(); self.wfile.write(b'<html>review</html>')
 def do_POST(self):
  text=self.rfile.read(int(self.headers.get('Content-Length',0)))+b'|'+self.headers.get('X-Review','').encode()
  self.send_response(201);self.send_header('X-Reply','verified');self.end_headers();self.wfile.write(text)
 def log_message(self,*args): pass
ThreadingHTTPServer(('127.0.0.1',18881),Handler).serve_forever()
