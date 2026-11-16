import json
import unittest
from pathlib import Path
from unittest.mock import patch
import numpy as np
import cv2
from autofarm import AutoFarm,Page,PageDetector,MatchProgress,COMMANDS,page_delta
import windows_io as win

def page(color=0,world=False,result=False,dance=False):return Page(world,result,dance,np.full((484,1080,3),color,np.uint8))

class MatchTests(unittest.TestCase):
    def test_latest_real_history_has_three_ten_group_phases(self):
        observations=json.loads(Path('tests/fixtures/match_history.json').read_text(encoding='utf-8'))
        track=MatchProgress()
        for index, (seconds, length) in enumerate(observations):
            track.group(index,length,seconds)
        self.assertEqual(track.counts,[10,10,10]);self.assertEqual(track.phase,3);self.assertEqual(track.total,30)
        track.group(len(observations)-1,observations[-1][1],0)
        self.assertEqual(track.total,30)
    def test_new_game_resets_when_three_key_opening_returns(self):
        track=MatchProgress()
        for i,n in enumerate([3,4]+[5]*8+[4,5]+[6]*8+[5,6]+[7]*8):track.group(str(i),n,i*2)
        self.assertEqual(track.counts,[10,10,10]);track.group('new',3,100)
        self.assertEqual(track.counts,[1,0,0]);self.assertEqual(track.phase,1)

class NavigationTests(unittest.TestCase):
    def start(self):
        c=AutoFarm();p=page(world=True)
        self.assertIsNone(c.update(p,0,True,True));self.assertEqual(c.update(p,1.1,True,True),'shift');c.sent(p,1.1)
        return c
    def test_exact_standalone_shift_bracket_sequence_and_one_second_gaps(self):
        c=self.start();sent=['shift'];times=[1.1]
        # Shift may leave an already-open matching page unchanged.
        self.assertIsNone(c.update(page(world=True),1.3,True,True))
        self.assertEqual(c.update(page(world=True),2.2,True,True),'bracket_left')
        c.sent(page(world=True),2.2);sent.append('bracket_left');times.append(2.2)
        for sent_at,color,next_time in [(2.2,255,3.3),(3.3,0,4.4),(4.4,255,5.5)]:
            after=page(color)
            self.assertIsNone(c.update(after,sent_at+.25,True,True))
            command=c.update(after,next_time,True,True)
            if command:sent.append(command);times.append(next_time);c.sent(after,next_time)
        self.assertEqual(sent,list(COMMANDS));self.assertTrue(all(b-a>=1 for a,b in zip(times,times[1:])))
        self.assertEqual(c.state,'next_wait')
        c.group('first_next_game',3,15);self.assertEqual(c.state,'playing');self.assertEqual(c.progress.counts,[1,0,0])
    def test_no_navigation_when_disabled_unarmed_or_unfocused(self):
        for enabled,armed,focused in [(False,True,True),(True,False,True),(True,True,False)]:
            c=AutoFarm()
            for t in range(20):self.assertIsNone(c.update(page(world=True),t,enabled,armed,focused))
    def test_intermission_does_not_restart_even_after_thirty_groups(self):
        c=AutoFarm()
        for i,n in enumerate([3,4]+[5]*8+[4,5]+[6]*8+[5,6]+[7]*8):c.group(str(i),n,i*2)
        for t in [61,65,70,80]:self.assertIsNone(c.update(page(),t,True,True))
        self.assertEqual(c.game_count,0)
    def test_no_change_times_out_without_resending(self):
        c=self.start();p=page(world=True)
        c.update(p,1.3,True,True);self.assertEqual(c.update(p,2.2,True,True),'bracket_left');c.sent(p,2.2)
        for t in [2.5,3.5,4.5,7,8.3,10]:self.assertIsNone(c.update(p,t,True,True))
        self.assertEqual(c.state,'error');self.assertEqual(c.error,'page_did_not_change')
        self.assertEqual(sum(e['action']=='navigation_key_sent' for e in c.events),2)
    def test_world_motion_does_not_acknowledge_failed_left_bracket(self):
        c=self.start();c.update(page(world=True),1.3,True,True);c.update(page(world=True),2.2,True,True);c.sent(page(world=True),2.2)
        for t in [2.5,3.5,4.5]:self.assertIsNone(c.update(page(255,world=True),t,True,True))
        self.assertEqual(c.state,'change_wait')
    def test_pause_interrupts_navigation_and_requires_a_ready_page_to_restart(self):
        c=self.start();self.assertIsNone(c.update(page(),1.5,True,False));self.assertEqual(c.state,'paused')
        for t in [2,5,8]:self.assertIsNone(c.update(page(),t,True,True))
    def test_shift_requires_match_page_not_animated_result(self):
        c=AutoFarm();p=page(result=True)
        c.update(p,0,True,True);c.update(p,1.1,True,True);c.sent(p,1.1)
        for t in [1.5,2.5,3.5]:self.assertIsNone(c.update(page(255,result=True),t,True,True))
        self.assertEqual(c.state,'change_wait')

class PageTests(unittest.TestCase):
    def test_recorded_win_loss_world_and_playing_screens(self):
        reader=PageDetector()
        for file,expected in [('loss.png','result'),('win.png','result'),('world.png','world'),('frame_43.png','dance')]:
            im=cv2.imread(f'tests/fixtures/{file}');self.assertIsNotNone(im);self.assertTrue(getattr(reader.read(im),expected))
    def test_stale_region_can_fall_back_to_window_geometry(self):
        reader=PageDetector();im=cv2.imread('tests/fixtures/world.png')
        self.assertTrue(reader.read(im,(340,375,447,49)).world)
    def test_dimmed_background_button_is_not_an_actionable_match_page(self):
        reader=PageDetector();im=cv2.imread('tests/fixtures/world.png')
        dimmed=(im.astype(np.float32)*.5).astype(np.uint8)
        self.assertTrue(reader.read(im).world);self.assertFalse(reader.read(dimmed).world)
    def test_delta_ignores_small_noise_and_detects_broad_page_change(self):
        a=np.zeros((484,1080,3),np.uint8);b=a.copy();b[200:220,500:520]=255
        self.assertLess(page_delta(a,b),.04);self.assertGreater(page_delta(a,np.full_like(a,255)),.9)
    def test_navigation_scan_codes_release_shift_and_do_not_use_extended_flags(self):
        events=[]
        def send(n,event,size):events.append((event._obj.ki.wScan,event._obj.ki.dwFlags));return 1
        with patch.object(win.user,'SendInput',side_effect=send):
            win.key('shift');win.key('shift',True);win.key('bracket_left');win.key('bracket_left',True)
        self.assertEqual(events,[(0x2a,8),(0x2a,10),(0x1a,8),(0x1a,10)])

if __name__=='__main__':unittest.main()
