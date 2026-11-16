"""Headless QtScrcpy recognizer. One RGB crop request -> one JSON reply.

No capture, windows, keyboard injection or round statistics live in this process.
The owning video window supplies decoded pixels and acknowledges actual input.
"""
import base64
import json
import sys
import time

import cv2
import numpy as np
from detector import Detector, RoundGate
from locator import Locator
from tracking import RegionConfirmation


class Recognizer:
    def __init__(self):
        self.detector = Detector(.70)
        self.gate = RoundGate(stable_seconds=.06)
        self.epoch = None
        self.locator = Locator()
        self.region_confirmation = RegionConfirmation()

    def handle(self, request):
        epoch = request['epoch']
        if epoch != self.epoch:
            self.gate.reset()
            self.region_confirmation = RegionConfirmation()
            self.epoch = epoch
        now = float(request['time'])
        if request.get('commit'):
            self.gate.commit(request['commit'], now)
            return None
        if request.get('locate'):
            w,h=int(request['width']),int(request['height'])
            if not (100 <= w <= 1080 and 50 <= h <= 1080):
                raise ValueError('Invalid locating frame dimensions')
            rgb=np.frombuffer(base64.b64decode(request['rgb'],validate=True),np.uint8).reshape(h,w,3)
            rect=self.region_confirmation.offer(self.locator.locate(cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR)))
            return dict(epoch=epoch,locate=True,region=[rect[0]/w,rect[1]/h,rect[2]/w,rect[3]/h] if rect else [])
        row = np.frombuffer(base64.b64decode(request['rgb'], validate=True), np.uint8)
        row = cv2.cvtColor(row.reshape(54, 495, 3), cv2.COLOR_RGB2BGR)
        start = time.perf_counter()
        reading = self.detector.read(row)
        sequence = self.gate.update(reading, now, consume=False)
        return dict(epoch=epoch, sequence=list(sequence), confidence=reading.confidence,
                    ms=(time.perf_counter()-start)*1000, visible=reading.visible)


def main():
    cv2.setNumThreads(1)
    recognizer = Recognizer()
    print(json.dumps({'ready': True}), flush=True)
    for line in sys.stdin:
        try:
            response = recognizer.handle(json.loads(line))
        except Exception as exc:
            response = {'error': str(exc)}
        if response is not None:
            print(json.dumps(response, separators=(',', ':')), flush=True)


if __name__ == '__main__':
    main()
