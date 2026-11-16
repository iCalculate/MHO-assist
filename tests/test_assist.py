import ctypes
import unittest
from unittest.mock import patch
import cv2
from detector import Detector,Reading,RoundGate,video_row,yellow_feedback
from locator import Locator
import numpy as np
import windows_io as win

class GateTests(unittest.TestCase):
    def test_stability_feedback_and_identical_next_round(self):
        gate=RoundGate();r=Reading(('left','left','up'),.9,True)
        self.assertFalse(gate.update(r,0))
        self.assertEqual(gate.update(r,.01),r.sequence)
        for i in range(30):self.assertFalse(gate.update(Reading(visible=True),.02+i*.01))
        self.assertFalse(gate.update(r,.4))
        self.assertFalse(gate.update(r,.41))
        gate.update(Reading(),.5);gate.update(Reading(),.7)
        gate.update(r,.71);self.assertEqual(gate.update(r,.72),r.sequence)

    def test_short_blank_does_not_rearm(self):
        gate=RoundGate();r=Reading(('up',)*3,.9,True)
        gate.update(r,0);gate.update(r,.01);gate.update(Reading(),.02)
        gate.update(r,.1);self.assertFalse(gate.update(r,.11))

class VideoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.d=Detector()
    def test_manually_read_reference_frames(self):
        # Ground truth transcribed by looking at the original video screenshots.
        samples={36:('down','left','up'),50:('right','up','down','up','right')}
        for sec,expected in samples.items():
            f=cv2.imread(f'tests/fixtures/frame_{sec}.png')
            self.assertIsNotNone(f)
            self.assertEqual(self.d.read(video_row(f)).sequence,expected)
    def test_no_sequence_on_scene_only(self):
        f=cv2.imread('tests/fixtures/frame_35.png');self.assertFalse(self.d.read(video_row(f)).sequence)
    def test_gold_feedback_is_visible_but_not_fresh(self):
        r=self.d.read(video_row(cv2.imread('tests/fixtures/frame_43.png')))
        self.assertTrue(r.visible);self.assertFalse(r.sequence)
    def test_resize(self):
        f=cv2.imread('tests/fixtures/frame_50.png');f=cv2.resize(f,(1620,726))
        self.assertEqual(self.d.read(video_row(f)).sequence,('right','up','down','up','right'))
    def test_yellow_confirmation_and_original_orange_up(self):
        fresh=self.d.read(video_row(cv2.imread('tests/fixtures/frame_50.png')))
        self.assertEqual(yellow_feedback(video_row(cv2.imread('tests/fixtures/frame_50.png')),fresh.centers),(False,)*5)
        self.assertEqual(yellow_feedback(video_row(cv2.imread('tests/fixtures/frame_43.png')),fresh.centers),(True,)*5)
        centers=tuple((x-280,30) for x in [379,429,480,529,578,628,678])
        self.assertEqual(yellow_feedback(video_row(cv2.imread('tests/fixtures/frame_151.png')),centers),(True,True,False,False,False,False,False))
    def test_auto_locates_game_with_padding_and_scaling(self):
        f=cv2.imread('tests/fixtures/frame_36.png')
        for scale in [1,.65,1.5]:
            resized=cv2.resize(f,None,fx=scale,fy=scale)
            canvas=np.zeros((resized.shape[0]+140,resized.shape[1]+220,3),np.uint8)
            canvas[70:70+resized.shape[0],110:110+resized.shape[1]]=resized
            rect=Locator().locate(canvas);self.assertIsNotNone(rect)
            x,y,w,h=rect
            self.assertEqual(self.d.read(cv2.resize(canvas[y:y+h,x:x+w],(495,54))).sequence,('down','left','up'))
    def test_locator_does_not_lock_on_empty_scene(self):
        self.assertIsNone(Locator().locate(cv2.imread('tests/fixtures/frame_35.png')))

class InputTests(unittest.TestCase):
    def test_input_structure_size(self):
        self.assertEqual(ctypes.sizeof(win.INPUT),40 if ctypes.sizeof(ctypes.c_void_p)==8 else 28)
    def test_duplicate_directions_have_individual_releases(self):
        with patch.object(win,'is_target',return_value=True),patch.object(win,'foreground',return_value=True),patch.object(win,'key') as key:
            n=win.send_sequence(1,['left','left','up'],lambda:True,0,0)
        self.assertEqual(n,3)
        self.assertEqual([tuple(c.args) for c in key.call_args_list],[('left',),('left',True),('left',),('left',True),('up',),('up',True)])
    def test_cancel_during_hold_releases_key(self):
        active=iter([True,False])
        with patch.object(win,'is_target',return_value=True),patch.object(win,'foreground',return_value=True),patch.object(win,'key') as key:
            n=win.send_sequence(1,['up','down'],lambda:next(active,False),18,18)
        self.assertEqual(n,1);self.assertEqual([tuple(c.args) for c in key.call_args_list],[('up',),('up',True)])
    def test_no_keys_to_background_or_wrong_process(self):
        with patch.object(win,'is_target',return_value=True),patch.object(win,'foreground',return_value=False),patch.object(win,'key') as key:
            self.assertEqual(win.send_sequence(1,['up'],lambda:True),0);key.assert_not_called()
        with patch.object(win,'is_target',return_value=False),patch.object(win,'key') as key:
            with self.assertRaises(RuntimeError):win.send_sequence(1,['up'],lambda:True)
            key.assert_not_called()

if __name__=='__main__':unittest.main()
