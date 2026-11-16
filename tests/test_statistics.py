import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch,MagicMock
from history_store import HistoryStore,TimingBatch
from language import TEXT,tr,reason
import assist
import app_window
from PySide6 import QtWidgets,QtCore
from game_bridge import GameBridge

class HistoryTests(unittest.TestCase):
    def event(self,store,key,seq,confidence=.9):
        store.record(dict(kind='round',id=key,sequence=seq,confidence=confidence,ms=4.))
    def test_session_and_lifetime_totals_survive_restart(self):
        with tempfile.TemporaryDirectory() as folder:
            first=HistoryStore(folder)
            self.event(first,'r1',['up','up','left']);self.event(first,'r2',['right','down','down','left'],.8)
            first.record(dict(kind='input',round_id='r1',sent=3,total=3))
            first.record(dict(kind='feedback',round_id='r1',confirmed=3,outcome='confirmed'))
            first.record(dict(kind='input',round_id='r2',sent=2,total=4,outcome='interrupted'))
            data=first.snapshot();self.assertEqual(data['rounds'],2);self.assertEqual(data['keys'],7)
            self.assertEqual(data['directions'],dict(up=2,down=2,left=2,right=1));self.assertEqual(data['sent_keys'],5)
            self.assertEqual(data['confirmed_rounds'],1);self.assertEqual(data['confirmed_keys'],3);first.close()
            second=HistoryStore(folder);self.assertEqual(second.snapshot()['rounds'],0)
            self.event(second,'r3',['right']*5,.75)
            self.assertEqual(second.snapshot()['keys'],5);self.assertEqual(second.snapshot(True)['keys'],12)
            self.assertEqual(second.snapshot(True)['rounds'],3);second.close()
            summary=json.loads((Path(folder)/'summary.json').read_text(encoding='utf-8'))
            self.assertEqual(summary['history']['keys'],12);self.assertEqual(summary['session']['keys'],5)
            self.assertEqual(len(list((Path(folder)/'logs').glob('*.log'))),3)
    def test_round_and_sample_ids_are_idempotent_and_mean_is_weighted(self):
        with tempfile.TemporaryDirectory() as folder:
            store=HistoryStore(folder)
            try:
                for _ in range(2):self.event(store,'same',['down']*3)
                for _ in range(2):store.record(dict(kind='timing',id='s1',n=10,total_ms=20,low=1,high=4))
                store.record(dict(kind='timing',id='s2',n=2,total_ms=20,low=9,high=11))
                self.assertEqual(store.snapshot()['rounds'],1);self.assertEqual(store.snapshot()['frames'],12)
                self.assertAlmostEqual(store.snapshot()['mean_ms'],40/12)
                self.assertEqual(store.snapshot()['confidence_hist'],[0,0,0,0,1,0,0])
            finally:store.close()
    def test_close_marks_pending_feedback_unverified_not_confirmed(self):
        with tempfile.TemporaryDirectory() as folder:
            store=HistoryStore(folder);self.event(store,'r',['left']*4)
            store.record(dict(kind='input',round_id='r',sent=4,total=4));store.close()
            data=json.loads((Path(folder)/'summary.json').read_text())['history']
            self.assertEqual(data['confirmed_rounds'],0);self.assertEqual(data['unconfirmed_rounds'],1)
    def test_round_log_contains_per_round_distribution_source(self):
        with tempfile.TemporaryDirectory() as folder:
            store=HistoryStore(folder);self.event(store,'r',['up','left','left'])
            store.close();lines=[json.loads(s) for s in store.session_log.read_text(encoding='utf-8').splitlines()]
            self.assertEqual(next(e['sequence'] for e in lines if e['kind']=='round'),['up','left','left'])
    def test_timing_batch_retains_all_frames_without_per_frame_disk_io(self):
        batch=TimingBatch()
        for value in [1.,2.,3.]:batch.add(value)
        event=batch.flush('w');self.assertEqual(event['n'],3);self.assertEqual(event['total_ms'],6.)
        self.assertIsNone(batch.flush('w'))
    def test_refined_confidence_bins_preserve_history_and_boundaries(self):
        with tempfile.TemporaryDirectory() as folder:
            store=HistoryStore(folder)
            values=[.79,.8,.849,.85,.879,.88,.899,.9,.919,.92,.949,.95,1.]
            for i,value in enumerate(values):self.event(store,str(i),['left']*3,value)
            self.assertEqual(store.snapshot()['confidence_hist'],[1,2,2,2,2,2,2]);store.close()
    def test_completed_games_persist_and_duplicate_events_do_not_count_twice(self):
        with tempfile.TemporaryDirectory() as folder:
            store=HistoryStore(folder);event=dict(kind='cycle',id='finish',action='game_completed',counts=[10,10,10])
            store.record(event);store.record(event);self.assertEqual(store.snapshot()['completed_games'],1);store.close()
            other=HistoryStore(folder);self.assertEqual(other.snapshot()['completed_games'],0);self.assertEqual(other.snapshot(True)['completed_games'],1);other.close()

class DashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    def test_language_switch_keeps_session_data_and_english_labels(self):
        with tempfile.TemporaryDirectory() as folder:
            ui=app_window.Main(data_dir=folder,smoke=True,language='zh')
            try:
                ui.on_metric(dict(kind='round',id='r',sequence=['up','down','left','right'],confidence=.95,ms=3.))
                session=ui.store.session_id;ui.languages.setCurrentIndex(1)
                self.assertEqual(ui.lang,'en');self.assertEqual(ui.store.session_id,session)
                self.assertEqual(ui.dashboard.snapshot['keys'],4);self.assertEqual(ui.dashboard.cards['rounds'][0].text(),'Recognized rounds')
                self.assertEqual(ui.dashboard.cards['confidence'][1].text(),'95.0%')
                ui.scope.setCurrentIndex(1);self.assertEqual(ui.dashboard.snapshot['keys'],4)
            finally:ui.close()
    def test_data_directory_is_shared_across_packaged_releases(self):
        with patch.object(app_window,'APP_DIR',Path('D:/Game/MH_assist/release_v4/ArrowAssist')),patch.object(app_window.sys,'frozen',True,create=True):
            self.assertEqual(app_window.data_directory(),Path('D:/Game/MH_assist/data'))
    def test_repeat_switch_updates_runtime_flag_and_does_not_arm_input(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as folder:
            ui=app_window.Main(data_dir=folder,smoke=True,language='zh')
            try:
                ui.repeat.setChecked(False);fake=SimpleNamespace(auto_repeat=False,armed=False);ui.worker=fake
                ui.repeat.click();self.assertTrue(fake.auto_repeat);self.assertFalse(fake.armed)
                ui.repeat.click();self.assertFalse(fake.auto_repeat)
            finally:ui.worker=None;ui.close()
    def test_every_label_has_both_languages_and_dynamic_status_translates(self):
        self.assertTrue(all(len(value)==2 and all(value) for value in TEXT.values()))
        self.assertEqual(reason('等待黄色反馈 2/4','en'),'Waiting for yellow feedback 2/4')
        self.assertIn('Ctrl',tr('mapping','en',key='Ctrl'))
    def test_tool_launcher_uses_existing_grouped_shortcut(self):
        with tempfile.TemporaryDirectory() as folder:
            bridge=GameBridge();bridge.folder=Path(folder);(bridge.folder/'游戏按键位置编辑器.lnk').touch()
            with patch('game_bridge.QtGui.QDesktopServices.openUrl',return_value=True) as opened:
                bridge.open_tool('editor');self.assertIn('游戏按键位置编辑器.lnk',opened.call_args[0][0].toLocalFile())
            bridge.root=Path(folder)/'installed'
            archived=bridge.root/'tool-shortcuts'/'QtScrcpy 输入修复版.lnk'
            archived.parent.mkdir(parents=True);archived.touch()
            with patch('game_bridge.QtGui.QDesktopServices.openUrl',return_value=True) as opened:
                bridge.open_tool('mirror');self.assertEqual(str(archived).replace('\\','/'),opened.call_args[0][0].toLocalFile())

if __name__=='__main__':unittest.main()
