"""Opt-in QtScrcpy linkage; generic-window recognition remains available."""
import json
import os
from pathlib import Path
from PySide6 import QtCore,QtGui
import windows_io as win

TOOL_NAMES={'mirror':'QtScrcpy 输入修复版.lnk','recover':'恢复游戏控制.lnk','editor':'游戏按键位置编辑器.lnk','touches':'清理手机残留触点.lnk'}

class GameBridge:
    def __init__(self):
        self.root=Path(os.environ.get('LOCALAPPDATA',str(Path.home()/'AppData/Local')))/'Programs/QtScrcpy'
        self.folder=Path.home()/'Desktop'/'Monster Hunter Outlanders'
    def mirrors(self):
        return [(h,t) for h,t in win.windows() if t.startswith('Phone-') and Path(win.process_path(h)).name.lower()=='qtscrcpy.exe']
    def open_tool(self,key):
        path=self.folder/TOOL_NAMES[key]
        if not path.exists():path=self.root/'tool-shortcuts'/TOOL_NAMES[key]
        if not path.exists():raise FileNotFoundError(str(path))
        if not QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(path))):raise OSError(str(path))
    def mapping_key(self):
        paths=[self.root/'QtScrcpy-input-fix-v4.2.1/keymap/Generic-Alt.json',Path(os.environ.get('APPDATA',''))/'QtScrcpy/QtScrcpy/keymap/Generic-Alt.json']
        for p in paths:
            try:
                value=json.loads(p.read_text(encoding='utf-8-sig')).get('switchKey','')
                return {'Key_Control':'Ctrl','Key_Alt':'Alt','Key_QuoteLeft':'~','Key_F1':'F1'}.get(value,value.removeprefix('Key_') if hasattr(value,'removeprefix') else value.replace('Key_',''))
            except (OSError,ValueError):pass
        return '?'
    def focus(self,hwnd):
        if not win.is_target(hwnd):return False
        win.user.ShowWindow.argtypes=[win.W.HWND,win.W.INT]
        win.user.SetForegroundWindow.argtypes=[win.W.HWND]
        if win.user.IsIconic(hwnd):win.user.ShowWindow(hwnd,9)
        return bool(win.user.SetForegroundWindow(hwnd))
