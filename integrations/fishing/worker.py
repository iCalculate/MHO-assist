"""Read raw QtScrcpy frames on stdin and return fishing decisions on stdout."""

import argparse
from base64 import b64encode
from collections import Counter
import json
from math import cos, radians, sin
from pathlib import Path
import struct
import sys
from time import monotonic

import cv2
import numpy as np

from strategy import FISH_GROUP_RADIUS, PHASE_LABELS, FishingStrategy
from vision import ACTION, BAITS, CONTINUE, GEAR_CLOSE, HEIGHT, RODS, START, TENSION_CENTER, WIDTH, FishingVision, Phase


HEADER = struct.Struct(">III")
RESULT_SAMPLE_INTERVAL = 0.10
RESULT_READ_SECONDS = 0.42


class CatchFrames:
    def __init__(self):
        self.samples = []
        self.last_result = None
        self.hook_labels = Counter()
        self.last_hook_sample = float("-inf")

    def reset_hook(self):
        self.hook_labels.clear()
        self.last_hook_sample = float("-inf")

    def update(self, frame, phase, vision, now):
        if self.last_result is not None and now - self.last_result > 0.32:
            self.samples.clear()
        if phase == Phase.BITE and now - self.last_hook_sample >= RESULT_SAMPLE_INTERVAL:
            self.last_hook_sample = now
            self.hook_labels.update(vision.hook_details(frame).items())
        if phase != Phase.RESULT:
            return False
        self.last_result = now
        if not self.samples or now - self.samples[-1][0] >= RESULT_SAMPLE_INTERVAL:
            self.samples.append((now, vision.catch_details(frame)))
        elapsed = now - self.samples[0][0]
        return (len(self.samples) >= 3 and elapsed >= RESULT_READ_SECONDS) or elapsed >= 0.85

    def take(self):
        samples = [details for _, details in self.samples]
        self.samples.clear()
        self.last_result = None
        result = {}
        for field in ("name", "rarity", "size", "crown"):
            values = Counter(sample[field] for sample in samples if field in sample)
            # Any plausible crown blocks a no-crown decision until its color repeats.
            if field == "crown" and any(kind != "无冠" for kind in values):
                values.pop("无冠", None)
            top = values.most_common(2)
            if top and top[0][1] >= 2 and (len(top) == 1 or top[0][1] > top[1][1]):
                result[field] = top[0][0]

        hook = {field: value for (field, value), count in self.hook_labels.items() if count >= 2}
        hook_rank = hook.get("rarity")
        if hook_rank and result.get("rarity", hook_rank) == hook_rank:
            for field, value in hook.items():
                result.setdefault(field, value)
        self.reset_hook()
        for key, fields in (("nameImage", ("name", "rarity")), ("sizeImage", ("size", "crown"))):
            if all(field in result for field in fields):
                continue
            options = [sample[key] for sample in samples if key in sample
                       and all(sample.get(field, result.get(field)) == result.get(field)
                               for field in fields if field in result)]
            if not options:
                continue
            crop = max(options, key=lambda image: cv2.Laplacian(image, cv2.CV_32F).var())
            ok, encoded = cv2.imencode(".jpg", crop, [cv2.IMWRITE_JPEG_QUALITY, 93])
            if ok and encoded.size <= 8500:
                result[key] = b64encode(encoded).decode("ascii")
        return result


def recognition_marks(observation, strategy, now):
    marks = []

    def add(x, y, width, height, label, tone):
        marks.append([round(x / WIDTH, 4), round(y / HEIGHT, 4),
                      round(width / WIDTH, 4), round(height / HEIGHT, 4), label, tone])

    def control(center, width, height, label, tone="scan"):
        x, y = center
        add(x - width / 2, y - height / 2, width, height, label, tone)

    phase = observation.phase
    if phase == Phase.GEAR:
        if strategy.exit_started is not None and observation.close_available:
            control(GEAR_CLOSE, 56, 54, "退出钓点")
        elif observation.start_available:
            control(START, 410, 62, "开始钓鱼")
    elif phase == Phase.AIM:
        control(ACTION, 132, 132, "抛竿键")
        points = strategy.confirmed_fish(now)
        if observation.reticle is not None:
            blocked = observation.reticle_blocked
            label = "落点越界" if blocked else f"落点 · {len(points)} 个鱼标"
            control(observation.reticle, 119, 95, label, "alert" if blocked else "valid")
        else:
            add(415, 185, 1090, 543, "寻找落点", "scan")
        for x, y in points:
            add(x, y - 10, 0, 0, "", "scan")
        if strategy.target is not None:
            control(strategy.target, 2 * FISH_GROUP_RADIUS, 1.6 * FISH_GROUP_RADIUS, "抛竿区", "target")
    elif phase == Phase.WAIT:
        control(ACTION, 128, 128, "等待咬钩")
    elif phase == Phase.BITE:
        label = "刺鱼计时盘" if observation.hook_angle is None else f"刺鱼计时盘 · {observation.hook_angle:.0f}°"
        add(744, 59, 432, 215, label, "target")
        control(ACTION, 128, 128, "刺鱼键")
    elif phase == Phase.REEL:
        angle, low, high = observation.tension, observation.green_limit, observation.green_upper
        label, tone = "张力指针待识别", "scan"
        if angle is not None:
            if observation.red_limit is not None and angle <= observation.red_limit:
                zone, tone = "红区", "alert"
            elif low is not None and high is not None:
                if angle < low:
                    zone, tone = "红区边缘", "alert"
                elif angle > high:
                    zone, tone = "灰区", "target"
                else:
                    zone, tone = "绿区", "valid"
            else:
                zone = "区域待确认"
            if observation.release_reel:
                tone = "alert"
            label = f"张力指针 · {angle:.0f}° · {zone}"
            x, y = TENSION_CENTER
            direction = radians(angle)
            add(x + 96 * cos(direction), y - 96 * sin(direction), 0, 0, "", tone)
        add(847, 54, 226, 122, label, tone)
        fish = "鱼标待识别"
        if observation.fish_red:
            fish = "鱼标变红 · 松手"
        elif observation.fish_visible:
            fish = "鱼标正常"
        add(914, 216, 99, 60, fish, "alert" if observation.fish_red else "scan")
        if observation.release_reel or strategy.reel_guard:
            control(ACTION, 128, 128, "松开收线键", "alert")
        else:
            control(ACTION, 128, 128, "收线中" if 0 in strategy.held else "收线键")
    elif phase == Phase.RESULT and observation.continue_available:
        control(CONTINUE, 270, 64, "继续钓鱼")
    return marks


def recognition_event(observation, strategy, now):
    if observation.phase == Phase.AIM and observation.reticle is not None and not observation.reticle_blocked:
        count = len(strategy.confirmed_fish(now))
        return f"发现 {count} 个鱼标" if count else ""
    if observation.phase == Phase.BITE:
        return "识别到刺鱼计时盘"
    if observation.phase == Phase.REEL and observation.green_limit is not None:
        return "识别到张力表"
    if observation.phase == Phase.MISS:
        return "识别到脱钩"
    return ""


def run(rod, bait, source, write):
    vision = FishingVision()
    catch = CatchFrames()
    gestures = []
    strategy = FishingStrategy(gestures.append, rod, bait)
    strategy.start(monotonic())
    write({"ready": True})
    while strategy.active:
        header = source.read(HEADER.size)
        if not header:
            return
        sequence, width, height = HEADER.unpack(header)
        if width % 2 or height % 2 or not 2 <= width <= 4096 or not 2 <= height <= 2400:
            raise ValueError("Invalid YUV frame geometry")
        size = width * height * 3 // 2
        data = source.read(size)
        if len(data) != size:
            return
        yuv = np.frombuffer(data, dtype=np.uint8).reshape(height * 3 // 2, width)
        frame = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_I420)
        now = monotonic()
        observation = vision.observe(frame, now)
        caught_before = strategy.caught
        result_ready = not strategy.cast_live or catch.update(frame, observation.phase, vision, now)
        strategy.update(observation, now, result_ready)
        reply = {
            "id": sequence, "active": strategy.active, "status": strategy.status, "caught": strategy.caught,
            "completed": strategy.completed,
            "touches": [[g.pointer, g.action, g.point[0] / WIDTH, g.point[1] / HEIGHT] for g in gestures],
            "marks": recognition_marks(observation, strategy, now) if strategy.active else [],
            "recognition": recognition_event(observation, strategy, now) if strategy.active else "",
        }
        if strategy.caught > caught_before:
            reply["catch"] = catch.take()
        elif observation.phase == strategy.phase and observation.phase in (Phase.AIM, Phase.GEAR, Phase.MISS):
            catch.reset_hook()
        write(reply)
        gestures.clear()


def replay(path):
    capture = cv2.VideoCapture(str(path))
    if not capture.isOpened():
        raise RuntimeError(f"无法打开录像 {path}")
    vision = FishingVision()
    stride = max(1, round(capture.get(cv2.CAP_PROP_FPS) / 20))
    counts = Counter()
    last = previous = Phase.UNKNOWN
    try:
        for index in range(round(capture.get(cv2.CAP_PROP_FRAME_COUNT))):
            if not capture.grab():
                break
            if index % stride:
                continue
            ok, frame = capture.retrieve()
            if not ok:
                break
            time = capture.get(cv2.CAP_PROP_POS_MSEC) / 1000
            seen = vision.observe(frame, time)
            if seen.phase == previous and seen.phase != last:
                last = seen.phase
                if last != Phase.UNKNOWN:
                    print(f"{time:7.2f}s  {PHASE_LABELS[last]}", flush=True)
            previous = seen.phase
            if seen.phase == Phase.AIM and seen.fish_points:
                counts["检测到鱼标"] += 1
            if seen.phase == Phase.BITE and seen.hook_angle is not None:
                counts["读到咬钩指针"] += 1
            if seen.phase == Phase.REEL:
                counts["鱼标红色"] += int(seen.fish_red)
                counts["张力全绿"] += int(seen.full_green)
                counts["左摆提示"] += int(seen.steer == -1)
                counts["右摆提示"] += int(seen.steer == 1)
    finally:
        capture.release()
    for label, count in counts.items():
        print(f"{label}  {count} 帧")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rod", type=int, choices=range(len(RODS) + 1), default=0)
    parser.add_argument("--bait", type=int, choices=range(len(BAITS) + 1), default=0)
    parser.add_argument("--replay", type=Path, help="离线检查录像识别")
    args = parser.parse_args()
    if args.replay:
        replay(args.replay)
    else:
        run(args.rod, args.bait, sys.stdin.buffer, lambda result: print(json.dumps(result), flush=True))


if __name__ == "__main__":
    main()
