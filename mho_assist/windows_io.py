"""Win32 capture geometry and foreground-only, paired SendInput events."""
import ctypes as C
from ctypes import wintypes as W
import time
import random

user=C.WinDLL('user32',use_last_error=True)
kernel=C.WinDLL('kernel32',use_last_error=True)
user.GetForegroundWindow.restype=W.HWND
for _name in ['IsWindow','IsWindowVisible','IsIconic']:
    getattr(user,_name).argtypes=[W.HWND]
user.GetAncestor.argtypes=[W.HWND,W.UINT];user.GetAncestor.restype=W.HWND
user.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
user.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
user.ClientToScreen.argtypes=[W.HWND,C.POINTER(W.POINT)]
user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
kernel.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
kernel.CloseHandle.argtypes=[W.HANDLE]

def process_path(hwnd):
    pid=W.DWORD();user.GetWindowThreadProcessId(hwnd,C.byref(pid))
    h=kernel.OpenProcess(0x1000,False,pid.value)
    if not h:return ''
    try:
        size=W.DWORD(4096);buf=C.create_unicode_buffer(4096)
        return buf.value if kernel.QueryFullProcessImageNameW(h,0,buf,C.byref(size)) else ''
    finally:kernel.CloseHandle(h)

def title(hwnd):
    buf=C.create_unicode_buffer(1024);user.GetWindowTextW(hwnd,buf,1024);return buf.value

def is_target(hwnd):
    return bool(hwnd and user.IsWindow(hwnd))

def windows():
    found=[]
    cb=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    @cb
    def visit(h,_):
        if user.IsWindowVisible(h) and title(h) and title(h)!='方向序列助手' and user.GetWindow(h,4)==0:
            found.append((int(h),title(h)))
        return True
    user.EnumWindows(visit,0)
    return found

def foreground(hwnd):
    return user.GetForegroundWindow()==hwnd and not user.IsIconic(hwnd)

def client_rect(hwnd):
    r=W.RECT();p=W.POINT(0,0)
    if not user.GetClientRect(hwnd,C.byref(r)) or not user.ClientToScreen(hwnd,C.byref(p)):
        raise RuntimeError('目标窗口已关闭')
    return (p.x,p.y,r.right,r.bottom)

user.WindowFromPoint.argtypes=[W.POINT];user.WindowFromPoint.restype=W.HWND
def window_at(x,y):
    h=user.WindowFromPoint(W.POINT(x,y))
    return int(user.GetAncestor(h,2)) if h else None

class MONITORINFOEX(C.Structure):
    _fields_=[('cbSize',W.DWORD),('rcMonitor',W.RECT),('rcWork',W.RECT),('dwFlags',W.DWORD),('szDevice',W.WCHAR*32)]

def monitors():
    found={}
    callback=C.WINFUNCTYPE(W.BOOL,W.HANDLE,W.HDC,C.POINTER(W.RECT),W.LPARAM)
    user.GetMonitorInfoW.argtypes=[W.HANDLE,C.POINTER(MONITORINFOEX)]
    @callback
    def visit(handle,hdc,rect,data):
        info=MONITORINFOEX();info.cbSize=C.sizeof(info)
        if user.GetMonitorInfoW(handle,C.byref(info)):
            r=info.rcMonitor;found[info.szDevice.lower()]=(r.left,r.top,r.right-r.left,r.bottom-r.top)
        return True
    user.EnumDisplayMonitors(None,None,visit,0)
    return found

def game_rect(hwnd,relative=None):
    x,y,w,h=client_rect(hwnd)
    if w<200 or h<100:raise RuntimeError('投屏窗口太小或已最小化')
    if relative:
        a,b,c,d=relative
        return (round(x+a*w),round(y+b*h),round((c-a)*w),round((d-b)*h))
    ratio=1080/484
    gw=min(w,h*ratio);gh=gw/ratio
    return (round(x+(w-gw)/2),round(y+(h-gh)/2),round(gw),round(gh))

class MOUSEINPUT(C.Structure):
    _fields_=[('dx',W.LONG),('dy',W.LONG),('mouseData',W.DWORD),('dwFlags',W.DWORD),('time',W.DWORD),('dwExtraInfo',C.c_size_t)]
class KEYBDINPUT(C.Structure):
    _fields_=[('wVk',W.WORD),('wScan',W.WORD),('dwFlags',W.DWORD),('time',W.DWORD),('dwExtraInfo',C.c_size_t)]
class HARDWAREINPUT(C.Structure):
    _fields_=[('uMsg',W.DWORD),('wParamL',W.WORD),('wParamH',W.WORD)]
class UNION(C.Union):
    _fields_=[('ki',KEYBDINPUT),('mi',MOUSEINPUT),('hi',HARDWAREINPUT)]
class INPUT(C.Structure):
    _anonymous_=('u',)
    _fields_=[('type',W.DWORD),('u',UNION)]
user.SendInput.argtypes=[W.UINT,C.POINTER(INPUT),C.c_int]

SCANS=dict(up=0x48,down=0x50,left=0x4b,right=0x4d)
NAV_SCANS=dict(shift=0x2a,bracket_left=0x1a,bracket_right=0x1b)
def key(label,up=False):
    event=INPUT(type=1)
    scan=SCANS[label] if label in SCANS else NAV_SCANS[label]
    event.ki=KEYBDINPUT(0,scan,0x0008|(0x0001 if label in SCANS else 0)|(0x0002 if up else 0),0,0)
    if user.SendInput(1,C.byref(event),C.sizeof(INPUT))!=1:
        raise RuntimeError('Windows 拒绝按键。请确保工具和目标软件权限级别一致。')

def timing_ms(base,spread,rng=None):
    if spread<=0:return max(0,float(base))
    return max(5.,(rng or random).uniform(max(5.,base-spread),max(5.,base+spread)))

def send_sequence(hwnd,sequence,active,hold_ms=18,gap_ms=18,hold_jitter_ms=0,gap_jitter_ms=0):
    if not is_target(hwnd):raise RuntimeError('目标窗口已关闭')
    sent=0
    def wait(ms):
        end=time.perf_counter()+ms/1000
        while time.perf_counter()<end:
            if not active() or not foreground(hwnd):return False
            time.sleep(min(.002,max(0,end-time.perf_counter())))
        return True
    for label in sequence:
        if not active() or not foreground(hwnd):break
        key(label)
        try:
            proceed=wait(timing_ms(hold_ms,hold_jitter_ms))
        finally:
            key(label,True)
        sent+=1
        if not proceed or not wait(timing_ms(gap_ms,gap_jitter_ms)):break
    return sent
