"""Local evidence for unconfirmed inputs: arrow strips only, not desktops."""
import json
from datetime import datetime
from pathlib import Path
import cv2
import windows_io as win

def save_unconfirmed(folder,pending,row,region,hwnd):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    stem=datetime.now().strftime('%Y%m%d_%H%M%S_%f');path=folder/(stem+'.json')
    cv2.imwrite(str(folder/(stem+'_before.png')),pending['before'])
    cv2.imwrite(str(folder/(stem+'_after.png')),row)
    data={k:v for k,v in pending.items() if k not in ('before','time')}
    data.update(region=region,target=win.title(hwnd),outcome='Windows accepted key events; game confirmation missing')
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2,default=int),encoding='utf-8')
    return path
