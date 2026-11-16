"""Session + lifetime statistics. Structured UTF-8 .log files accompany SQLite."""
import json
import sqlite3
import uuid
import weakref
from collections import Counter
from datetime import datetime
from pathlib import Path

def timestamp():return datetime.now().astimezone().isoformat(timespec='milliseconds')

class HistoryStore:
    def __init__(self,folder,language='zh'):
        self.folder=Path(folder);self.logs=self.folder/'logs';self.logs.mkdir(parents=True,exist_ok=True)
        self.db=sqlite3.connect(str(self.folder/'history.sqlite3'),timeout=.15);self.db.row_factory=sqlite3.Row
        self._cleanup=weakref.finalize(self,self.db.close)
        self.db.executescript('''
          PRAGMA journal_mode=WAL;
          PRAGMA synchronous=NORMAL;
          CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY,started TEXT,ended TEXT,version TEXT,language TEXT);
          CREATE TABLE IF NOT EXISTS rounds(id TEXT PRIMARY KEY,session TEXT,time TEXT,sequence TEXT,confidence REAL,ms REAL,up_count INTEGER,down_count INTEGER,left_count INTEGER,right_count INTEGER);
          CREATE TABLE IF NOT EXISTS inputs(round_id TEXT PRIMARY KEY,session TEXT,sent INTEGER,total INTEGER,confirmed INTEGER,outcome TEXT);
          CREATE TABLE IF NOT EXISTS samples(id TEXT PRIMARY KEY,session TEXT,time TEXT,n INTEGER,total_ms REAL,low REAL,high REAL);
          CREATE TABLE IF NOT EXISTS cycles(id TEXT PRIMARY KEY,session TEXT,time TEXT,action TEXT,payload TEXT);
          CREATE INDEX IF NOT EXISTS idx_round_session ON rounds(session);
          CREATE INDEX IF NOT EXISTS idx_sample_session ON samples(session);
        ''')
        self.session_id=uuid.uuid4().hex
        self.session_log=self.logs/(datetime.now().strftime('%Y%m%d_%H%M%S')+'_'+self.session_id[:8]+'.log')
        self.closed=False
        self.db.execute('INSERT INTO sessions VALUES(?,?,?,?,?)',(self.session_id,timestamp(),None,'5',language));self.db.commit()
        self.write_log({'kind':'session_start','language':language,'version':5})
        self.save_summary()

    def write_log(self,event):
        data=dict(event);data.setdefault('time',timestamp());data['session']=self.session_id
        line=json.dumps(data,ensure_ascii=False,default=int)+'\n'
        for file in (self.session_log,self.logs/'history.log'):
            with file.open('a',encoding='utf-8') as f:f.write(line)

    def record(self,event):
        e=dict(event);e.setdefault('time',timestamp());kind=e['kind'];changed=True
        if kind=='round':
            seq=tuple(e['sequence'])
            if not seq or any(x not in ('up','down','left','right') for x in seq):raise ValueError('Invalid direction sequence')
            if not 0<=float(e['confidence'])<=1 or float(e['ms'])<0:raise ValueError('Invalid recognition metric')
            c=Counter(seq)
            changed=self.db.execute('INSERT OR IGNORE INTO rounds VALUES(?,?,?,?,?,?,?,?,?,?)',(e['id'],self.session_id,e['time'],json.dumps(seq),float(e['confidence']),float(e['ms']),c['up'],c['down'],c['left'],c['right'])).rowcount>0
        elif kind=='timing':
            if int(e['n'])<=0:return
            changed=self.db.execute('INSERT OR IGNORE INTO samples VALUES(?,?,?,?,?,?,?)',(e['id'],self.session_id,e['time'],int(e['n']),float(e['total_ms']),float(e['low']),float(e['high']))).rowcount>0
        elif kind=='input':
            # Per-round key totals count actual Windows sends, including interruptions.
            self.db.execute('INSERT OR REPLACE INTO inputs VALUES(?,?,?,?,?,?)',(e['round_id'],self.session_id,int(e['sent']),int(e['total']),0,e.get('outcome','sent')))
        elif kind=='feedback':
            self.db.execute('UPDATE inputs SET confirmed=?,outcome=? WHERE round_id=?',(int(e['confirmed']),e['outcome'],e['round_id']))
        elif kind=='cycle':
            e.setdefault('id',uuid.uuid4().hex)
            changed=self.db.execute('INSERT OR IGNORE INTO cycles VALUES(?,?,?,?,?)',(e['id'],self.session_id,e['time'],e['action'],json.dumps(e,ensure_ascii=False))).rowcount>0
        self.db.commit()
        if changed:self.write_log(e)

    def snapshot(self,lifetime=False):
        where='' if lifetime else ' WHERE session=?';params=() if lifetime else (self.session_id,)
        rows=self.db.execute('SELECT * FROM rounds'+where+' ORDER BY rowid DESC LIMIT 10',params).fetchall()[::-1]
        total=self.db.execute('SELECT COUNT(*),AVG(confidence),SUM(up_count),SUM(down_count),SUM(left_count),SUM(right_count),SUM(confidence<.8),SUM(confidence>=.8 AND confidence<.85),SUM(confidence>=.85 AND confidence<.88),SUM(confidence>=.88 AND confidence<.9),SUM(confidence>=.9 AND confidence<.92),SUM(confidence>=.92 AND confidence<.95),SUM(confidence>=.95) FROM rounds'+where,params).fetchone()
        directions=dict(zip(('up','down','left','right'),[int(v or 0) for v in total[2:6]]));hist=[int(v or 0) for v in total[6:13]]
        inputs=self.db.execute("SELECT COALESCE(SUM(sent>0),0),COALESCE(SUM(sent),0),COALESCE(SUM(outcome='confirmed' AND confirmed=total),0),COALESCE(SUM(confirmed),0),COALESCE(SUM(outcome IN ('unconfirmed','interrupted','unverified')),0) FROM inputs"+where,params).fetchone()
        sample=self.db.execute('SELECT COALESCE(SUM(n),0),COALESCE(SUM(total_ms),0) FROM samples'+where,params).fetchone()
        # Recent points only: weighted mean of detector calls within each sample.
        timing=list(reversed(self.db.execute('SELECT time,n,total_ms,low,high FROM samples'+where+' ORDER BY rowid DESC LIMIT 60',params).fetchall()))
        return dict(rounds=total[0],keys=sum(directions.values()),directions=directions,
                    confidence=total[1] or 0.,confidence_hist=hist,
                    mean_ms=sample[1]/sample[0] if sample[0] else 0.,frames=sample[0],
                    sent_rounds=inputs[0],sent_keys=inputs[1],confirmed_rounds=inputs[2],confirmed_keys=inputs[3],unconfirmed_rounds=inputs[4],
                    recent_rounds=[dict(id=r['id'],sequence=json.loads(r['sequence']),confidence=r['confidence'],time=r['time']) for r in rows[-10:]],
                    timing=[dict(time=r['time'],mean=r['total_ms']/r['n'],n=r['n'],low=r['low'],high=r['high']) for r in timing],
                    sessions=self.db.execute('SELECT COUNT(*) FROM sessions').fetchone()[0],
                    completed_games=self.db.execute("SELECT COUNT(*) FROM cycles"+(' WHERE action=?' if lifetime else ' WHERE session=? AND action=?'),('game_completed',) if lifetime else (self.session_id,'game_completed')).fetchone()[0])

    def save_summary(self):
        data={'updated':timestamp(),'current_session':self.session_id,'session':self.snapshot(),'history':self.snapshot(True),
              'definitions':'Rounds = stable recognized groups, including preview. Sent = Windows events. Confirmed = yellow image feedback. Mean ms = detector algorithm only. History begins with V4.'}
        target=self.folder/'summary.json';temp=target.with_suffix('.tmp')
        temp.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8');temp.replace(target)

    def close(self):
        if self.closed:return
        self.db.execute("UPDATE inputs SET outcome='unverified' WHERE session=? AND outcome='sent'",(self.session_id,))
        self.write_log({'kind':'session_end'});self.db.execute('UPDATE sessions SET ended=? WHERE id=?',(timestamp(),self.session_id));self.db.commit()
        self.save_summary();self.db.close();self.closed=True

class TimingBatch:
    def __init__(self):self.values=[];self.index=0
    def add(self,ms):self.values.append(float(ms))
    def flush(self,prefix):
        if not self.values:return None
        self.index+=1;v=self.values;self.values=[]
        return dict(kind='timing',id=f'{prefix}:t{self.index}',n=len(v),total_ms=sum(v),low=min(v),high=max(v))
