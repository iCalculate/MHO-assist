"""Three dance phases and page-verified, cancellable next-game navigation.

No screen capture or input side effects here: the worker supplies frames and
executes one command at a time. Shift is a standalone tap, never held with [.
"""
from dataclasses import dataclass
import cv2
import numpy as np
from mho_assist.resources import load_template

COMMANDS=('shift','bracket_left','bracket_right','bracket_right')
NAMES=('Shift','[',']',']')

def canonical(frame,region=None):
    if region:
        x,y,w,h=region;s=w/495.;ox=x-280*s;oy=y-377*s
        matrix=np.float32([[1/s,0,-ox/s],[0,1/s,-oy/s]])
        return cv2.warpAffine(frame,matrix,(1080,484))
    return cv2.resize(frame,(1080,484))

@dataclass
class Page:
    world: bool=False
    result: bool=False
    dance: bool=False
    frame: object=None

class PageDetector:
    def __init__(self):
        self.templates={k:load_template(f'cycle/{k}.png') for k in ['result_win','result_loss','world_match']}
        self.controls={k:load_template(f'control_{k}.png') for k in ['up','down','left','right']}
    def read(self,frame,region=None):
        f=canonical(frame,region);gray=cv2.cvtColor(f,cv2.COLOR_BGR2GRAY)
        def score(name,box):
            x1,y1,x2,y2=box;p=gray[y1:y2,x1:x2];t=self.templates[name]
            return float(cv2.matchTemplate(p,t,cv2.TM_CCOEFF_NORMED).max())
        # Normalized correlation alone also matches a dimmed background button
        # behind a modal. Require its normal text brightness as well.
        crop=gray[180:258,140:295];template=self.templates['world_match']
        _,world_score,_,pos=cv2.minMaxLoc(cv2.matchTemplate(crop,template,cv2.TM_CCOEFF_NORMED))
        th,tw=template.shape;button=crop[pos[1]:pos[1]+th,pos[0]:pos[0]+tw]
        world=world_score>=.78 and float(np.percentile(button,95))>=float(np.percentile(template,95))*.82
        result=max(score(k,(465,270,615,405)) for k in ['result_win','result_loss'])>=.74
        points={'up':(900,283),'left':(836,347),'down':(900,410),'right':(965,347)};valid=0
        for k,(x,y) in points.items():
            value=float(cv2.matchTemplate(gray[y-33:y+33,x-33:x+33],self.controls[k],cv2.TM_CCOEFF_NORMED).max())
            valid+=value>=.68
        if region is not None and not(world or result or valid>=3):return self.read(frame,None)
        return Page(world,result,valid>=3,f)

def page_delta(before,after):
    """Changed UI tiles, excluding tiny motion and noisy pixels.

    Character/camera animation alone is insufficient: use the central panel
    and left interaction panel, and require broad, strong structural changes.
    """
    def features(f):
        f=cv2.resize(f,(270,121));g=cv2.cvtColor(f,cv2.COLOR_BGR2GRAY)
        return cv2.GaussianBlur(g,(5,5),0)
    a=features(before);b=features(after);diff=cv2.absdiff(a,b)
    tiles=[]
    for x1,y1,x2,y2 in [(70,26,222,103),(23,35,73,68)]:
        for y in range(y1,y2-7,8):
            for x in range(x1,x2-7,8):tiles.append(float(diff[y:y+8,x:x+8].mean()))
    return sum(v>=18 for v in tiles)/max(1,len(tiles))

class MatchProgress:
    def __init__(self):self.reset()
    def reset(self):self.phase=0;self.counts=[0,0,0];self.last_time=None;self.last_length=0;self.ids=set()
    def group(self,identifier,length,now):
        if identifier in self.ids:return False
        gap=now-self.last_time if self.last_time is not None else 0
        if length==3 and self.phase and (self.phase>1 or gap>10):self.reset()
        self.ids.add(identifier)
        if self.phase==0:self.phase=3 if length>=7 else 2 if length>=6 else 1
        elif self.phase==1 and (self.counts[0]>=10 or length>=6 or gap>6 and self.counts[0]>=5 and length<self.last_length):self.phase=2
        elif self.phase==2 and (self.counts[1]>=10 or length>=7 or gap>6 and self.counts[1]>=5 and length<self.last_length):self.phase=3
        self.counts[self.phase-1]+=1;self.last_time=now;self.last_length=length;return True
    @property
    def total(self):return sum(self.counts)

class AutoFarm:
    def __init__(self,interval=1.,timeout=6.):
        self.progress=MatchProgress();self.interval=interval;self.timeout=timeout
        self.state='playing';self.step=0;self.sent_at=0;self.before=None;self.before_page=None
        self.ready_since=None;self.changed_since=None;self.game_count=0;self.error='';self.events=[];self.next_wait_at=0
        self.recorded_finish=False
        self.change=0.
    def event(self,kind,**data):self.events.append(dict(kind='cycle',action=kind,**data))
    def group(self,identifier,length,now):
        if self.state=='next_wait':
            self.progress.reset();self.recorded_finish=False;self.state='playing';self.event('next_game_started')
        previous=self.progress.phase
        self.progress.group(identifier,length,now)
        if previous>1 and self.progress.phase==1:self.recorded_finish=False
    def cancel(self,why='cancelled'):
        if self.state in ('navigation','change_wait','next_wait'):
            self.event('navigation_cancelled',reason=why);self.state='paused';self.before=None
        self.ready_since=None
        if self.state=='error' and why=='disabled':self.state='paused';self.error=''
    def snapshot(self):return dict(phase=self.progress.phase,counts=list(self.progress.counts),state=self.state,step=self.step,games=self.game_count,error=self.error,change=round(self.change,3))
    def update(self,page,now,enabled,armed,foreground=True):
        if not enabled or not armed or not foreground:
            self.cancel('disabled' if not enabled else 'paused' if not armed else 'focus_lost');return None
        if self.state=='paused':self.state='playing';self.error=''
        if self.state=='error':return None
        if self.state=='playing':
            ready=(page.result or page.world) and not page.dance
            # Never infer completion from an intermission or blank screen.
            if not ready:self.ready_since=None;return None
            if self.ready_since is None:self.ready_since=now
            if now-self.ready_since<1.:return None
            if self.progress.phase==3 and self.progress.counts[2]>=8 and not self.recorded_finish:
                self.game_count+=1;self.recorded_finish=True;self.event('game_completed',counts=list(self.progress.counts),groups=self.progress.total)
            self.state='navigation';self.step=0;self.sent_at=now-self.interval
        if self.state=='next_wait':
            if now-self.next_wait_at>90:
                self.fail('next_game_timeout');return None
            return None
        if self.state=='change_wait':
            score=page_delta(self.before,page.frame)
            self.change=score
            # Shift may be unnecessary when the result already returned to the
            # matching page. Brackets always need an observed page transition.
            if self.step==0:changed=page.world
            elif self.step==1 and self.before_page.world:changed=not page.world and score>=.04
            else:changed=score>=.18
            if changed:
                if self.changed_since is None:self.changed_since=now
            else:self.changed_since=None
            if self.changed_since is not None and now-self.changed_since>=.2 and now-self.sent_at>=self.interval:
                already=self.step==0 and self.before_page.world and page.world
                self.event('page_verified' if already else 'page_changed',command=NAMES[self.step],change=round(score,4),method='already_match_page' if already else 'world_marker' if page.world!=self.before_page.world else 'ui_tiles')
                self.step+=1;self.before=None
                if self.step==len(COMMANDS):
                    self.state='next_wait';self.next_wait_at=now;self.event('restart_sequence_finished');return None
                self.state='navigation'
            elif now-self.sent_at>=self.timeout:self.fail('page_did_not_change');return None
        if self.state=='navigation' and now-self.sent_at>=self.interval:return COMMANDS[self.step]
        return None
    def sent(self,page,now):
        self.before=page.frame.copy();self.before_page=page;self.sent_at=now;self.changed_since=None;self.state='change_wait';self.change=0.
        self.event('navigation_key_sent',command=NAMES[self.step],index=self.step)
    def fail(self,why):
        self.error=why;self.state='error';self.event('navigation_failed',reason=why,step=self.step)
