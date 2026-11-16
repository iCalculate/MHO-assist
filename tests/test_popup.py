import unittest
from unittest.mock import MagicMock,patch
import cv2
import numpy as np
from detector import Detector,Reading,RoundGate
from tracking import RegionConfirmation,guarded_rect
import assist
from worker import Worker

class PopupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frame=cv2.imread('tests/fixtures/frame_36.png');cls.detector=Detector()

    def test_original_small_offset_failures_now_recognized(self):
        for dx,dy in [(8,0),(-8,0),(0,12),(12,10)]:
            with self.subTest(dx=dx,dy=dy):
                r=self.detector.read(self.frame[377+dy:431+dy,280+dx:775+dx])
                self.assertEqual(r.sequence,('down','left','up'))

    def test_guard_band_handles_both_horizontal_and_vertical_drift(self):
        for dx in [-24,-16,-8,0,8,16,24]:
            for dy in [-18,-9,0,9,18]:
                with self.subTest(dx=dx,dy=dy):
                    (x,y,w,h),size=guarded_rect((280+dx,377+dy,495,54),(1080,484))
                    r=self.detector.read(cv2.resize(self.frame[y:y+h,x:x+w],size))
                    self.assertEqual(r.sequence,('down','left','up'))

    def test_bad_popup_locator_result_is_not_committed(self):
        lock=RegionConfirmation()
        self.assertIsNone(lock.offer((340,375,447,49)))
        self.assertIsNone(lock.offer((283,377,493,54)))
        self.assertEqual(lock.offer((283,377,493,54)),(283,377,493,54))

    def test_gold_prefix_cannot_become_shorter_new_round(self):
        frame=cv2.imread('tests/fixtures/frame_151.png')
        r=self.detector.read(frame[353:455,256:799]);self.assertFalse(r.sequence)
        gate=RoundGate();original=('down','up','down','up','left');gate.commit(original,0)
        suffix=Reading(original[1:],.95,True)
        for i in range(20):self.assertFalse(gate.update(suffix,.5+i*.05,consume=False))

    def test_identical_directions_still_wait_for_motion_to_settle(self):
        gate=RoundGate(stable_seconds=.06)
        def r(y):return Reading(('down','left','up'),.95,True,((200,y),(248,y),(298,y)))
        self.assertFalse(gate.update(r(45),0,consume=False))
        self.assertFalse(gate.update(r(38),.02,consume=False))
        self.assertFalse(gate.update(r(32),.04,consume=False))
        self.assertFalse(gate.update(r(32),.07,consume=False))
        self.assertEqual(gate.update(r(32),.11,consume=False),r(32).sequence)

    def test_real_video_popup_centers_and_gate(self):
        gate=RoundGate(stable_seconds=.06)
        event=None
        with np.load('tests/fixtures/popup.npz') as clip:
            for sec,row in zip(clip['seconds'],clip['rows']):
                sequence=gate.update(self.detector.read(row),sec)
                if sequence:
                    event=(sec,sequence)
                    break
        self.assertIsNotNone(event)
        self.assertGreater(event[0],35.70);self.assertLess(event[0],36.)
        self.assertEqual(event[1],('down','left','up'))

    def test_wrong_cached_rectangle_recovers_and_can_send(self):
        prefs=MagicMock();prefs.restore.return_value=(340,375,447,49)
        w=Worker(1,None,30,30,.72,preferences=prefs,cache_key='stale');w.armed=True;w.settle_seconds=0
        ticks=[0.];grabs=[0]
        def clock():ticks[0]+=.025;return ticks[0]
        def grab(box):
            grabs[0]+=1
            if grabs[0]>150:w.running=False
            return self.frame[box['top']:box['top']+box['height'],box['left']:box['left']+box['width']]
        cap=MagicMock();cap.__enter__.return_value=cap;cap.grab.side_effect=grab
        def sent(*args):w.running=False;return len(args[1])
        with patch('worker.mss.mss',return_value=cap),patch('worker.Locator') as locator,patch('worker.win.is_target',return_value=True),patch('worker.win.foreground',return_value=True),patch('worker.win.client_rect',return_value=(0,0,1080,484)),patch('worker.win.send_sequence',side_effect=sent) as send,patch('worker.time.perf_counter',side_effect=clock),patch('worker.time.sleep'):
            locator.return_value.locate.return_value=(283,377,493,54);w.run()
            self.assertEqual(send.call_count,1);self.assertGreaterEqual(locator.return_value.locate.call_count,2)

if __name__=='__main__':unittest.main()
