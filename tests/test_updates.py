import tempfile
import random
import unittest
from pathlib import Path
from unittest.mock import patch,MagicMock
import numpy as np
from detector import RoundGate,Reading
from preferences import Preferences
import windows_io as win
# Import configures the matching PySide6 plugins before constructing a GUI.
import assist
from worker import Worker
from selection import SelectionOverlay
from PySide6 import QtCore,QtGui,QtWidgets,QtTest

class UpdatedGateTests(unittest.TestCase):
    def test_preview_does_not_consume_current_round_on_arm(self):
        gate=RoundGate();r=Reading(('left','right','up'),.9,True)
        for i in range(10):gate.update(r,i*.01,consume=False)
        self.assertFalse(gate.latched)
        self.assertEqual(gate.update(r,.2,consume=False),r.sequence)
        gate.commit(r.sequence,.2)
        self.assertFalse(gate.update(r,.21,consume=False))

    def test_distinct_fresh_round_without_blank_needs_three_frames(self):
        gate=RoundGate();a=Reading(('up',)*3,.9,True);b=Reading(('left','right','down'),.9,True)
        gate.commit(a.sequence,0)
        self.assertFalse(gate.update(b,.4,consume=False));self.assertFalse(gate.update(b,.41,consume=False))
        self.assertEqual(gate.update(b,.42,consume=False),b.sequence)

class CacheTests(unittest.TestCase):
    def test_persists_across_restart_and_uniform_resize(self):
        with tempfile.TemporaryDirectory() as tmp:
            file=Path(tmp)/'settings.json';prefs=Preferences(file)
            prefs.remember('game',(280,377,495,54),(1080,484))
            loaded=Preferences(file)
            self.assertEqual(loaded.restore('game',(1620,726)),(420,566,742,81))
            self.assertIsNone(loaded.restore('game',(1080,900)))
            loaded.forget('game');self.assertIsNone(Preferences(file).restore('game',(1080,484)))
    def test_corrupt_json_is_recoverable(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'settings.json';p.write_text('{bad',encoding='utf-8')
            self.assertEqual(Preferences(p).data,{})

class JitterTests(unittest.TestCase):
    def test_independent_bounded_variations(self):
        rng=random.Random(123)
        values=[win.timing_ms(30,10,rng) for _ in range(100)]
        self.assertTrue(all(20<=v<=40 for v in values));self.assertGreater(len(set(values)),90)
        self.assertEqual(win.timing_ms(30,0),30)
        self.assertTrue(all(win.timing_ms(5,40,rng)>=5 for _ in range(100)))

class WorkerTests(unittest.TestCase):
    def run_worker(self,cached=False):
        prefs=MagicMock();prefs.restore.return_value=(280,377,495,54) if cached else None
        w=Worker(1,None,30,30,.72,preferences=prefs,cache_key='game');calls=[0]
        # This test isolates preview/commit behavior, not the separately tested
        # real-time popup settling window.
        w.settle_seconds=0;metrics=[];w.telemetry.connect(metrics.append)
        reading=Reading(('left','right','up'),.9,True,((199,30),(249,30),(299,30)))
        def read(row):
            calls[0]+=1
            if calls[0]==3:w.armed=True
            if calls[0]==7:w.running=False
            return reading
        capture=MagicMock();capture.__enter__.return_value=capture;capture.grab.return_value=np.zeros((54,495,4),np.uint8)
        with patch('worker.mss.mss',return_value=capture),patch('worker.Detector') as detector,patch('worker.Locator') as locator,patch('worker.win.is_target',return_value=True),patch('worker.win.foreground',return_value=True),patch('worker.win.client_rect',return_value=(0,0,1080,484)),patch('worker.win.send_sequence',return_value=3) as send,patch('worker.time.sleep'):
            detector.return_value.read.side_effect=read;locator.return_value.locate.return_value=(280,377,495,54)
            w.run()
            self.assertEqual(send.call_count,1)
            self.assertEqual(locator.return_value.locate.call_count,0 if cached else 2)
            rounds=[e for e in metrics if e['kind']=='round'];self.assertEqual(len(rounds),1)
            self.assertEqual(rounds[0]['sequence'],['left','right','up'])
            self.assertEqual(sum(e['n'] for e in metrics if e['kind']=='timing'),7)
            self.assertEqual(next(e['round_id'] for e in metrics if e['kind']=='input'),rounds[0]['id'])
    def test_arm_after_preview_sends_current_row_once(self):self.run_worker()
    def test_cached_roi_skips_global_locator(self):self.run_worker(cached=True)

class SelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    def test_drag_shows_band_and_returns_physical_coordinates(self):
        screen=self.app.primaryScreen();image=QtGui.QPixmap(800,600);image.fill(QtGui.QColor('#29384a'))
        overlay=SelectionOverlay(screen,image,(-1000,0,1000,750));overlay.resize(800,600);overlay.show();self.app.processEvents()
        selections=[];overlay.selected.connect(selections.append)
        try:
            QtTest.QTest.mousePress(overlay,QtCore.Qt.LeftButton,QtCore.Qt.NoModifier,QtCore.QPoint(100,100))
            QtTest.QTest.mouseMove(overlay,QtCore.QPoint(500,350));self.app.processEvents()
            self.assertTrue(overlay.band.isVisible());self.assertGreater(overlay.band.width(),300)
            overlay.grab().save('analysis/selection_test.png')
            QtTest.QTest.mouseRelease(overlay,QtCore.Qt.LeftButton,QtCore.Qt.NoModifier,QtCore.QPoint(500,350))
            self.assertEqual(selections,[(-875,125,501,314)])
        finally:overlay.close()
    def test_mapping_negative_origin_and_150_percent(self):
        screen=self.app.primaryScreen();pix=QtGui.QPixmap(800,600)
        overlay=SelectionOverlay(screen,pix,(-1200,-900,1200,900));overlay.resize(800,600)
        self.assertEqual(overlay.physical_rect(QtCore.QRect(100,100,400,200)),(-1050,-750,600,300));overlay.close()

if __name__=='__main__':unittest.main()
