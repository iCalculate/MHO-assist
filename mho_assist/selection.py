"""Visible per-monitor rubber bands, with logical-to-physical coordinate mapping."""
from PySide6 import QtCore,QtGui,QtWidgets
from mho_assist import windows_io as win

class SelectionOverlay(QtWidgets.QDialog):
    selected=QtCore.Signal(tuple)
    cancelled=QtCore.Signal()
    def __init__(self,screen,pixmap,physical,lang='zh'):
        super().__init__();self.lang=lang;self.setWindowTitle('Select capture area' if lang=='en' else '框选识别范围')
        self.setWindowFlags(QtCore.Qt.Tool|QtCore.Qt.FramelessWindowHint|QtCore.Qt.WindowStaysOnTopHint)
        self.setGeometry(screen.geometry());self.setCursor(QtCore.Qt.CrossCursor);self.setMouseTracking(True)
        self.pixmap=pixmap;self.physical=physical;self.anchor=None;self.endpoint=None
        self.band=QtWidgets.QRubberBand(QtWidgets.QRubberBand.Rectangle,self)
        self.band.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents);self.setFocusPolicy(QtCore.Qt.StrongFocus)

    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.drawPixmap(self.rect(),self.pixmap);p.fillRect(self.rect(),QtGui.QColor(0,0,0,90))
        p.setPen(QtGui.QPen(QtGui.QColor('#6df0bb'),3))
        if self.anchor is not None and self.endpoint is not None:
            r=QtCore.QRect(self.anchor,self.endpoint).normalized();p.drawRect(r);p.fillRect(r,QtGui.QColor(80,240,180,30))
        p.setPen(QtCore.Qt.white);p.setFont(QtGui.QFont('Microsoft YaHei',13))
        p.drawText(24,40,'Drag around the full game view (buttons and arrow row). Esc / right-click to cancel.' if self.lang=='en' else '按住左键拖动框选完整游戏画面（含右侧四按钮和底部序列）；Esc / 右键取消')

    def mousePressEvent(self,event):
        if event.button()==QtCore.Qt.RightButton:self.cancelled.emit();return
        if event.button()!=QtCore.Qt.LeftButton:return
        self.anchor=event.position().toPoint();self.endpoint=self.anchor
        self.band.setGeometry(QtCore.QRect(self.anchor,self.endpoint));self.band.show();self.grabMouse();self.update()

    def mouseMoveEvent(self,event):
        if self.anchor is None:return
        self.endpoint=event.position().toPoint();self.band.setGeometry(QtCore.QRect(self.anchor,self.endpoint).normalized());self.update()

    def physical_rect(self,rect):
        x,y,w,h=self.physical;sx=w/self.width();sy=h/self.height()
        return (round(x+rect.x()*sx),round(y+rect.y()*sy),round(rect.width()*sx),round(rect.height()*sy))

    def mouseReleaseEvent(self,event):
        if event.button()!=QtCore.Qt.LeftButton or self.anchor is None:return
        self.releaseMouse();self.endpoint=event.position().toPoint()
        rect=QtCore.QRect(self.anchor,self.endpoint).normalized().intersected(self.rect());selection=self.physical_rect(rect)
        if selection[2]>=200 and selection[3]>=100:self.selected.emit(selection)
        else:self.anchor=None;self.endpoint=None;self.band.hide();self.update()

    def keyPressEvent(self,event):
        if event.key()==QtCore.Qt.Key_Escape:self.cancelled.emit()

class ScreenSelector:
    def __init__(self,lang='zh'):self.lang=lang;self.selection=None;self.overlays=[];self.loop=QtCore.QEventLoop()
    def finish(self,selection=None):
        self.selection=selection
        for overlay in self.overlays:overlay.releaseMouse();overlay.hide();overlay.close()
        self.loop.quit()
    def exec(self):
        monitors=win.monitors()
        for screen in QtWidgets.QApplication.screens():
            physical=monitors.get(screen.name().lower())
            if physical is None:
                g=screen.geometry();d=screen.devicePixelRatio();physical=(round(g.x()*d),round(g.y()*d),round(g.width()*d),round(g.height()*d))
            overlay=SelectionOverlay(screen,screen.grabWindow(0),physical,self.lang)
            overlay.selected.connect(self.finish);overlay.cancelled.connect(self.finish);self.overlays.append(overlay)
        for overlay in self.overlays:overlay.show();overlay.raise_()
        current=next((o for o in self.overlays if o.geometry().contains(QtGui.QCursor.pos())),self.overlays[0])
        current.activateWindow();current.setFocus();self.loop.exec();return self.selection
