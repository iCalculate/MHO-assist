"""Locate the game's four-button diamond anywhere inside an image."""
import cv2
import numpy as np
from mho_assist.resources import load_template

CENTERS={'up':(900,283),'left':(836,347),'down':(900,410),'right':(965,347)}

class Locator:
    def __init__(self):
        self.templates={k:load_template(f'control_{k}.png') for k in CENTERS}

    def locate(self,image):
        factor=min(1.,1000/image.shape[1])
        small=cv2.resize(image,None,fx=factor,fy=factor) if factor<1 else image
        gray=cv2.cvtColor(small,cv2.COLOR_BGR2GRAY)
        guess=min(small.shape[1]/1080,small.shape[0]/484)
        scales=sorted(set([round(float(x),4) for x in np.geomspace(.28,1.55,22)]+[round(guess,4)]))
        scales.append(None)
        best=None
        for scale in scales:
            if scale is None:
                if best:scales.extend(np.linspace(best[3]*.95,best[3]*1.05,17))
                continue
            template=cv2.resize(self.templates['right'],None,fx=scale,fy=scale)
            th,tw=template.shape
            if min(th,tw)<12 or th>=gray.shape[0] or tw>=gray.shape[1]:continue
            scores=cv2.matchTemplate(gray,template,cv2.TM_CCOEFF_NORMED)
            _,score,_,pos=cv2.minMaxLoc(scores)
            if score<.64:continue
            ox=pos[0]+tw/2-965*scale;oy=pos[1]+th/2-347*scale
            values=[score]
            anchors=[(CENTERS['right'],(pos[0]+tw/2,pos[1]+th/2),score)]
            for name in ['up','left','down']:
                t=cv2.resize(self.templates[name],(tw,th))
                cx,cy=CENTERS[name];x=round(ox+cx*scale-tw/2);y=round(oy+cy*scale-th/2)
                x1=max(0,x-9);y1=max(0,y-9);x2=min(gray.shape[1],x+tw+9);y2=min(gray.shape[0],y+th+9)
                p=gray[y1:y2,x1:x2]
                if p.shape[0]>=th and p.shape[1]>=tw:
                    _,v,_,q=cv2.minMaxLoc(cv2.matchTemplate(p,t,cv2.TM_CCOEFF_NORMED))
                    values.append(v);anchors.append((CENTERS[name],(x1+q[0]+tw/2,y1+q[1]+th/2),v))
                else:values.append(0.)
            quality=float(np.mean(sorted(values,reverse=True)[:3]))
            if sum(v>.62 for v in values)<3 or quality<.72:continue
            if best is None or quality>best[0]:
                good=[a for a in anchors if a[2]>.62]
                source=np.array([a[0] for a in good],float);target=np.array([a[1] for a in good],float)
                src=source-source.mean(axis=0);dst=target-target.mean(axis=0)
                refined=float(np.sum(src*dst)/np.sum(src*src))
                origin=target.mean(axis=0)-refined*source.mean(axis=0)
                best=(quality,float(origin[0]),float(origin[1]),refined)
        if best is None:return None
        _,ox,oy,scale=best
        x=round((ox+280*scale)/factor);y=round((oy+377*scale)/factor)
        w=round(495*scale/factor);h=round(54*scale/factor)
        if x<0 or y<0 or x+w>image.shape[1] or y+h>image.shape[0]:return None
        return x,y,w,h
