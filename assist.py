# -*- coding: utf-8 -*-
"""Local arrow sequence helper. Starts paused; F8 arms, F9 pauses."""
import os,sys,time,importlib.util
from pathlib import Path
os.environ.setdefault('QT_LOGGING_RULES','qt.qpa.window=false')
# Some Anaconda installations ship a qt.conf that points outside PySide6.
# Resolve the matching bundled Qt plugins explicitly before QApplication.
_qt_dir=Path(importlib.util.find_spec('PySide6').origin).parent
os.environ['QT_PLUGIN_PATH']=str(_qt_dir/'plugins')
os.environ['QT_QPA_PLATFORM_PLUGIN_PATH']=str(_qt_dir/'plugins'/'platforms')
from PySide6 import QtCore,QtGui,QtWidgets
import tempfile
from app_window import Main,APP_DIR,ROOT
import windows_io as win

STYLE="""QWidget{background:#1b2431;color:#e9edf4} QPushButton,QToolButton,QComboBox,QSpinBox,QDoubleSpinBox{padding:5px;border:1px solid #46576b;border-radius:4px} QPushButton:hover,QToolButton:hover{background:#35465a} QMenu{border:1px solid #46576b} QMenu::item{padding:6px 16px} QMenu::item:selected{background:#35465a} QCheckBox::indicator{width:14px;height:14px;border:1px solid #7b8ca2;border-radius:3px;background:#141c28} QCheckBox::indicator:checked{background:#6df0bb;border:2px solid #6df0bb}"""

def seed_demo(ui):
    import random,cv2
    rng=random.Random(14)
    for i in range(24):
        seq=[rng.choice(['up','down','left','right']) for _ in range(min(7,3+i//4))]
        event=dict(kind='round',id=f'demo:{i}',sequence=seq,confidence=min(.995,max(.6,rng.gauss(.892,.024))),ms=rng.uniform(3,8))
        ui.store.record(event)
        if i<19:
            ui.store.record(dict(kind='input',round_id=event['id'],sent=len(seq),total=len(seq)))
            ui.store.record(dict(kind='feedback',round_id=event['id'],confirmed=len(seq) if i!=8 else 2,outcome='confirmed' if i!=8 else 'unconfirmed'))
    for i in range(60):
        mean=4.5+rng.uniform(-1.4,1.7)+(2 if 25<i<30 else 0)
        ui.store.record(dict(kind='timing',id=f'demo:t{i}',n=50,total_ms=mean*50,low=mean*.6,high=mean*1.5))
    path=ROOT/'templates'/'preview.png'
    row=cv2.imread(str(path))
    if row is not None:ui.show_preview(row,'↓ ← ↑   置信度 96%   识别 4.8 ms')
    ui.on_cycle(dict(phase=2,counts=[10,6,0],state='playing',step=0,games=1,error=''))
    ui.refresh_stats()

def main():
    app=QtWidgets.QApplication(sys.argv);app.setStyle('Fusion');app.setFont(QtGui.QFont('Microsoft YaHei',9));app.setStyleSheet(STYLE)
    smoke='--smoke-test' in sys.argv;temporary=tempfile.TemporaryDirectory() if smoke else None
    ui=Main(data_dir=temporary.name if temporary else None,smoke=smoke,language='en' if '--english' in sys.argv else None);ui.show()
    if smoke:
        seed_demo(ui)
        def snap():
            output=APP_DIR/'analysis';output.mkdir(exist_ok=True)
            ui.grab().save(str(output/('ui_en.png' if ui.lang=='en' else 'ui.png')));ui.close();app.quit()
        QtCore.QTimer.singleShot(800,snap)
    result=app.exec()
    if temporary:temporary.cleanup()
    return result

if __name__=='__main__':raise SystemExit(main())
