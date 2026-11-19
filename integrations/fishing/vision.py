"""Read the fishing HUD and the pale fish markers in the overhead view."""

from collections import deque
from dataclasses import dataclass
from enum import Enum, auto
from math import hypot
from pathlib import Path
from time import monotonic

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
TENSION_CENTER = (960, 165)

CATCH_NAMES = {
    "catch_great_whetfish": "大磨刀鰺鱼",
    "catch_glutton_tuna_king": "贪吃鲔鱼王",
    "catch_glutton_tuna": "贪吃鲔鱼",
}
CATCH_RARITIES = {
    "catch_rank_good": "精良",
    "catch_rank_rare": "稀有",
    "catch_rank_excellent": "卓越",
}


def _crown_silhouette(points):
    image = np.zeros((16, 24), np.uint8)
    cv2.fillPoly(image, [np.array(points, np.int32)], 1)
    return image.astype(bool)


# Compare the filled outline so the jewel and metallic highlights can vary
# independently. The lower profile covers the small-size crown.
_CROWN_PROFILES = (
    _crown_silhouette([(0, 3), (6, 8), (11, 0), (17, 8), (23, 3), (20, 15), (3, 15)]),
    _crown_silhouette([(0, 1), (7, 7), (12, 3), (16, 7), (23, 1), (20, 15), (3, 15)]),
)


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
    view_stable: bool = True
    fish_pending: bool = False
    hook_angle: float | None = None
    hook_zone: tuple[float, float] | None = None
    fish_red: bool = False
    fish_visible: bool = False
    release_reel: bool = False
    full_green: bool = False
    tension: float | None = None
    green_limit: float | None = None
    green_upper: float | None = None
    red_limit: float | None = None
    fish_pressure: float | None = None
    steer: int = 0


@dataclass(frozen=True)
class TensionMeter:
    angle: float | None = None
    green_zone: tuple[float, float] | None = None
    red_limit: float | None = None
    fish_pressure: float | None = None
    full_green: bool = False


def _polar(cx, cy, first_radius, last_radius):
    angles = np.deg2rad(np.arange(181))
    radii = np.arange(first_radius, last_radius + 1)[:, None]
    x = np.rint(cx + radii * np.cos(angles)).astype(np.int32)
    y = np.rint(cy - radii * np.sin(angles)).astype(np.int32)
    return y, x


_HOOK_ARC = _polar(960, 267, 126, 199)
_TENSION_ARC = _polar(*TENSION_CENTER, 34, 103)
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


def _fish_glow(scale):
    # The lower dot is bright, with a slimmer glow extending above it.
    height, width = round(11 * scale), round(10 * scale)
    y, x = np.mgrid[-height:height + 1, -width:width + 1].astype(np.float32) / scale
    return (np.exp(-0.5 * ((x / 3.8) ** 2 + (y / 3.6) ** 2))
            + 0.72 * np.exp(-0.5 * ((x / 2.6) ** 2 + ((y + 7.8) / 5.2) ** 2)))


_FISH_GLOWS = tuple(_fish_glow(scale) for scale in (0.78, 1.0, 1.24))


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


def _band_runs(band, merge_gap):
    closed = cv2.morphologyEx(band.astype(np.uint8)[None, :], cv2.MORPH_CLOSE,
                              np.ones((1, merge_gap), np.uint8))[0]
    return np.flatnonzero(np.diff(np.r_[0, closed.astype(int), 0])).reshape(-1, 2)


def _tension_shaft_peak(plane, minimum_band):
    left, right = np.roll(plane, 4, axis=1), np.roll(plane, -4, axis=1)
    ridge = plane - (left + right) * 0.5 - 0.22 * np.abs(left - right)
    first, second = ridge[8:20].mean(axis=0), ridge[20:33].mean(axis=0)
    score = (first + second) * 0.5 + 0.4 * np.minimum(first, second)
    score += 0.1 * np.clip(ridge[34:38].mean(axis=0), -5, 20)
    score[(first < minimum_band) | (second < minimum_band)] = -np.inf
    score[:4] = score[-4:] = -np.inf
    angle = int(np.argmax(score))
    return float(angle), float(score[angle])


def _tension_meter(frame, gray, release=False):
    hsv = cv2.cvtColor(frame[_TENSION_ARC], cv2.COLOR_BGR2HSV)
    green = _green(hsv[38:46]).mean(axis=0) > 0.55
    if green[50:133].sum() < 26:
        return TensionMeter()
    full = bool(green[10:171].mean() > 0.86 and green[10:45].mean() > 0.80 and green[135:171].mean() > 0.80)
    first, end = max(_band_runs(green, 13), key=lambda run: run[1] - run[0], default=(0, 0))
    green_zone = (0.0, 180.0) if full else None
    if not full and end - first >= 32:
        green_zone = float(first), float(end - 1)

    # Fit the shaft inside the colored dial so the green/red seam and the
    # moving outer arc cannot stand in for the needle.
    angle, score = _tension_shaft_peak(gray[_TENSION_ARC].astype(np.float32), 6)
    red_plane = hsv[:, :, 1].astype(np.float32) * _red(hsv)
    red_angle, red_score = _tension_shaft_peak(red_plane, 12)
    if red_score >= 27 and (release or (red_angle <= 75 and red_score > 1.38 * max(20, score))):
        angle, score = red_angle, red_score
    if score < 21:
        angle = None

    inner_red = _red(hsv[38:46]).mean(axis=0) > 0.55
    red_limit = max((float(end - 1) for first, end in _band_runs(inner_red, 9)
                     if first <= 12 and 19 <= end - first <= 90), default=None)
    outer_red = _red(hsv[61:69]).mean(axis=0) >= 0.28
    # This strip grows from the right of the outer ring. Requiring that
    # anchor avoids mistaking a red object behind the dial for fish pressure.
    pressure = max((float(end - 1) for first, end in _band_runs(outer_red, 19)
                    if first <= 34 and end - first >= 11 and outer_red[16:36].sum() >= 6), default=None)
    return TensionMeter(angle, green_zone, red_limit, pressure, full)


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
    if center is None:
        # Over some blue ponds the gold ring blends toward pale cream. Keep
        # the center and ring support checks when accepting these weaker colors.
        pale_gold = cv2.inRange(hsv, (12, 45, 160), (43, 255, 255))
        center = _reticle_from_arcs(pale_gold, value, 190)
    return ((x + center[0], y + center[1]) if center else None), False


def _fish_candidates(frame, gray, reticle):
    x, y = 370, 214
    light = gray[y:728, x:1520].astype(np.float32)
    local = cv2.GaussianBlur(light, (0, 0), 2.0) - cv2.GaussianBlur(light, (0, 0), 7.5)
    # Scale the cutoff with nearby water texture. The same marker can be dim
    # against dark water or nearly white against a bright reflection.
    texture = cv2.blur(np.abs(local), (27, 31))
    strength = local / (6.0 + 1.7 * texture)
    peaks = (local == cv2.dilate(local, _RING_TOLERANCE)) & (local >= 3.7) & (strength >= 0.48)
    ys, xs = np.nonzero(peaks)
    ordered = sorted(zip(ys, xs), key=lambda p: strength[p], reverse=True)[:64]
    strong, weak = [], []
    for v, u in ordered:
        if not (16 <= v < light.shape[0] - 16 and 15 <= u < light.shape[1] - 15):
            continue
        if reticle is not None:
            dx, dy = x + u - reticle[0], y + v - reticle[1]
            if abs(dx) < 13 and abs(dy) < 13:
                continue
            if 0.50 < (dx / 51) ** 2 + (dy / 35) ** 2 < 1.72:
                blue, _, red = map(int, frame[y + v, x + u])
                if red - blue > 47:
                    continue
        core = local[v - 8:v + 9, u - 3:u + 4]
        if (core > local[v, u] * 0.35).mean() < 0.42:
            continue
        similarities = []
        for glow in _FISH_GLOWS:
            half_height, half_width = (length // 2 for length in glow.shape)
            crop = local[v - half_height - 2:v + half_height + 3, u - half_width - 2:u + half_width + 3]
            similarities.append(float(cv2.matchTemplate(crop, glow, cv2.TM_CCOEFF_NORMED).max()))
        # In textured water require a closer match, including the upper glow
        # and dimmer sides, before passing a point to the multi-frame tracker.
        similarity = max(similarities)
        target = (float(x + u), float(y + v + 10))
        if local[v, u] >= 9 and strength[v, u] >= 1.15 and similarity >= (0.88 if strength[v, u] >= 1.7 else 0.92):
            strong.append(target)
        elif similarity >= (0.86 if strength[v, u] >= 0.72 else 0.89):
            weak.append(target)
    return strong[:20], weak[:28]


def _fish_motion(point, recent, now):
    x, y = point
    last_seen, distance, hits = now, 0.0, 1
    vx = vy = 0.0
    for timestamp, candidates in reversed(recent):
        gap = last_seen - timestamp
        if gap < 0.065:
            continue
        if gap > 0.25:
            break
        predicted_x, predicted_y = x + vx * gap, y + vy * gap
        nearby = ((hypot(predicted_x - px, predicted_y - py), px, py) for px, py in candidates
                  if hypot(x - px, y - py) <= 3 + 68 * gap)
        error, px, py = min(nearby, default=(float("inf"), x, y))
        if error > 3 + (68 if hits == 1 else 41) * gap:
            continue
        distance += hypot(x - px, y - py)
        vx, vy = (px - x) / gap, (py - y) / gap
        hits += 1
        x, y, last_seen = px, py, timestamp
    displacement = hypot(x - point[0], y - point[1])
    # Require a compact marker to travel consistently. A flicker, a stationary
    # reflection or a patch that jumps between crests cannot confirm a fish.
    if hits >= 4 and now - last_seen >= 0.30 and displacement >= max(4.5, 0.59 * distance):
        return 2
    if hits >= 3 and now - last_seen >= 0.17 and displacement >= max(4, 0.72 * distance):
        return 1
    return 0


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


def _catch_crown(size_line):
    hsv = cv2.cvtColor(size_line, cv2.COLOR_BGR2HSV)
    hue, saturation, value = np.moveaxis(hsv, -1, 0)
    gold = (hue >= 13) & (hue <= 43) & (saturation >= 68)
    silver = saturation <= 54
    bright_metal = ((gold | silver) & (value >= 156)).astype(np.uint8)
    connected = cv2.morphologyEx(bright_metal, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
    contours, _ = cv2.findContours(connected, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    center = size_line.shape[1] / 2
    hits = []
    ambiguous = False
    for outline in contours:
        x, y, width, height = cv2.boundingRect(outline)
        if not (17 <= width <= 56 and 12 <= height <= 36 and 1.12 <= width / height <= 2.65):
            continue
        if not 22 <= abs(x + width / 2 - center) <= 110 or y == 0 or y + height == size_line.shape[0]:
            continue
        mask = np.zeros((height, width), np.uint8)
        cv2.drawContours(mask, [outline - (x, y)], 0, 1, cv2.FILLED)
        shape = cv2.resize(mask, (24, 16), interpolation=cv2.INTER_NEAREST).astype(bool)
        score = max(np.count_nonzero(shape & sample) / np.count_nonzero(shape | sample)
                    for sample in _CROWN_PROFILES)
        pixels = mask.astype(bool) & (value[y:y + height, x:x + width] >= 156)
        if score < 0.65 or np.count_nonzero(pixels) < width * height * 0.20:
            continue
        ambiguous = True
        gold_fraction = gold[y:y + height, x:x + width][pixels].mean()
        silver_fraction = silver[y:y + height, x:x + width][pixels].mean()
        # A bright outline on its own is insufficient. Require one metal tone
        # to dominate the pixels inside the crown profile.
        if score >= 0.78 and max(gold_fraction, silver_fraction) >= 0.68:
            kind = "金冠" if gold_fraction > silver_fraction else "银冠"
            hits.append((score, kind, x, x + width))
    if not hits:
        # The size is centered when the crown is absent. Require legible gold
        # glyphs before calling an empty icon slot, since the line fades in.
        columns = np.flatnonzero(np.count_nonzero(gold & (value >= 174), axis=0) >= 3)
        if not ambiguous and len(columns) >= 13 and abs((columns[0] + columns[-1]) / 2 - center) <= 12:
            return "无冠", 0, 0
        return "", 0, 0
    _, kind, left, right = max(hits)
    return kind, left, right


class FishingVision:
    def __init__(self):
        self._last_view = None
        self._fish_frames = deque(maxlen=24)
        self._swimmers = []
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
        if patch.shape[0] > region.shape[0] or patch.shape[1] > region.shape[1]:
            return 0.0, 0, 0
        matches = cv2.matchTemplate(region, patch, cv2.TM_CCOEFF_NORMED)
        _, confidence, _, (left, _) = cv2.minMaxLoc(matches)
        return confidence, left, left + patch.shape[1]

    def hook_details(self, frame):
        if frame.shape[:2] != (HEIGHT, WIDTH):
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_LINEAR)
        label = cv2.cvtColor(frame[281:320, 760:1160], cv2.COLOR_BGR2GRAY)
        rank, _, right = self._match_catch_label(label, "hook_rank_normal")
        if rank < 0.83:
            return {}
        details = {"rarity": "普通"}
        # The fish name begins immediately to the right of the rank badge.
        name = self._match_catch_label(label[:, right:right + 101], "hook_whetfish")[0]
        if name >= 0.85:
            details["name"] = "磨刀鰺鱼"
        return details

    def catch_details(self, frame):
        if frame.shape[:2] != (HEIGHT, WIDTH):
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_LINEAR)
        label = cv2.cvtColor(frame[543:587, 179:576], cv2.COLOR_BGR2GRAY)
        ranked = sorted(((self._match_catch_label(label, key)[0], name) for key, name in CATCH_NAMES.items()),
                        reverse=True)
        confidence, name = ranked[0]
        details = {}
        if confidence >= 0.86 and confidence - ranked[1][0] >= 0.07:
            details["name"] = name

        rank, rarity = max((self._match_catch_label(label, key), rarity) for key, rarity in CATCH_RARITIES.items())
        if rank[0] >= 0.77:
            details["rarity"] = rarity
        if "name" not in details or "rarity" not in details:
            left = rank[2] + 3 if "rarity" in details else 0
            midpoint = label.shape[1] // 2
            new = self._match_catch_label(label[:, midpoint:], "catch_new")
            right = midpoint + new[1] - 3 if new[0] >= 0.78 else label.shape[1]
            details["nameImage"] = label[:, left:right] if right - left >= 40 else label

        size_line = frame[589:629, 222:532]
        crown, left, right = _catch_crown(size_line)
        if crown:
            details["crown"] = crown
        if right:
            if left + right < size_line.shape[1]:
                size_line = size_line[:, right + 3:]
            else:
                size_line = size_line[:, :left - 3]
        size_image = cv2.cvtColor(size_line, cv2.COLOR_BGR2GRAY)
        if self._match_catch_label(size_image, "catch_size_giant")[0] >= 0.83:
            details["size"] = "巨型种"
        else:
            details["sizeImage"] = size_image
        return details

    def _observe_fish(self, frame, gray, reticle, previous, now):
        water = cv2.resize(gray[239:698, 450:1460], (48, 22), interpolation=cv2.INTER_AREA)
        self._last_view = (now, water, reticle)
        stable = False
        if previous is not None and reticle is not None:
            timestamp, previous_water, previous_reticle = previous
            stable = (previous_reticle is not None and 0 < now - timestamp <= 0.40
                      and hypot(reticle[0] - previous_reticle[0], reticle[1] - previous_reticle[1]) < 24
                      and (cv2.absdiff(water, previous_water) > 15).mean() < 0.28)
        if not stable:
            self._fish_frames.clear()
            self._swimmers.clear()
        while self._fish_frames and now - self._fish_frames[0][0] > 0.85:
            self._fish_frames.popleft()
        strong, weak = _fish_candidates(frame, gray, reticle)
        motion = [(point, _fish_motion(point, self._fish_frames, now)) for point in weak]
        moving = [point for point, score in motion if score == 2]
        self._fish_frames.append((now, strong + weak))
        # Keep a confirmed swimmer briefly when the reflection covers its marker.
        # Only newly matched tracks extend that time, so a depleted pond clears.
        fresh = strong + moving
        retained = [(point, seen) for point, seen in self._swimmers if now - seen <= 0.26
                    and all(hypot(point[0] - x, point[1] - y) > 18 for x, y in fresh)]
        self._swimmers = [(point, now) for point in moving] + retained
        points = []
        for x, y in fresh + [point for point, _ in retained]:
            if all(hypot(x - px, y - py) > 18 for px, py in points):
                points.append((x, y))
        return tuple(points[:18]), stable, any(score == 1 for _, score in motion)

    def observe(self, frame, now=None):
        previous = self._last_view
        self._last_view = None
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
            meter = _tension_meter(frame, gray, release)
            angle = meter.angle
            # The needle starts flush with the left endpoint. The polar ridge
            # reader excludes that border, so verify the visible tip separately.
            if (angle is None or angle >= 171) and meter.green_zone and hold and not release:
                if self.score(gray, "tension_left_tip", 5) > 0.78:
                    angle = 180.0
            red = self.score(gray, "red_fish") > 0.63 and _red(
                cv2.cvtColor(frame[228:264, 924:1001], cv2.COLOR_BGR2HSV)
            ).sum() > 95
            fish = self.score(gray, "fish") > 0.63 or (meter.full_green and self.score(gray, "glint_fish") > 0.58)
            if red or fish or meter.green_zone:
                left = self.score(gray, "left", 15)
                right = self.score(gray, "right", 15)
                steer = -1 if left > 0.64 and left > right + 0.10 else 1 if right > 0.64 and right > left + 0.10 else 0
                return Observation(
                    Phase.REEL, fish_red=bool(red), fish_visible=bool(fish), release_reel=release,
                    full_green=meter.full_green, tension=angle, red_limit=meter.red_limit,
                    green_limit=meter.green_zone[0] if meter.green_zone else None,
                    green_upper=meter.green_zone[1] if meter.green_zone else None,
                    fish_pressure=meter.fish_pressure, steer=steer,
                )

        if self.score(gray, "cast") > 0.67:
            reticle, blocked = _reticle(frame)
            points, stable, pending = self._observe_fish(frame, gray, reticle, previous, monotonic() if now is None else now)
            return Observation(
                Phase.AIM, return_available=self.score(gray, "return") > 0.67,
                reticle=reticle, reticle_blocked=blocked, fish_points=points, view_stable=stable, fish_pending=pending,
            )

        if self.score(gray, "hook") > 0.66:
            needle, zone = _hook_meter(frame, gray)
            if zone is not None:
                return Observation(Phase.BITE, hook_angle=needle, hook_zone=zone)
            return Observation(Phase.WAIT, return_available=self.score(gray, "return") > 0.67)

        if self.score(gray, "miss") > 0.71:
            return Observation(Phase.MISS)
        return Observation()
