"""Compact, color-coded activity feed."""
from PySide6 import QtCore,QtGui,QtWidgets

class ActivityLog(QtWidgets.QTreeWidget):
    def __init__(self,parent=None):
        super().__init__(parent);self.setColumnCount(3);self.setHeaderLabels(['时间','状态','详情'])
        self.setRootIsDecorated(False);self.setAlternatingRowColors(True);self.setUniformRowHeights(True)
        self.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection);self.setIndentation(0)
        self.setColumnWidth(0,72);self.setColumnWidth(1,64)
        self.header().setSectionResizeMode(0,QtWidgets.QHeaderView.Fixed)
        self.header().setSectionResizeMode(1,QtWidgets.QHeaderView.Fixed)
        self.header().setSectionResizeMode(2,QtWidgets.QHeaderView.Stretch)
        self.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);self.setTextElideMode(QtCore.Qt.ElideRight)
        self.setStyleSheet('QTreeWidget{background:#131d2a;alternate-background-color:#172332;border:1px solid #324156;border-radius:6px;} QTreeWidget::item{height:27px;padding:1px 4px;} QHeaderView::section{background:#202e40;border:0;padding:5px;color:#93a4b9;} QTreeWidget::item:selected{background:#304763;}')

    def appendPlainText(self,text):
        timestamp,message=text.split('  ',1) if '  ' in text else ('',text)
        if any(k in message for k in ['仅确认','中断','失败','停止：']):tag,color='未确认','#ffbd75'
        elif '黄色反馈确认' in message:tag,color='已确认','#75e2b5'
        elif '已发送' in message:tag,color='已发送','#8abdfb'
        elif '预览识别' in message:tag,color='预览','#a5b2c3'
        elif any(k in message for k in ['定位','区域','记忆']):tag,color='定位','#bba5f8'
        else:tag,color='状态','#a5b2c3'
        bar=self.verticalScrollBar();follow=bar.value()>=bar.maximum()-4
        item=QtWidgets.QTreeWidgetItem([timestamp,tag,message]);item.setToolTip(2,message)
        item.setForeground(0,QtGui.QColor('#8192a8'));item.setForeground(1,QtGui.QColor(color))
        self.addTopLevelItem(item)
        while self.topLevelItemCount()>200:self.takeTopLevelItem(0)
        if follow:self.scrollToBottom()
