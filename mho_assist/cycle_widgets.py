# -*- coding: utf-8 -*-
from PySide6 import QtCore,QtGui,QtWidgets

class RepeatSwitch(QtWidgets.QAbstractButton):
    def __init__(self):
        super().__init__();self.setCheckable(True);self.setFixedSize(40,22);self.setCursor(QtCore.Qt.PointingHandCursor);self.setFocusPolicy(QtCore.Qt.StrongFocus)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing);p.setPen(QtCore.Qt.NoPen)
        p.setBrush(QtGui.QColor('#59bea0' if self.isChecked() else '#48586d'));p.drawRoundedRect(QtCore.QRectF(1,2,38,18),9,9)
        p.setBrush(QtGui.QColor('#f1f7ff'));p.drawEllipse(QtCore.QRectF(22 if self.isChecked() else 4,5,12,12))

class PhaseSteps(QtWidgets.QWidget):
    def __init__(self):
        super().__init__();self.phase=0;self.counts=[0,0,0];self.lang='zh';self.setMinimumHeight(28)
    def set_state(self,state,lang):
        self.phase=state.get('phase',0);self.counts=state.get('counts',[0,0,0]);self.lang=lang;self.update()
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing);p.setFont(QtGui.QFont('Microsoft YaHei',9))
        pitch=self.width()/3
        for i in range(3):
            done=self.counts[i]>=10 or self.phase>i+1 and self.counts[i]>0
            active=self.phase==i+1
            r=QtCore.QRectF(i*pitch+2,2,pitch-7,self.height()-4)
            p.setPen(QtGui.QColor('#5abfa1' if done else '#83b4f7' if active else '#45556d'))
            p.setBrush(QtGui.QColor('#203b37' if done else '#26384d' if active else '#202c3b'));p.drawRoundedRect(r,5,5)
            marker='✓' if done else '●' if active else '○';title=f'第{i+1}轮' if self.lang=='zh' else f'Phase {i+1}'
            text=f'{marker}  {title}  {self.counts[i]}/10'
            p.setPen(QtGui.QColor('#83dfbb' if done else '#91c6ff' if active else '#8499b3'))
            p.drawText(r,QtCore.Qt.AlignCenter,text)
