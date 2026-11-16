"""Send real OS key events only to a dedicated test window; never to the game."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import assist
from PySide6 import QtCore,QtWidgets
from unittest.mock import patch
import windows_io as win

events=[]
class Receiver(QtWidgets.QWidget):
    def keyPressEvent(self,e):events.append(('down',e.key()))
    def keyReleaseEvent(self,e):events.append(('up',e.key()))

app=QtWidgets.QApplication([]);ui=Receiver();ui.setWindowTitle('Arrow helper input validation');ui.resize(350,100);ui.show();ui.activateWindow();ui.raise_()
win.user.SetForegroundWindow.argtypes=[win.W.HWND]
win.user.SetForegroundWindow(int(ui.winId()))
result=[1]
def send():
    hwnd=int(ui.winId())
    if not win.foreground(hwnd):
        print('SKIPPED: test window is not foreground');app.quit();return
    # Override only the production process whitelist for this dedicated test.
    # The real foreground check and actual SendInput remain active throughout.
    with patch.object(win,'is_target',return_value=True):
        n=win.send_sequence(hwnd,['left','left','up','right','down'],lambda:True)
    def check():
        expected=[]
        for k in [QtCore.Qt.Key_Left,QtCore.Qt.Key_Left,QtCore.Qt.Key_Up,QtCore.Qt.Key_Right,QtCore.Qt.Key_Down]:expected += [('down',int(k)),('up',int(k))]
        result[0]=0 if n==5 and events==expected else 1
        print('PASS: native SendInput delivered 5 presses + 5 releases including repeated Left' if result[0]==0 else f'FAIL: {n=} {events=}')
        ui.close();app.quit()
    QtCore.QTimer.singleShot(150,check)
QtCore.QTimer.singleShot(500,send);QtCore.QTimer.singleShot(5000,app.quit);app.exec();raise SystemExit(result[0])
