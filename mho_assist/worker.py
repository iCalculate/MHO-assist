"""Capture/recognize/send loop. Preview never consumes a round."""
import time
import cv2
import mss
import numpy as np
import uuid
from PySide6 import QtCore
from mho_assist.detector import Detector,RoundGate,SYMBOLS,yellow_feedback
from mho_assist.locator import Locator
from mho_assist.tracking import RegionConfirmation,capture_row,moved
from mho_assist.history_store import TimingBatch,timestamp
from mho_assist.autofarm import AutoFarm,Page,PageDetector,NAMES
from mho_assist import windows_io as win

class Worker(QtCore.QThread):
    preview=QtCore.Signal(object,str)
    log=QtCore.Signal(str)
    region_found=QtCore.Signal(tuple,tuple)
    telemetry=QtCore.Signal(dict)
    cycle_state=QtCore.Signal(dict)

    def __init__(self,hwnd,screen_range,hold,gap,threshold,hold_jitter=4,gap_jitter=10,strict_feedback=False,preferences=None,cache_key=None,diagnostic_dir=None):
        super().__init__();self.hwnd=hwnd;self.screen_range=screen_range;self.hold=hold;self.gap=gap
        self.threshold=threshold;self.hold_jitter=hold_jitter;self.gap_jitter=gap_jitter
        self.strict_feedback=strict_feedback;self.preferences=preferences;self.cache_key=cache_key
        self.armed=False;self.running=True;self.reason='等待目标窗口进入前台'
        self.settle_seconds=.06;self.diagnostic_dir=diagnostic_dir
        self.run_id=uuid.uuid4().hex
        self.auto_repeat=False;self.cycle=AutoFarm();self._cycle_reader=None;self._cycle_next=0;self._cycle_snapshot=None;self._cycle_reset_gates=False

    def publish_cycle(self):
        for event in self.cycle.events:
            if event.get('action')=='restart_sequence_finished':self._cycle_reset_gates=True
            self.emit_metric(event)
        self.cycle.events=[]
        state=self.cycle.snapshot()
        if state!=self._cycle_snapshot:self.cycle_state.emit(state);self._cycle_snapshot=state

    def cycle_tick(self,capture,origin,size,region,now):
        if not self.auto_repeat or not self.armed:
            self.cycle.update(Page(),now,self.auto_repeat,self.armed);self.publish_cycle();return False
        if now<self._cycle_next:return self.cycle.state in ('navigation','change_wait','error')
        self._cycle_next=now+.2
        if self._cycle_reader is None:self._cycle_reader=PageDetector()
        x,y=origin;w,h=size
        full=np.asarray(capture.grab(dict(left=x,top=y,width=w,height=h)))[:,:,:3]
        page=self._cycle_reader.read(full,region)
        command=self.cycle.update(page,now,True,self.armed,win.foreground(self.hwnd))
        if command:
            self.reason=f'连续刷：发送 {NAMES[self.cycle.step]}'
            n=win.send_sequence(self.hwnd,[command],lambda:self.running and self.armed and self.auto_repeat,60,0)
            if n==1:self.cycle.sent(page,time.perf_counter())
            else:self.cycle.cancel('input_interrupted');self.armed=False
        if self.cycle.state=='error':
            self.armed=False;self.reason='连续刷暂停：页面未按预期改变，请检查映射或重新回到匹配页面'
            self.log.emit(self.reason)
            if self.diagnostic_dir and self.cycle.before is not None:
                from pathlib import Path
                folder=Path(self.diagnostic_dir)/'cycle';folder.mkdir(parents=True,exist_ok=True)
                cv2.imwrite(str(folder/'last_before.png'),self.cycle.before);cv2.imwrite(str(folder/'last_after.png'),page.frame)
        self.publish_cycle()
        return self.cycle.state in ('navigation','change_wait','error')

    def emit_metric(self,event):
        if event.get('kind')=='cycle':event.setdefault('id',uuid.uuid4().hex)
        event.setdefault('time',timestamp());self.telemetry.emit(event)

    def run(self):
        detector=Detector(self.threshold);gate=RoundGate(stable_seconds=self.settle_seconds);locator=Locator()
        observed=RoundGate(stable_seconds=self.settle_seconds);batch=TimingBatch();round_index=0;round_id=None;last_batch=time.perf_counter()
        lock=RegionConfirmation();repair=RegionConfirmation()
        last_ui=0;focus_state=None;located=None;dimensions=None;last_search=0;pending=None;previewed=()
        last_good=time.perf_counter();last_repair=0
        try:
            with mss.mss() as capture:
                while self.running:
                    started=time.perf_counter()
                    if not win.is_target(self.hwnd):raise RuntimeError('目标窗口已关闭，请重新选择')
                    focus=win.foreground(self.hwnd)
                    if focus!=focus_state:
                        self.log.emit('目标窗口前台：识别已恢复' if focus else '目标未在前台：暂停识别与输入')
                        if not focus and pending:
                            self.emit_metric(dict(kind='feedback',round_id=pending['round_id'],confirmed=sum(pending['confirmed']),outcome='unverified'));pending=None
                        focus_state=focus
                    if not focus:
                        navigating=self.cycle.state in ('navigation','change_wait','next_wait')
                        self.cycle.cancel('focus_lost');self.publish_cycle()
                        if navigating:self.armed=False
                        self.reason='目标不在前台，不发送按键';time.sleep(.025);continue
                    x,y,w,h=self.screen_range or win.client_rect(self.hwnd)
                    if w<200 or h<100:
                        self.reason='目标窗口太小';time.sleep(.1);continue
                    if (w,h)!=dimensions:
                        if pending:self.emit_metric(dict(kind='feedback',round_id=pending['round_id'],confirmed=sum(pending['confirmed']),outcome='unverified'))
                        located=self.preferences.restore(self.cache_key,(w,h)) if self.preferences else None
                        dimensions=(w,h);pending=None;gate.reset();observed.reset();round_id=None;previewed=();lock=RegionConfirmation();last_good=time.perf_counter()
                        if located:self.log.emit('已恢复记忆的高速识别区域，直接使用局部捕获；如错位请点重新定位')
                    if self.cycle_tick(capture,(x,y),(w,h),located,time.perf_counter()):
                        self.reason='连续刷：等待页面变化' if self.cycle.state!='error' else '连续刷暂停：页面未按预期改变，请检查映射或重新回到匹配页面'
                        time.sleep(.02);continue
                    if self._cycle_reset_gates:
                        gate.reset();observed.reset();round_id=None;previewed=();self._cycle_reset_gates=False
                    if located is None:
                        self.reason='正在自动定位箭头区域'
                        if time.perf_counter()-last_search<.15:time.sleep(.025);continue
                        full=np.asarray(capture.grab(dict(left=x,top=y,width=w,height=h)))[:,:,:3]
                        candidate=locator.locate(full);located=lock.offer(candidate);last_search=time.perf_counter()
                        if located:
                            self.log.emit('定位连续确认通过，已锁定高速区域')
                        else:
                            self.reason='定位动画中，等待区域稳定' if candidate else '正在寻找四方向按钮'
                            self.preview.emit(cv2.resize(full,(495,max(54,round(full.shape[0]*495/full.shape[1])))).copy(),self.reason);continue
                    row=capture_row(capture,(x,y),located,(w,h))
                    t=time.perf_counter();reading=detector.read(row);elapsed=(time.perf_counter()-t)*1000
                    batch.add(elapsed)
                    now=time.perf_counter();sequence=gate.update(reading,now,consume=False)
                    if observed.update(reading,now):
                        round_index+=1;round_id=f'{self.run_id}:r{round_index}'
                        self.emit_metric(dict(kind='round',id=round_id,sequence=list(reading.sequence),confidence=reading.confidence,ms=elapsed))
                        self.cycle.group(round_id,len(reading.sequence),now);self.publish_cycle()
                    if now-last_batch>=.5:
                        item=batch.flush(self.run_id)
                        if item:self.emit_metric(item)
                        last_batch=now
                    if reading.sequence:last_good=now
                    # A stale cache must not permanently strand the recognizer.
                    # No blind invalidation in blank scenes: adopt only a new,
                    # independently confirmed four-button geometry.
                    if now-last_good>1.5 and now-last_repair>3.0 and not pending:
                        full=np.asarray(capture.grab(dict(left=x,top=y,width=w,height=h)))[:,:,:3]
                        candidate=locator.locate(full);confirmed=repair.offer(candidate)
                        last_repair=time.perf_counter()
                        if candidate and moved(located,candidate) and not confirmed:last_repair-=2.7
                        if confirmed and moved(located,confirmed):
                            located=confirmed;lock=RegionConfirmation();repair=RegionConfirmation()
                            last_good=now;self.log.emit('检测到区域失配，已自动恢复定位；等待稳定序列确认');continue
                    if not reading.sequence:previewed=()
                    if pending:
                        flags=yellow_feedback(row,pending['centers'])
                        pending['confirmed']=[a or b for a,b in zip(pending['confirmed'],flags)]
                        count=sum(pending['confirmed'])
                        if count==len(flags):
                            self.emit_metric(dict(kind='feedback',round_id=pending['round_id'],confirmed=count,outcome='confirmed'))
                            self.log.emit(f'黄色反馈确认：{count}/{len(flags)} 个箭头已接受');pending=None
                        elif now-pending['time']>1.5:
                            self.emit_metric(dict(kind='feedback',round_id=pending['round_id'],confirmed=count,outcome='unconfirmed'))
                            action='已暂停，请按 F8 恢复' if self.strict_feedback else '本轮不重复发送，继续等待下一轮'
                            self.log.emit(f'Windows 已发送，但黄色反馈仅确认 {count}/{len(flags)}；{action}')
                            if self.diagnostic_dir:
                                from diagnostics import save_unconfirmed
                                try:
                                    path=save_unconfirmed(self.diagnostic_dir,pending,row,located,self.hwnd)
                                    self.log.emit(f'未确认现场已保存：{path.name}')
                                except OSError as exc:self.log.emit(f'现场保存失败：{exc}')
                            if self.strict_feedback:self.armed=False
                            pending=None
                    self.reason=gate.reason
                    if not self.armed:self.reason='自动输入未启用，按 F8；预览不会消耗本轮'
                    elif pending:self.reason=f"等待黄色反馈 {sum(pending['confirmed'])}/{len(pending['confirmed'])}"
                    if sequence:
                        if self.preferences:
                            # Persist only after an entire row is spatially stable,
                            # never the first animated control-button estimate.
                            if getattr(self,'saved_region',None)!=tuple(located):
                                self.region_found.emit(tuple(located),(w,h));self.saved_region=tuple(located)
                        text=' '.join(SYMBOLS[k] for k in sequence)
                        if self.armed and not pending:
                            n=win.send_sequence(self.hwnd,sequence,lambda:self.running and self.armed,self.hold,self.gap,self.hold_jitter,self.gap_jitter)
                            self.emit_metric(dict(kind='input',round_id=round_id,sent=n,total=len(sequence),outcome='sent' if n==len(sequence) else 'interrupted'))
                            if n:gate.commit(sequence,time.perf_counter())
                            self.log.emit(f'系统已发送 {text}  ({n}/{len(sequence)} 键，置信度 {reading.confidence:.0%})')
                            if n!=len(sequence):
                                self.armed=False;self.reason='发送中断，按 F8 恢复';self.log.emit('输入中断，已暂停；已发送部分不会重复')
                            else:
                                pending=dict(round_id=round_id,centers=reading.centers,confirmed=[False]*len(sequence),time=time.perf_counter(),sequence=sequence,before=row.copy(),hold=self.hold,gap=self.gap)
                                self.reason='已发送本轮，等待黄色反馈'
                        elif not self.armed and previewed!=sequence:
                            self.log.emit(f'预览识别 {text}；未启用自动输入，按 F8 后可输入当前轮');previewed=sequence
                    now=time.perf_counter()
                    if now-last_ui>.1:
                        symbols=' '.join(SYMBOLS[k] for k in reading.sequence) or '等待完整序列'
                        self.preview.emit(row.copy(),f'{symbols}   置信度 {reading.confidence:.0%}   识别 {elapsed:.1f} ms')
                        last_ui=now
                    time.sleep(max(0,.0083-(time.perf_counter()-started)))
        except Exception as exc:
            self.armed=False;self.reason=f'停止：{exc}';self.log.emit(self.reason)
        finally:
            item=batch.flush(self.run_id)
            if item:self.emit_metric(item)
            if pending:self.emit_metric(dict(kind='feedback',round_id=pending['round_id'],confirmed=sum(pending['confirmed']),outcome='unverified'))
