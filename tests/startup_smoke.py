"""Launch only an isolated test copy and verify hidden startup plus clean close."""
import ctypes,ctypes.wintypes as wt,subprocess,time,shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
directory=root/'build/review/startup-smoke'
directory.mkdir(parents=True,exist_ok=True)
data=directory/'portable_data';data.mkdir(exist_ok=True)
(data/'settings.ini').write_text('[arkaplan]\nkapatinca_tepsiye=0\ntarama_araligi_dk=0\n[pencere]\ngecerli=1\nbuyutulmus=1\nx=100\ny=100\ngenislik=1000\nyukseklik=700\n',encoding='utf-8')
(data/'library.json').write_text('{"mangas":[],"bookmarks":[]}',encoding='utf-8')
exe=directory/'GhostManga.exe';shutil.copy2(root/'GhostManga.exe',exe)
process=subprocess.Popen([str(exe),'--startup'],cwd=directory,creationflags=subprocess.CREATE_NO_WINDOW)
user=ctypes.WinDLL('user32',use_last_error=True)
callback_type=ctypes.WINFUNCTYPE(wt.BOOL,wt.HWND,wt.LPARAM)
user.EnumWindows.argtypes=[callback_type,wt.LPARAM]
user.GetWindowThreadProcessId.argtypes=[wt.HWND,ctypes.POINTER(wt.DWORD)]
user.GetClassNameW.argtypes=[wt.HWND,wt.LPWSTR,ctypes.c_int]
user.IsWindowVisible.argtypes=[wt.HWND]
user.PostMessageW.argtypes=[wt.HWND,wt.UINT,wt.WPARAM,wt.LPARAM]
owned=[]
@callback_type
def find(hwnd,unused):
 pid=wt.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
 if pid.value==process.pid:
  name=ctypes.create_unicode_buffer(128);user.GetClassNameW(hwnd,name,128)
  if name.value=='GhostManga.MainWindow':owned.append(hwnd)
 return True
try:
 for _ in range(100):
  user.EnumWindows(find,0)
  if owned:break
  assert process.poll() is None,'Process exited before creating its window'
  time.sleep(.1)
 assert owned,'Test window was not created'
 time.sleep(1)
 assert not user.IsWindowVisible(owned[0]),'Startup unexpectedly showed the window'
 assert user.PostMessageW(owned[0],0x0010,0,0)
 assert process.wait(timeout=15)==0
 print('Isolated startup stays hidden with maximized saved placement and closes cleanly: PASS')
finally:
 if process.poll() is None:
  if owned:user.PostMessageW(owned[0],0x0010,0,0)
  try:process.wait(timeout=5)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
