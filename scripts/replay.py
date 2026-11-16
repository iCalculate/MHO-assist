"""Offline replay: never sends keys."""
import argparse,json,time
from pathlib import Path
import cv2
import numpy as np
from mho_assist.detector import Detector,RoundGate,video_row,SYMBOLS
from mho_assist.locator import Locator
from mho_assist.tracking import RegionConfirmation,guarded_rect

def run(video,output,step=2,auto=False):
    cap=cv2.VideoCapture(str(video));fps=cap.get(cv2.CAP_PROP_FPS)
    if not cap.isOpened() or fps<=0:raise RuntimeError('Cannot open video')
    detector=Detector();gate=RoundGate(stable_seconds=.06);events=[];latencies=[];idx=0;tiles=[];locator=Locator();located=None;lock=RegionConfirmation()
    while True:
        ok,frame=cap.read()
        if not ok:break
        if idx%step==0:
            if auto:
                if located is None and idx%30==0:located=lock.offer(locator.locate(frame))
                if located is None:idx+=1;continue
                (x,y,w,h),size=guarded_rect(located,(frame.shape[1],frame.shape[0]));row=cv2.resize(frame[y:y+h,x:x+w],size)
            else:row=video_row(frame)
            start=time.perf_counter();r=detector.read(row)
            latencies.append((time.perf_counter()-start)*1000)
            seq=gate.update(r,idx/fps)
            if seq:
                events.append(dict(seconds=round(idx/fps,3),sequence=list(seq),confidence=round(r.confidence,3),centers=r.centers))
                tile=cv2.resize(row,(990,108));cv2.putText(tile,f'{idx/fps:.2f}s  '+','.join(seq),(5,18),cv2.FONT_HERSHEY_SIMPLEX,.45,(255,255,255),1)
                tiles.append(tile)
        idx+=1
    cap.release()
    result=dict(video=str(video),frames=idx,sampled_frames=len(latencies),events=events,
                automatic_location=located if auto else None,
                recognition_ms_median=round(float(np.median(latencies)),2),recognition_ms_p95=round(float(np.percentile(latencies,95)),2),
                note='Offline detections, not independently labelled accuracy or phone delivery measurements.')
    output_path=Path(output)
    output_path.parent.mkdir(parents=True,exist_ok=True)
    output_path.write_text(json.dumps(result,ensure_ascii=False,indent=2,default=int),encoding='utf-8')
    if tiles:cv2.imwrite(str(output_path.with_suffix('.jpg')),cv2.vconcat(tiles))
    print(json.dumps(result,ensure_ascii=True,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('video');p.add_argument('--output',default='analysis/replay.json');p.add_argument('--step',type=int,default=2);p.add_argument('--auto',action='store_true');a=p.parse_args();run(a.video,a.output,a.step,a.auto)
