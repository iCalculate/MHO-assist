"""Portable application settings and normalized image-region cache."""
import hashlib
import json
from pathlib import Path

class Preferences:
    def __init__(self,path):
        self.path=Path(path)
        try:self.data=json.loads(self.path.read_text(encoding='utf-8'))
        except (OSError,ValueError):self.data={}
        if not isinstance(self.data,dict):self.data={}

    def save(self):
        self.path.parent.mkdir(parents=True,exist_ok=True)
        temp=self.path.with_suffix('.tmp')
        temp.write_text(json.dumps(self.data,ensure_ascii=False,indent=2),encoding='utf-8');temp.replace(self.path)

    @staticmethod
    def key(process,title,screen_range=None):
        value=json.dumps([process.lower(),title,list(screen_range) if screen_range else None],ensure_ascii=False)
        return hashlib.sha256(value.encode('utf-8')).hexdigest()[:24]

    def remember(self,key,roi,size):
        w,h=size;x,y,rw,rh=roi
        if not(0<=x<x+rw<=w and 0<=y<y+rh<=h):return
        self.data.setdefault('regions',{})[key]={'relative':[x/w,y/h,rw/w,rh/h],'size':[w,h]};self.save()

    def restore(self,key,size):
        try:
            record=self.data.get('regions',{})[key];sw,sh=record['size'];w,h=size
            if min(w,h,sw,sh)<=0 or abs((w/h)/(sw/sh)-1)>.025:return None
            a,b,c,d=record['relative'];rect=(round(a*w),round(b*h),round(c*w),round(d*h))
            x,y,rw,rh=rect
            return rect if 0<=x<x+rw<=w and 0<=y<y+rh<=h else None
        except (KeyError,TypeError,ValueError,ZeroDivisionError):return None

    def forget(self,key):
        self.data.get('regions',{}).pop(key,None);self.save()
