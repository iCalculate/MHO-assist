"""Read the fishing HUD and the pale fish markers in the overhead view."""

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

import cv2
import numpy as np


WIDTH, HEIGHT = 1920, 862
Point = tuple[float, float]

RODS = ((1430, 240), (1535, 240), (1640, 240))
BAITS = ((1430, 389), (1535, 389), (1640, 389), (1745, 389), (1430, 494))
START = (1599, 793)
GEAR_CLOSE = (1764, 42)
CONTINUE = (1669, 793)
RETURN = (286, 185)
ACTION = (1594, 673)
STICK = (333, 639)

CATCH_NAMES = {
    "catch_great_whetfish": "大磨刀鰺鱼",
    "catch_glutton_tuna_king": "贪吃鲔鱼王",
    "catch_glutton_tuna": "贪吃鲔鱼",
}


class Phase(Enum):
    UNKNOWN = auto()
    GEAR = auto()
    AIM = auto()
    WAIT = auto()
    BITE = auto()
    REEL = auto()
    RESULT = auto()
    MISS = auto()


@dataclass(frozen=True)
class Observation:
    phase: Phase = Phase.UNKNOWN
    rod: int | None = None
    bait: int | None = None
    start_available: bool = False
    close_available: bool = False
    continue_available: bool = False
    return_available: bool = False
    reticle: Point | None = None
    reticle_blocked: bool = False
    fish_points: tuple[Point, ...] = ()
    hook_angle: float | None = None
    hook_zone: tuple[float, float] | None = None
    fish_red: bool = False
    fish_visible: bool = False
    release_reel: bool = False
    full_green: bool = False
    tension: float | None = None
    green_limit: float | None = None
    steer: int = 0


def _polar(cx, cy, first_radius, last_radius):
    angles = np.deg2rad(np.arange(181))
    radii = np.arange(first_radius, last_radius + 1)[:, None]
    x = np.rint(cx + radii * np.cos(angles)).astype(np.int32)
    y = np.rint(cy - radii * np.sin(angles)).astype(np.int32)
    return y, x


_HOOK_ARC = _polar(960, 267, 126, 199)
_TENSION_ARC = _polar(960, 165, 34, 103)
_RING_ANGLES = np.linspace(0, 2 * np.pi, 48, endpoint=False)
_RING_AXES = np.array([(radius, radius * aspect) for radius in range(33, 70, 4)
                       for aspect in (0.53, 0.63, 0.73, 0.83)])


def _ring_offsets(scale):
    x = _RING_AXES[:, 0, None] * np.cos(_RING_ANGLES)
    y = _RING_AXES[:, 1, None] * np.sin(_RING_ANGLES)
    return np.rint(x * scale).astype(np.intp), np.rint(y * scale).astype(np.intp)


_RING_X, _RING_Y = _ring_offsets(1.0)
_INNER_X, _INNER_Y = _ring_offsets(0.66)
_OUTER_X, _OUTER_Y = _ring_offsets(1.30)
_RING_PAD = 91
_RING_TOLERANCE = np.ones((9, 9), np.uint8)
_GLOW_Y, _GLOW_X = np.mgrid[-11:12, -10:11].astype(np.float32)
# The marker has a bright lower tip and a narrower, dimmer glow above it.
_FISH_GLOW = (np.exp(-0.5 * ((_GLOW_X / 3.8) ** 2 + (_GLOW_Y / 3.6) ** 2))
              + 0.72 * np.exp(-0.5 * ((_GLOW_X / 2.6) ** 2 + ((_GLOW_Y + 7.8) / 5.2) ** 2)))


def _green(hsv):
    h, s, v = np.moveaxis(hsv, -1, 0)
    return (h >= 31) & (h <= 65) & (s >= 118) & (v >= 137)


def _red(hsv):
    h, s, v = np.moveaxis(hsv, -1, 0)
    return ((h <= 10) | (h >= 166)) & (s >= 91) & (v >= 125)


def _gold(hsv):
    h, s, v = np.moveaxis(hsv, -1, 0)
    return (h >= 14) & (h <= 37) & (s >= 134) & (v >= 167)


def _needle(gray, hsv, segments, minimum, gap, edge_penalty=0):
    # The needle is narrow and spans several radial bands. Compare it with
    # both adjacent angles so that a boundary between colors cannot act as a needle.
    value = gray.astype(np.float32)
    left = np.roll(value, gap, axis=1)
    right = np.roll(value, -gap, axis=1)
    ridge = value - (left + right) * 0.5 - edge_penalty * np.abs(left - right) * 0.5
    scores = np.array([np.mean(ridge[a:b], axis=0) for a, b in segments])
    score = scores.mean(axis=0) + np.clip(scores.min(axis=0), -20, 15)
    score[np.median(hsv[segments[0][0]:segments[0][1], :, 1], axis=0) > 143] -= 24
    score[:gap + 1] = score[-gap - 1:] = -np.inf
    angle = int(np.argmax(score))
    if score[angle] < minimum or (scores[:, angle] > 4).sum() < 2:
        return None
    return float(angle)


def _hook_meter(frame, gray):
    hsv = cv2.cvtColor(frame[_HOOK_ARC], cv2.COLOR_BGR2HSV)
    gold = _gold(hsv[26:51]).mean(axis=0) >= 0.55
    # Close the narrow gap where the needle crosses the yellow band.
    band = cv2.morphologyEx(gold.astype(np.uint8)[None, :], cv2.MORPH_CLOSE, np.ones((1, 7), np.uint8))[0]
    spans = np.flatnonzero(np.diff(np.r_[0, band.astype(int), 0])).reshape(-1, 2)
    if not len(spans):
        return None, None
    first, end = max(spans, key=lambda span: span[1] - span[0])
    if end - first < 15 or end - first > 115 or not 10 < first < end < 171:
        return None, None
    angle = _needle(gray[_HOOK_ARC], hsv, ((8, 21), (25, 45), (55, 69)), 16, 3)
    return angle, (float(first), float(end - 1))


def _tension_meter(frame, gray):
    hsv = cv2.cvtColor(frame[_TENSION_ARC], cv2.COLOR_BGR2HSV)
    green = _green(hsv[38:46]).mean(axis=0) > 0.55
    if green[50:133].sum() < 26:
        return None, None, False
    full = green[10:171].mean() > 0.86 and green[10:45].mean() > 0.80 and green[135:171].mean() > 0.80
    angle = _needle(gray[_TENSION_ARC], hsv, ((8, 27), (30, 44), (50, 69)), 9, 4, edge_penalty=0.48)
    right_green = np.flatnonzero(green[:92])
    limit = float(np.percentile(right_green, 10)) if len(right_green) >= 10 else None
    return angle, limit, bool(full)


def _reticle_from_arcs(mask, value, minimum_value):
    contours, _ = cv2.findContours(mask, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    centers = []
    for contour in contours:
        x, y, width, height = cv2.boundingRect(contour)
        if not (8 <= width <= 38 and 5 <= height <= 32 and cv2.contourArea(contour) >= width * height * 0.27):
            continue
        cx, cy = x + width // 2, y + height // 2
        if value[cy, cx] >= minimum_value:
            centers.append((cx, cy))
    if not centers:
        return None

    pad = _RING_PAD
    raw = cv2.copyMakeBorder(mask, pad, pad, pad, pad, cv2.BORDER_CONSTANT)
    band = cv2.dilate(raw, _RING_TOLERANCE)
    choices = []
    for cx, cy in centers:
        ox, oy = cx + pad, cy + pad
        # The bright central marker stays visible when the moving arcs are split.
        # Check three quadrants and the gaps inside and outside the ring to reject glare.
        quadrants = (band[oy + _RING_Y, ox + _RING_X] > 0).reshape(-1, 4, 12).mean(axis=2)
        ring = quadrants.mean(axis=1)
        inside = (raw[oy + _INNER_Y, ox + _INNER_X] > 0).mean(axis=1)
        outside = (raw[oy + _OUTER_Y, ox + _OUTER_X] > 0).mean(axis=1)
        valid = (ring >= 0.64) & ((quadrants >= 0.42).sum(axis=1) >= 3) & (inside <= 0.27) & (outside <= 0.32)
        if valid.any():
            score = (ring - 0.35 * (inside + outside))[valid].max()
            choices.append((score, (cx, cy)))
    return max(choices, default=(0, None))[1]


def _closed_reticle(mask):
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, _RING_TOLERANCE)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    choices = []
    for contour in contours:
        u, v, width, height = cv2.boundingRect(contour)
        area = cv2.contourArea(cv2.convexHull(contour))
        if 50 <= width <= 151 and 28 <= height <= 110 and 1.12 <= width / height <= 1.95:
            middle = mask[v + height // 3:v + height * 2 // 3, u + width // 3:u + width * 2 // 3]
            if area > width * height * 0.63 and np.count_nonzero(middle) >= 13:
                choices.append((area, (u + width / 2, v + height / 2)))
    return max(choices, default=(0, None))[1]


def _reticle(frame):
    x, y = 415, 185
    hsv = cv2.cvtColor(frame[y:728, x:1505], cv2.COLOR_BGR2HSV)
    value = hsv[:, :, 2]
    red = _red(hsv).astype(np.uint8)
    # A supported red ring takes priority over unrelated gold reflections.
    blocked = _reticle_from_arcs(red, value, 150) if np.count_nonzero(red) >= 70 else None
    if blocked:
        return (x + blocked[0], y + blocked[1]), True

    gold = cv2.inRange(hsv, (17, 88, 153), (38, 255, 255))
    center = _closed_reticle(gold)
    if center is None:
        gold = cv2.inRange(hsv, (16, 77, 145), (41, 255, 255))
        center = _reticle_from_arcs(gold, value, 196)
    return ((x + center[0], y + center[1]) if center else None), False


def _fish_points(frame, gray, reticle):
    # Fish carry a small blue-white glow above the body. Use its vertical
    # footprint and contrast to reject the broader patches of water glare.
    x, y = 370, 214
    roi = frame[y:728, x:1520]
    light = gray[y:728, x:1520].astype(np.float32)
    local = cv2.GaussianBlur(light, (0, 0), 2.0) - cv2.GaussianBlur(light, (0, 0), 7.5)
    peaks = (local == cv2.dilate(local, _RING_TOLERANCE)) & (local >= 21)
    blue = roi[:, :, 0].astype(np.int16) - roi[:, :, 2].astype(np.int16)
    ys, xs = np.nonzero(peaks & (blue >= 6) & (light < 186))
    ordered = sorted(zip(ys, xs), key=lambda p: local[p], reverse=True)[:40]
    points = []
    for v, u in ordered:
        if not (11 <= v < light.shape[0] - 12 and 10 <= u < light.shape[1] - 10):
            continue
        dx, dy = x + u - reticle[0], y + v - reticle[1]
        if abs((dx / 51) ** 2 + (dy / 35) ** 2 - 1) < 0.29 or (abs(dx) < 14 and abs(dy) < 14):
            continue
        core = local[v - 8:v + 9, u - 3:u + 4]
        sides = np.r_[local[v - 7:v + 8, u - 10:u - 5].ravel(), local[v - 7:v + 8, u + 6:u + 11].ravel()]
        if np.mean(core > local[v, u] * 0.42) < 0.42 or np.percentile(sides, 85) > local[v, u] * 0.60:
            continue
        crop = local[v - 11:v + 12, u - 10:u + 11]
        similarity = cv2.matchTemplate(crop, _FISH_GLOW, cv2.TM_CCOEFF_NORMED)[0, 0]
        minimum_similarity = 0.83 if local[v, u] >= 27 else 0.90
        if similarity < minimum_similarity:
            continue
        target = (float(x + u), float(y + v + 10))
        if all((target[0] - p[0]) ** 2 + (target[1] - p[1]) ** 2 > 25 ** 2 for p in points):
            points.append(target)
    return tuple(points[:14])


def _selected(frame, cells):
    for index, (x, y) in enumerate(cells, start=1):
        hsv = cv2.cvtColor(frame[y - 43:y - 18, x + 12:x + 45], cv2.COLOR_BGR2HSV)
        if _green(hsv).sum() >= 46:
            return index
    return None


def _active_button(frame, point):
    x, y = point
    hsv = cv2.cvtColor(frame[y + 8:y + 21, x - 79:x + 80], cv2.COLOR_BGR2HSV)
    return _gold(hsv).mean() > 0.27


class FishingVision:
    def __init__(self):
        with np.load(Path(__file__).with_name("templates.npz"), allow_pickle=False) as templates:
            self.templates = {
                key: (templates[key], *map(int, templates[key + "_xy"]))
                for key in templates.files if not key.endswith("_xy")
            }

    def score(self, gray, key, margin=9):
        patch, x, y = self.templates[key]
        h, w = patch.shape
        region = gray[y - margin:y + h + margin, x - margin:x + w + margin]
        return float(cv2.matchTemplate(region, patch, cv2.TM_CCOEFF_NORMED).max())

    def _match_catch_label(self, region, key):
        signature = self.templates.get(key)
        if signature is None:
            return 0.0, 0, 0
        patch = signature[0]
        matches = cv2.matchTemplate(region, patch, cv2.TM_CCOEFF_NORMED)
        _, confidence, _, (left, _) = cv2.minMaxLoc(matches)
        return confidence, left, left + patch.shape[1]

    def catch_label(self, frame):
        if frame.shape[:2] != (HEIGHT, WIDTH):
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_LINEAR)
        label = cv2.cvtColor(frame[543:587, 179:576], cv2.COLOR_BGR2GRAY)
        ranked = sorted(((self._match_catch_label(label, key)[0], name) for key, name in CATCH_NAMES.items()),
                        reverse=True)
        confidence, name = ranked[0]
        if confidence >= 0.86 and confidence - ranked[1][0] >= 0.07:
            return name, None

        rank = max(self._match_catch_label(label, key) for key in
                   ("catch_rank_good", "catch_rank_rare", "catch_rank_excellent"))
        left = rank[2] + 3 if rank[0] >= 0.77 else 0
        midpoint = label.shape[1] // 2
        new = self._match_catch_label(label[:, midpoint:], "catch_new")
        right = midpoint + new[1] - 3 if new[0] >= 0.78 else label.shape[1]
        return "", label[:, left:right] if right - left >= 40 else label

    def observe(self, frame):
        height, width = frame.shape[:2]
        if abs(width / height / (WIDTH / HEIGHT) - 1) > 0.035:
            return Observation()
        if (width, height) != (WIDTH, HEIGHT):
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_LINEAR)
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        if self.score(gray, "gear") > 0.65:
            return Observation(
                Phase.GEAR, rod=_selected(frame, RODS), bait=_selected(frame, BAITS),
                start_available=self.score(gray, "start") > 0.70 and _active_button(frame, START),
                close_available=self.score(gray, "gear_close", 6) > 0.73,
            )
        if self.score(gray, "exit") > 0.69 and self.score(gray, "caught") > 0.56:
            return Observation(
                Phase.RESULT, continue_available=self.score(gray, "continue") > 0.69 and _active_button(frame, CONTINUE),
            )

        release = self.score(gray, "release") > 0.73
        hold = self.score(gray, "hold") > 0.65
        if release or hold or self.score(gray, "reel") > 0.72:
            angle, limit, full = _tension_meter(frame, gray)
            # The needle starts flush with the left endpoint. The polar ridge
            # reader excludes that border, so verify the visible tip separately.
            if angle is None and limit is not None and hold and not release:
                if self.score(gray, "tension_left_tip", 5) > 0.78:
                    angle = 180.0
            red = self.score(gray, "red_fish") > 0.63 and _red(
                cv2.cvtColor(frame[228:264, 924:1001], cv2.COLOR_BGR2HSV)
            ).sum() > 95
            fish = self.score(gray, "fish") > 0.63 or (full and self.score(gray, "glint_fish") > 0.58)
            if red or fish or limit is not None:
                left = self.score(gray, "left", 15)
                right = self.score(gray, "right", 15)
                steer = -1 if left > 0.64 and left > right + 0.10 else 1 if right > 0.64 and right > left + 0.10 else 0
                return Observation(
                    Phase.REEL, fish_red=bool(red), fish_visible=bool(fish), release_reel=release,
                    full_green=full, tension=angle, green_limit=limit, steer=steer,
                )

        if self.score(gray, "cast") > 0.67:
            reticle, blocked = _reticle(frame)
            points = _fish_points(frame, gray, reticle) if reticle and not blocked else ()
            return Observation(
                Phase.AIM, return_available=self.score(gray, "return") > 0.67,
                reticle=reticle, reticle_blocked=blocked, fish_points=points,
            )

        if self.score(gray, "hook") > 0.66:
            needle, zone = _hook_meter(frame, gray)
            if zone is not None:
                return Observation(Phase.BITE, hook_angle=needle, hook_zone=zone)
            return Observation(Phase.WAIT, return_available=self.score(gray, "return") > 0.67)

        if self.score(gray, "miss") > 0.71:
            return Observation(Phase.MISS)
        return Observation()
