# -*- coding: utf-8 -*-
import os
import sys
import time
from pathlib import Path
from PySide6 import QtCore,QtGui,QtWidgets
from worker import Worker
from preferences import Preferences
from selection import ScreenSelector
from history_store import HistoryStore
from dashboard import Dashboard
from language import tr,reason
from game_bridge import GameBridge
from cycle_widgets import RepeatSwitch,PhaseSteps
from autofarm import NAMES
import windows_io as win

ROOT=Path(__file__).resolve().parent
APP_DIR=Path(sys.executable).parent if getattr(sys,'frozen',False) else ROOT

def data_directory():
    if getattr(sys,'frozen',False) and APP_DIR.parent.name.startswith('release'):
        return APP_DIR.parent.parent/'data'
    return (APP_DIR if getattr(sys,'frozen',False) else ROOT)/'data'

class Main(QtWidgets.QWidget):
    def __init__(self,data_dir=None,smoke=False,language=None):
        super().__init__();self.resize(800,650);self.smoke=smoke;self.worker=None;self.screen_range=None;self.range_hwnd=None
        self.preferences=Preferences(APP_DIR/'settings.json');self.lang=language or self.preferences.data.get('language','zh')
        self.store=HistoryStore(data_dir or data_directory(),self.lang);self.bridge=GameBridge()
        self.last_preview='';self.notice_text='';self.notice_until=0;self.bridge_until=0;self._closed=False;self._dirty=True;self._widgets=[]
        self._cycle_state={'phase':0,'counts':[0,0,0],'state':'playing','step':0,'games':0,'error':''}
        values=self.preferences.data.get('options',{})
        layout=QtWidgets.QVBoxLayout(self);layout.setContentsMargins(12,10,12,10);layout.setSpacing(6)
        header=QtWidgets.QHBoxLayout();self.title=QtWidgets.QLabel();self.title.setStyleSheet('font-size:20px;font-weight:600');header.addWidget(self.title)
        self.demo=QtWidgets.QLabel('DEMO');self.demo.setStyleSheet('color:#ffcb72;font-size:11px');self.demo.setVisible(smoke);header.addWidget(self.demo)
        header.addStretch();self.hotkeys=QtWidgets.QLabel();self.hotkeys.setStyleSheet('color:#93a7c0;font-size:11px');header.addWidget(self.hotkeys)
        self.languages=QtWidgets.QComboBox();self.languages.addItem('中文','zh');self.languages.addItem('English','en');self.languages.setCurrentIndex(self.lang=='en');self.languages.currentIndexChanged.connect(self.change_language);header.addWidget(self.languages);layout.addLayout(header)
        bar=QtWidgets.QHBoxLayout();self.combo=QtWidgets.QComboBox();bar.addWidget(self.combo,1)
        bar.addWidget(self.button('refresh',self.refresh));bar.addWidget(self.button('select',self.select))
        self.tools=QtWidgets.QToolButton();self.tools.setPopupMode(QtWidgets.QToolButton.MenuButtonPopup);self.tools.clicked.connect(self.connect_mirror)
        menu=QtWidgets.QMenu(self.tools);self.tool_actions=[]
        for key,action in [('qt_open',self.connect_mirror),('game_focus',self.focus_game),('recover',lambda:self.launch_tool('recover')),('editor',lambda:self.launch_tool('editor')),('touches',lambda:self.launch_tool('touches')),('folder',self.open_tools_folder)]:
            item=menu.addAction('');item.triggered.connect(action);self.tool_actions.append((item,key))
        self.tools.setMenu(menu);bar.addWidget(self.tools);layout.addLayout(bar)
        actions=QtWidgets.QHBoxLayout()
        for key,method in [('start',self.start),('window',self.auto),('relocate',self.relocate),('pause',self.pause)]:actions.addWidget(self.button(key,method))
        settings=self.button('settings',None);settings.setCheckable(True);actions.addWidget(settings);layout.addLayout(actions)
        cycle_row=QtWidgets.QHBoxLayout();cycle_row.setSpacing(7)
        cycle_row.addWidget(self.label('repeat'));self.repeat=RepeatSwitch();self.repeat.setChecked(bool(self.preferences.data.get('auto_repeat',False)))
        self.repeat.toggled.connect(self.repeat_toggled);cycle_row.addWidget(self.repeat)
        self.steps=PhaseSteps();cycle_row.addWidget(self.steps,1);self.games=QtWidgets.QLabel();self.games.setStyleSheet('font-size:10px;color:#92a9c3');cycle_row.addWidget(self.games);layout.addLayout(cycle_row)
        self.advanced=QtWidgets.QGroupBox();self.advanced.hide();settings.toggled.connect(self.advanced.setVisible);grid=QtWidgets.QGridLayout(self.advanced);grid.setContentsMargins(10,8,10,8)
        def spin(key,default,low,high):
            widget=QtWidgets.QSpinBox();widget.setRange(low,high);widget.setValue(values.get(key,default));widget.setSuffix(' ms');return widget
        self.hold=spin('hold',30,5,200);self.gap=spin('gap',30,5,200)
        self.hold_jitter=spin('hold_jitter',4,0,80);self.gap_jitter=spin('gap_jitter',10,0,80)
        self.threshold=QtWidgets.QDoubleSpinBox();self.threshold.setRange(.6,.95);self.threshold.setSingleStep(.01);self.threshold.setValue(values.get('threshold',.72))
        for col,(key,widget) in enumerate([('hold',self.hold),('gap',self.gap),('threshold',self.threshold)]):grid.addWidget(self.label(key),0,col*2);grid.addWidget(widget,0,col*2+1)
        for col,(key,widget) in enumerate([('hold_jitter',self.hold_jitter),('gap_jitter',self.gap_jitter)]):grid.addWidget(self.label(key),1,col*2);grid.addWidget(widget,1,col*2+1)
        self.strict=QtWidgets.QCheckBox();self.strict.setChecked(values.get('strict_feedback',False));self._widgets.append((self.strict,'strict'));grid.addWidget(self.strict,1,4,1,2)
        self.mapping=QtWidgets.QLabel();self.mapping.setStyleSheet('font-size:11px;color:#91a5bf');grid.addWidget(self.mapping,2,0,1,6);layout.addWidget(self.advanced)
        self.status=QtWidgets.QLabel();self.status.setWordWrap(True);self.status.setStyleSheet('font-size:12px;color:#ffcb72');layout.addWidget(self.status)
        self.picture=QtWidgets.QLabel();self.picture.setFixedHeight(70);self.picture.setAlignment(QtCore.Qt.AlignCenter);self.picture.setStyleSheet('background:#101824;border:1px solid #304055;border-radius:6px;color:#8496ae');layout.addWidget(self.picture)
        self.reading=QtWidgets.QLabel('—');self.reading.setStyleSheet('font-size:11px;color:#9fb3cc');layout.addWidget(self.reading)
        scope=QtWidgets.QHBoxLayout();self.scope=QtWidgets.QComboBox();self.scope.addItem('');self.scope.addItem('');self.scope.currentIndexChanged.connect(self.refresh_stats);scope.addWidget(self.scope)
        self.scope_note=QtWidgets.QLabel();self.scope_note.setStyleSheet('font-size:11px;color:#8196ae');scope.addWidget(self.scope_note,1)
        scope.addWidget(self.button('logs',self.open_logs));layout.addLayout(scope)
        self.dashboard=Dashboard();layout.addWidget(self.dashboard,1)
        self.footer=QtWidgets.QLabel();self.footer.setWordWrap(True);self.footer.setStyleSheet('font-size:10px;color:#8196ae');layout.addWidget(self.footer)
        self.retranslate();self.refresh();self.refresh_stats()
        self.key_states={vk:bool(win.user.GetAsyncKeyState(vk)&0x8000) for vk in [0x77,0x78,0x79]}
        self.keys=QtCore.QTimer(self);self.keys.timeout.connect(self.poll_keys);self.keys.start(10)
        self.timer=QtCore.QTimer(self);self.timer.timeout.connect(self.update_status);self.timer.start(100)
        self.statistics=QtCore.QTimer(self);self.statistics.timeout.connect(self.periodic);self.statistics.start(1000)
        self._tick=0

    def button(self,key,method):
        b=QtWidgets.QPushButton();self._widgets.append((b,key))
        if method:b.clicked.connect(method)
        return b
    def label(self,key):
        label=QtWidgets.QLabel();self._widgets.append((label,key));return label
    def retranslate(self):
        self.setWindowTitle(tr('title',self.lang));self.title.setText(tr('title',self.lang));self.hotkeys.setText(tr('hotkeys',self.lang))
        for widget,key in self._widgets:widget.setText(tr(key,self.lang))
        self.tools.setText(tr('tools',self.lang));self.advanced.setTitle(tr('advanced',self.lang))
        for action,key in self.tool_actions:action.setText(tr(key,self.lang))
        self.scope.setItemText(0,tr('session',self.lang));self.scope.setItemText(1,tr('history',self.lang))
        self.scope_note.setText(tr('history_start',self.lang));self.footer.setText(tr('scope_hint',self.lang))
        self.mapping.setText(tr('mapping',self.lang,key=self.bridge.mapping_key()))
        self.tools.setToolTip(self.mapping.text())
        self.repeat.setToolTip(tr('repeat_tip',self.lang));self.repeat.setAccessibleName(tr('repeat',self.lang));self.on_cycle(self._cycle_state)
        if self.picture.pixmap() is None or self.picture.pixmap().isNull():self.picture.setText(tr('preview',self.lang))
        self.format_preview();self.update_status()
    def change_language(self):
        self.lang=self.languages.currentData();self.preferences.data['language']=self.lang
        if not self.smoke:self.preferences.save()
        self.retranslate();self.refresh_stats()
        if self.combo.currentData() is None:self.combo.setItemText(0,tr('choose',self.lang))
    def log(self,text):
        try:self.store.write_log(dict(kind='message',message=text))
        except OSError:pass
        self.notice_text=text;self.notice_until=time.monotonic()+3
    def notice(self,text):self.notice_text=text;self.notice_until=time.monotonic()+5
    def on_metric(self,event):
        try:self.store.record(event);self._dirty=True
        except Exception as exc:self.log(f'统计保存失败：{exc}')
    def refresh_stats(self,*args):
        if self._closed:return
        session=self.store.snapshot();history=self.store.snapshot(True)
        self.dashboard.set_data(history if self.scope.currentIndex() else session,self.lang);self._dirty=False
        self.scope_note.setText(f"本次 +{session['rounds']} 轮 / {session['keys']} 键 · 累计 {history['rounds']} 轮 / {history['keys']} 键" if self.lang=='zh' else f"Session +{session['rounds']}r / {session['keys']}k · All {history['rounds']}r / {history['keys']}k")
        self.scope_note.setToolTip(tr('history_start',self.lang))
    def periodic(self):
        self._tick+=1
        if self._dirty:self.refresh_stats()
        if self._tick%5==0:
            try:self.store.save_summary()
            except OSError as exc:self.log(f'统计保存失败：{exc}')
        if self.bridge_until>time.monotonic():self.bind_mirror()
    def poll_keys(self):
        for vk,action in [(0x78,self.pause),(0x77,self.toggle),(0x79,self.focus_game)]:
            down=bool(win.user.GetAsyncKeyState(vk)&0x8000)
            if down and not self.key_states[vk]:action()
            self.key_states[vk]=down
    def refresh(self):
        previous=self.combo.currentData();last=self.preferences.data.get('last_source',{});match=None
        self.combo.clear();self.combo.addItem(tr('choose',self.lang),None)
        for h,title in win.windows():
            if title.startswith('Codex Computer Use') or h==int(self.winId()):continue
            self.combo.addItem(title,h)
            if h==previous:match=self.combo.count()-1
            elif previous is None and title==last.get('title') and win.process_path(h).lower()==last.get('process','').lower():match=self.combo.count()-1
        if match is not None:
            self.combo.setCurrentIndex(match)
            if previous is None and last.get('screen_range'):self.screen_range=tuple(last['screen_range']);self.range_hwnd=self.combo.currentData()
    def source_key(self,h):return self.preferences.key(win.process_path(h),win.title(h),self.screen_range)
    def save_options(self,h=None):
        self.preferences.data['language']=self.lang
        self.preferences.data['auto_repeat']=self.repeat.isChecked()
        self.preferences.data['options']=dict(hold=self.hold.value(),gap=self.gap.value(),threshold=self.threshold.value(),hold_jitter=self.hold_jitter.value(),gap_jitter=self.gap_jitter.value(),strict_feedback=self.strict.isChecked())
        if h:self.preferences.data['last_source']=dict(process=win.process_path(h),title=win.title(h),screen_range=self.screen_range)
        if not self.smoke:self.preferences.save()
    def remember(self,key,roi,size):
        try:self.preferences.remember(key,roi,size);self.log('高速区域位置已记忆')
        except OSError as exc:self.log(f'区域记忆保存失败：{exc}')
    def stop(self):
        if self.worker:
            old=self.worker;old.armed=False;old.running=False;old.wait();self.worker=None
            # Drain final timing/feedback signals before closing the database.
            QtWidgets.QApplication.processEvents()
    def start(self):
        h=self.range_hwnd if self.screen_range else self.combo.currentData()
        if not h or not win.is_target(h):self.notice(reason('请选择目标窗口或重新框选范围',self.lang));return
        if h==int(self.winId()):self.notice(reason('不能监测助手自己的窗口，请重新框选游戏',self.lang));return
        self.stop();self.save_options(h);key=self.source_key(h)
        self.worker=Worker(h,self.screen_range,self.hold.value(),self.gap.value(),self.threshold.value(),self.hold_jitter.value(),self.gap_jitter.value(),self.strict.isChecked(),self.preferences,key,diagnostic_dir=self.store.folder/'diagnostics')
        self.worker.auto_repeat=self.repeat.isChecked();self.worker.cycle_state.connect(self.on_cycle)
        self.worker.region_found.connect(lambda roi,size,k=key:self.remember(k,roi,size))
        self.worker.log.connect(self.log);self.worker.preview.connect(self.show_preview);self.worker.telemetry.connect(self.on_metric);self.worker.start()
        self.log('预览已启动。切回目标窗口，确认方向键可用，再按 F8。')
    def show_preview(self,row,text):
        image=QtGui.QImage(row.data,row.shape[1],row.shape[0],row.strides[0],QtGui.QImage.Format_BGR888).copy()
        self.picture.setPixmap(QtGui.QPixmap.fromImage(image).scaled(self.picture.width()-8,self.picture.height()-6,QtCore.Qt.KeepAspectRatio,QtCore.Qt.SmoothTransformation))
        self.last_preview=text;self.format_preview()
    def format_preview(self):
        text=self.last_preview
        if self.lang=='en':
            text=text.replace('置信度','Confidence').replace('识别 ','Detect ').replace('等待完整序列','Waiting for sequence')
            if any('\u4e00'<=c<='\u9fff' for c in text):text=reason(self.last_preview,self.lang)
        self.reading.setText(text or '—')
    def toggle(self):
        if not self.worker or not self.worker.isRunning():self.notice(reason('请先启动预览',self.lang));return
        if not self.worker.armed and not win.foreground(self.worker.hwnd):self.notice(reason('请先切回目标窗口，再按 F8',self.lang));return
        self.worker.armed=not self.worker.armed;self.log('自动输入已启用' if self.worker.armed else '已暂停输入')
    def pause(self):
        if self.worker:self.worker.armed=False
        self.log('已暂停输入')
    def update_status(self):
        active=bool(self.worker and self.worker.isRunning() and self.worker.armed)
        text=reason(self.worker.reason,self.lang) if self.worker else tr('idle',self.lang)
        cycle=self._cycle_state
        if self.repeat.isChecked() and self.worker:
            state=cycle.get('state','playing')
            if state in ('navigation','change_wait'):text=tr('cycle_'+state,self.lang,key=NAMES[min(3,cycle.get('step',0))])
            if state=='change_wait' and cycle.get('step',0)>0:text+=f" · {cycle.get('change',0):.0%}"
            elif state in ('next_wait','error'):text=tr('cycle_'+state,self.lang)
        if not self.worker and self.notice_until>time.monotonic():text=reason(self.notice_text,self.lang) if self.lang=='en' and any('\u4e00'<=c<='\u9fff' for c in self.notice_text) else self.notice_text
        self.status.setText(tr('enabled' if active else 'paused',self.lang)+' · '+text)
        self.status.setStyleSheet('font-size:12px;color:'+('#75e2b5' if active else '#ffcb72'))
    def auto(self):self.stop();self.screen_range=None;self.range_hwnd=None
    def select(self):
        self.stop();self.hide();QtWidgets.QApplication.processEvents();QtCore.QTimer.singleShot(180,self.select_after_hide)
    def select_after_hide(self):
        self.selector=ScreenSelector(self.lang);selection=self.selector.exec()
        if selection:QtCore.QTimer.singleShot(120,lambda r=selection:self.finish_selection(r))
        else:self.show()
    def finish_selection(self,selection):
        a,b,c,d=selection;h=win.window_at(a+c//2,b+d//2)
        if h and h!=int(self.winId()):self.screen_range=selection;self.range_hwnd=h;self.save_options(h)
        else:self.notice(reason('框选中心未找到有效目标窗口，请重新框选',self.lang))
        self.show()
    def relocate(self):
        h=self.range_hwnd if self.screen_range else self.combo.currentData()
        if not h:self.notice(reason('请先选择目标窗口',self.lang));return
        self.stop();self.preferences.forget(self.source_key(h));self.start()
    def repeat_toggled(self):
        if self.worker:self.worker.auto_repeat=self.repeat.isChecked()
        self.preferences.data['auto_repeat']=self.repeat.isChecked()
        if not self.smoke:self.preferences.save()
    def on_cycle(self,state):
        self._cycle_state=dict(state);self.steps.set_state(state,self.lang)
        self.steps.setToolTip(tr('cycle_playing',self.lang))
        self.games.setText(tr('cycle_games',self.lang,n=state.get('games',0) if self.smoke else self.store.snapshot()['completed_games']))
    def open_logs(self):QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(self.store.logs)))
    def open_tools_folder(self):QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(self.bridge.folder)))
    def launch_tool(self,key):
        self.pause()
        if key in ('recover','touches'):self.stop()
        try:self.bridge.open_tool(key);self.store.write_log(dict(kind='tool',tool=key))
        except OSError as exc:self.notice(tr('tool_error',self.lang,path=str(exc)))
    def connect_mirror(self):
        self.bridge_until=time.monotonic()+60
        if self.bind_mirror():return
        try:self.bridge.open_tool('mirror');self.notice(tr('waiting_phone',self.lang))
        except OSError as exc:self.notice(tr('tool_error',self.lang,path=str(exc)))
    def bind_mirror(self):
        mirrors=self.bridge.mirrors()
        if len(mirrors)!=1:
            if len(mirrors)>1:self.notice(tr('multiple',self.lang));self.bridge_until=0
            return False
        h,title=mirrors[0];self.refresh();index=self.combo.findData(h)
        if index<0:return False
        self.combo.setCurrentIndex(index);self.screen_range=None;self.range_hwnd=None;self.bridge_until=0
        self.start();self.bridge.focus(h);self.notice(tr('binding',self.lang));return True
    def focus_game(self):
        h=self.worker.hwnd if self.worker else self.range_hwnd if self.screen_range else self.combo.currentData()
        if h:self.bridge.focus(h)
    def closeEvent(self,event):
        self.keys.stop();self.timer.stop();self.statistics.stop();self.bridge_until=0;self.stop()
        try:self.save_options(self.range_hwnd if self.screen_range else self.combo.currentData())
        finally:self.store.close();self._closed=True
        super().closeEvent(event)
