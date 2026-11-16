"""Region confirmation, guard capture and recovery decisions."""
import cv2

class RegionConfirmation:
    def __init__(self):self.previous=None;self.count=0
    def offer(self,rect):
        if rect is None:self.previous=None;self.count=0;return None
        if self.previous and all(abs(a-b)<=max(2,rect[2]*.005) for a,b in zip(rect,self.previous)):
            self.count+=1
        else:self.count=1
        self.previous=tuple(rect)
        return tuple(rect) if self.count>=2 else None

def guarded_rect(rect,bounds,pad=24):
    x,y,w,h=rect;bw,bh=bounds
    sx=w/495;sy=h/54
    # Keep padding symmetric so normalized glyph coordinates have a fixed origin.
    gx=min(round(pad*sx),x,bw-x-w);gy=min(round(pad*sy),y,bh-y-h)
    gx=max(0,gx);gy=max(0,gy)
    return (x-gx,y-gy,w+2*gx,h+2*gy),(495+round(2*gx/sx),54+round(2*gy/sy))

def capture_row(capture,origin,rect,bounds):
    (rx,ry,rw,rh),(nw,nh)=guarded_rect(rect,bounds)
    x,y=origin
    import numpy as np
    pixels=np.asarray(capture.grab(dict(left=x+rx,top=y+ry,width=rw,height=rh)))[:,:,:3]
    return cv2.resize(pixels,(nw,nh),interpolation=cv2.INTER_LINEAR)

def moved(a,b):
    return any(abs(x-y)>max(3,a[2]*.008) for x,y in zip(a,b))
