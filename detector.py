"""Fixed-layout detector for the supplied 1080 x 484 game recording."""
from dataclasses import dataclass
from pathlib import Path
import cv2
import numpy as np

BASE = Path(__file__).resolve().parent
ROI = (280, 377, 775, 431)
LABELS = ('down', 'left', 'up', 'right')
SYMBOLS = dict(up='↑', down='↓', left='←', right='→')


@dataclass
class Reading:
    sequence: tuple = ()
    confidence: float = 0.
    visible: bool = False
    centers: tuple = ()


class Detector:
    def __init__(self, threshold=.72):
        self.threshold = threshold
        self.templates = [cv2.imread(str(BASE/'templates'/f'{k}.png'), 0) for k in LABELS]
        if any(t is None for t in self.templates):
            raise RuntimeError('Missing templates; run prepare_templates.py')

    def read(self, row):
        """Read a nominal 495x54 strip, optionally with symmetric guard pixels.

        Every glyph must agree on one row/pitch. Translation is tolerated;
        independent peaks cannot turn a diagonal/background into a sequence.
        """
        gray=cv2.cvtColor(row,cv2.COLOR_BGR2GRAY)
        hsv=cv2.cvtColor(row,cv2.COLOR_BGR2HSV)
        maps=np.array([cv2.matchTemplate(gray,t,cv2.TM_CCOEFF_NORMED) for t in self.templates])
        px=(row.shape[1]-495)/2;py=(row.shape[0]-54)/2
        cy=30+py;cx=249+px
        ylo=max(0,int(cy-24)-15);yhi=min(maps.shape[1],int(cy+24)-14)
        def fresh(k,x,y):
            p=hsv[max(0,y-12):y+13,max(0,x-12):x+13]
            h,s,v=cv2.split(p)
            if np.count_nonzero((h>=22)&(h<=39)&(s>65)&(v>200))>=100:return False
            lo,hi={'up':(12,42),'down':(125,174),'left':(38,91),'right':(87,122)}[LABELS[k]]
            return np.count_nonzero((h>=lo)&(h<=hi)&(s>65)&(v>125))>=28
        scratch=maps[:,ylo:yhi,:].copy();candidates=[]
        for _ in range(12):
            k,y,x=np.unravel_index(scratch.argmax(),scratch.shape)
            score=float(scratch[k,y,x])
            if score<.48:break
            point=(int(x+15),int(y+ylo+15),int(k),score);candidates.append(point)
            scratch[:,:,max(0,x-29):x+30]=-1
        if len(candidates)<2:return Reading(visible=False)
        best=Reading(visible=True)
        for n in range(3,10):
            expected=[cx+(i-(n-1)/2)*50 for i in range(n)]
            if expected[0]<15 or expected[-1]>row.shape[1]-15:continue
            found=[]
            for ex in expected:
                left=max(0,round(ex)-15-24);right=min(maps.shape[2],round(ex)-15+25)
                local=maps[:,ylo:yhi,left:right]
                k,yy,xx=np.unravel_index(local.argmax(),local.shape)
                found.append((int(k),float(local[k,yy,xx]),int(left+xx+15),int(yy+ylo+15)))
            confidence=min(f[1] for f in found)
            if confidence<self.threshold:continue
            dx=float(np.median([f[2]-ex for f,ex in zip(found,expected)]))
            yy=float(np.median([f[3] for f in found]))
            if any(abs(f[2]-ex-dx)>3 or abs(f[3]-yy)>3 for f,ex in zip(found,expected)):continue
            # Completed gold glyphs still occupy slots. Counting only fresh
            # colors would mistake a clicked prefix for a shorter new round.
            layout=[c for c in candidates if abs(c[1]-yy)<=4]
            if any(c[0]<expected[0]+dx-24 or c[0]>expected[-1]+dx+24 for c in layout):continue
            if not all(fresh(f[0],f[2],f[3]) for f in found):continue
            if confidence>best.confidence:
                best=Reading(tuple(LABELS[f[0]] for f in found),confidence,True,tuple((f[2],f[3]) for f in found))
        return best


class RoundGate:
    """Latch a round until its row disappears; rejected frames do not rearm."""
    def __init__(self, stable_frames=2, blank_seconds=.18, stable_seconds=0.):
        self.stable_frames=stable_frames
        self.blank_seconds=blank_seconds
        self.stable_seconds=stable_seconds
        self.reset()

    def reset(self):
        self.latched=False
        self.previous=()
        self.count=0
        self.blank_since=None
        self.committed_sequence=()
        self.committed_at=-1e9
        self.reason='等待完整序列'
        self.stable_since=None
        self.anchor_centers=()

    def commit(self, sequence, now):
        self.latched=True
        self.committed_sequence=tuple(sequence)
        self.committed_at=now

    def update(self, reading, now, consume=True):
        if reading.visible:
            self.blank_since=None
        else:
            if self.blank_since is None:
                self.blank_since=now
            if now-self.blank_since>=self.blank_seconds:
                self.latched=False
        if not reading.sequence:
            self.previous=()
            self.count=0
            self.stable_since=None;self.anchor_centers=()
            self.reason='未识别到完整、颜色有效且达到阈值的序列'
            return ()
        if reading.sequence==self.previous:
            self.count+=1
        else:
            self.previous=reading.sequence
            self.count=1
            self.stable_since=now;self.anchor_centers=reading.centers
        if self.stable_since is None:self.stable_since=now
        if reading.centers and self.anchor_centers:
            if len(reading.centers)!=len(self.anchor_centers) or any(abs(x-a)>1.25 or abs(y-b)>1.25 for (x,y),(a,b) in zip(reading.centers,self.anchor_centers)):
                self.stable_since=now;self.anchor_centers=reading.centers;self.count=1
        shorter_tail=len(reading.sequence)<len(self.committed_sequence) and any(self.committed_sequence[i:i+len(reading.sequence)]==reading.sequence for i in range(len(self.committed_sequence)-len(reading.sequence)+1))
        if self.latched and not shorter_tail and reading.sequence!=self.committed_sequence and self.count>=max(3,self.stable_frames) and now-self.committed_at>.35:
            self.latched=False
        if self.latched:
            self.reason='本轮已发送，等待新序列（防止重复输入）'
            return ()
        if self.count>=self.stable_frames and now-self.stable_since>=self.stable_seconds:
            self.reason='序列已稳定，可以输入'
            if consume:self.commit(reading.sequence,now)
            return reading.sequence
        self.reason='箭头弹出中，等待位置稳定' if self.stable_seconds and now-self.stable_since<self.stable_seconds else f'等待连续帧确认 {self.count}/{self.stable_frames}'
        return ()


def video_row(frame):
    if frame.shape[1]!=1080 or frame.shape[0]!=484:
        frame=cv2.resize(frame,(1080,484))
    x1,y1,x2,y2=ROI
    return frame[y1:y2,x1:x2]


def yellow_feedback(row,centers):
    hsv=cv2.cvtColor(row,cv2.COLOR_BGR2HSV);flags=[]
    for x,y in centers:
        p=hsv[max(0,y-12):y+13,max(0,x-12):x+13]
        h,s,v=cv2.split(p)
        flags.append(np.count_nonzero((h>=22)&(h<=39)&(s>65)&(v>200))>=100)
    return tuple(flags)
