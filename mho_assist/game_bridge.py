"""Opt-in QtScrcpy linkage; generic-window recognition remains available."""
import json
import os
import subprocess
from pathlib import Path
from PySide6 import QtCore,QtGui
from mho_assist import windows_io as win

TOOL_NAMES={'mirror':'QtScrcpy 输入修复版.lnk','recover':'恢复游戏控制.lnk','editor':'游戏按键位置编辑器.lnk','touches':'清理手机残留触点.lnk'}

class GameBridge:
    def __init__(self,mirror_executable=None):
        self.root=Path(os.environ.get('LOCALAPPDATA',str(Path.home()/'AppData/Local')))/'Programs/QtScrcpy'
        self.folder=Path.home()/'Desktop'/'Monster Hunter Outlanders'
        self.mirror_executable=Path(mirror_executable) if mirror_executable else None
    def mirrors(self):
        return [(h,t) for h,t in win.windows() if t.startswith('Phone-') and Path(win.process_path(h)).name.lower()=='qtscrcpy.exe']
    def open_tool(self,key):
        if key=='mirror' and self.mirror_executable:
            subprocess.Popen([str(self.mirror_executable)],cwd=self.mirror_executable.parent)
            return
        path=self.folder/TOOL_NAMES[key]
        if not path.exists():path=self.root/'tool-shortcuts'/TOOL_NAMES[key]
        if not path.exists():raise FileNotFoundError(str(path))
        if not QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(path))):raise OSError(str(path))
    def mapping_key(self):
        paths=[]
        if self.mirror_executable:
            keymap=self.mirror_executable.parent/'keymap'
            paths.extend([keymap/'MHO-Dance.json',keymap/'Generic-Alt.json'])
        paths.extend([
            self.root/'QtScrcpy-input-fix-v4.2.1/keymap/Generic-Alt.json',
            Path(os.environ.get('APPDATA',''))/'QtScrcpy/QtScrcpy/keymap/Generic-Alt.json',
        ])
        for p in paths:
            try:
                profile=json.loads(p.read_text(encoding='utf-8-sig'))
            except (OSError,ValueError):
                continue
            value=profile.get('switchKey') if isinstance(profile,dict) else None
            if isinstance(value,str) and value:
                return {'Key_Control':'Ctrl','Key_QuoteLeft':'~'}.get(value,value[4:] if value.startswith('Key_') else value)
        return '?'
    def focus(self,hwnd):
        if not win.is_target(hwnd):return False
        win.user.ShowWindow.argtypes=[win.W.HWND,win.W.INT]
        win.user.SetForegroundWindow.argtypes=[win.W.HWND]
        if win.user.IsIconic(hwnd):win.user.ShowWindow(hwnd,9)
        return bool(win.user.SetForegroundWindow(hwnd))
