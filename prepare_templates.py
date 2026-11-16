"""Extract the four fixed arrow glyphs from the user's reference video."""
from pathlib import Path
import cv2

def prepare(video):
    root=Path(__file__).resolve().parent
    (root/'templates').mkdir(exist_ok=True)
    cap=cv2.VideoCapture(str(video))
    try:
        for label,sec,x in [('down',36,480),('left',36,528),('up',36,578),('right',50,430)]:
            cap.set(cv2.CAP_PROP_POS_MSEC,sec*1000)
            ok,frame=cap.read()
            if not ok: raise RuntimeError(f'Cannot decode {sec}s')
            if frame.shape[:2]!=(484,1080): raise RuntimeError('Reference video dimensions changed')
            patch=cv2.cvtColor(frame[392:422,x-15:x+15],cv2.COLOR_BGR2GRAY)
            cv2.imwrite(str(root/'templates'/f'{label}.png'),patch)
        cap.set(cv2.CAP_PROP_POS_MSEC,36000);ok,frame=cap.read()
        if not ok:raise RuntimeError('Cannot extract locator controls')
        from locator import CENTERS
        for label,(x,y) in CENTERS.items():
            cv2.imwrite(str(root/'templates'/f'control_{label}.png'),cv2.cvtColor(frame[y-23:y+23,x-23:x+23],cv2.COLOR_BGR2GRAY))
    finally:
        cap.release()

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('video');args=p.parse_args();prepare(args.video)
