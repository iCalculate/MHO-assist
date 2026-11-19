"""Convert confirmed HUD observations into bounded touch gestures."""

from dataclasses import dataclass
from math import hypot

from vision import ACTION, BAITS, CONTINUE, GEAR_CLOSE, RETURN, RODS, START, STICK, Phase, Point


SAFE_AIM = (960, 450)
FISH_GROUP_RADIUS = 118


PHASE_LABELS = {
    Phase.UNKNOWN: "等待钓鱼界面",
    Phase.GEAR: "选择鱼竿和鱼饵",
    Phase.AIM: "寻找鱼并校正落点",
    Phase.WAIT: "等待咬钩",
    Phase.BITE: "瞄准刺鱼时机",
    Phase.REEL: "读取张力和摆杆提示",
    Phase.RESULT: "收获结算",
    Phase.MISS: "刺鱼失败，等待返回",
}


@dataclass(frozen=True)
class Gesture:
    pointer: int
    action: str
    point: Point


@dataclass
class FishTrack:
    position: Point
    first_seen: float
    last_seen: float
    hits: int = 1


def _aim_distance(left, right):
    return hypot(left[0] - right[0], (left[1] - right[1]) * 1.25)


def choose_cast_area(points, reticle, blocked_area=None):
    def neighbours(center):
        return [point for point in points if _aim_distance(point, center) <= FISH_GROUP_RADIUS]

    candidates = [reticle]
    for point in points:
        group = neighbours(point)
        center = (sum(x for x, _ in group) / len(group), sum(y for _, y in group) / len(group))
        if blocked_area is None or _aim_distance(center, blocked_area) > 80:
            candidates.append(center)

    density, _, best = max((len(neighbours(center)), -_aim_distance(center, reticle), center)
                           for center in candidates)
    if density == 0:
        return None
    if len(neighbours(reticle)) >= max(2, density - 1):
        return reticle
    return best


class FishingStrategy:
    def __init__(self, emit, rod=0, bait=0):
        self.emit = emit
        self.rod = rod
        self.bait = bait
        self.active = False
        self.status = "已暂停"
        self.caught = 0
        self.phase = Phase.UNKNOWN
        self.target = None
        self.held = {}

    def start(self, now):
        self.active = True
        self.status = "等待钓鱼界面"
        self.phase = self.candidate = Phase.UNKNOWN
        self.frames = 0
        self.phase_since = self.last_seen = now
        self.attempts = {}
        self.tracks = []
        self.target = None
        self.exit_started = None
        self.completed = False
        self.empty_since = None
        self.no_bites = 0
        self.failures = 0
        self.cast_live = False
        self.struck = False
        self.reel_ready_at = now
        self.wait_for_green = False
        self.steer_streak = 0
        self.last_steer = 0
        self.last_hook = None
        self.aim_nudges = 0
        self.aim_settle = now
        self.last_reticle_seen = now

    def stop(self, reason="已暂停"):
        self.active = False
        self.status = reason
        self.target = None
        self.release()

    def release(self, pointer=None):
        if pointer is None:
            for active in list(self.held):
                self.release(active)
        elif pointer in self.held:
            point, _ = self.held.pop(pointer)
            self.emit(Gesture(pointer, "up", point))

    def tick(self, now):
        for pointer, (_, until) in list(self.held.items()):
            if until is not None and now >= until:
                self.release(pointer)

    def _hold(self, pointer, point, until=None, origin=None):
        if pointer not in self.held:
            self.emit(Gesture(pointer, "down", origin or point))
            self.held[pointer] = (origin or point, until)
        previous, _ = self.held[pointer]
        if previous != point:
            self.emit(Gesture(pointer, "move", point))
        self.held[pointer] = (point, until)

    def _tap(self, point, now):
        if 0 in self.held:
            return False
        self._hold(0, point, now + 0.065)
        return True

    def _button(self, name, point, now):
        tries, last = self.attempts.get(name, (0, -1e6))
        if now - last < 1.2:
            return False
        if tries >= 3:
            self.stop("按钮未响应，已暂停")
            return False
        if not self._tap(point, now):
            return False
        self.attempts[name] = (tries + 1, now)
        return True

    def _enter(self, phase, now):
        if phase == Phase.GEAR and self.cast_live:
            self.failures += 1
            self.cast_live = False
            if self.failures >= 4:
                self.stop("连续四次未钓获，已暂停")
                return
        self.phase = phase
        self.phase_since = now
        self.attempts.clear()
        self.last_hook = None
        self.steer_streak = self.last_steer = 0
        self.target = None
        if phase == Phase.AIM:
            self.tracks.clear()
            self.aim_nudges = 0
            self.aim_settle = now + 0.40
            self.last_reticle_seen = now
            self.last_castable = None
            self.castable_frames = 0
            self.blocked_area = None
            self.recovery_steps = 0
            self.empty_since = None
            self.struck = False
        elif phase in (Phase.WAIT, Phase.BITE, Phase.REEL):
            self.cast_live = True
            if phase in (Phase.BITE, Phase.REEL):
                self.no_bites = 0
            if phase == Phase.REEL:
                self.wait_for_green = False
        elif phase == Phase.RESULT:
            self.caught += int(self.cast_live)
            self.no_bites = self.failures = 0
            self.cast_live = False

    def update(self, observation, now):
        self.tick(now)
        if not self.active:
            return
        phase = observation.phase
        if self.phase == Phase.REEL and (phase != Phase.REEL or observation.fish_red):
            self.release()
            self.reel_ready_at = now + 0.18
        if phase == Phase.UNKNOWN:
            self.candidate, self.frames = phase, 0
            self.empty_since = None
            self.castable_frames = 0
            if self.exit_started is not None:
                self._exit_pond(observation, now)
                return
            if now - self.last_seen > 12.0:
                self.stop("未识别到钓鱼界面，已暂停")
            elif now - self.last_seen > 0.45:
                self.status = "等待钓鱼界面恢复"
            return
        self.last_seen = now
        if phase != self.candidate:
            self.candidate, self.frames = phase, 0
        self.frames += 1
        if self.frames < 2:
            return
        if self.phase != phase:
            self.release()
            self._enter(phase, now)
        if not self.active:
            return
        if self.exit_started is not None:
            self._exit_pond(observation, now)
            return
        self.status = PHASE_LABELS[phase]
        if phase == Phase.GEAR:
            self._gear(observation, now)
        elif phase == Phase.AIM:
            self._aim(observation, now)
        elif phase == Phase.WAIT:
            self._wait(observation, now)
        elif phase == Phase.BITE:
            self._bite(observation, now)
        elif phase == Phase.REEL:
            self._reel(observation, now)
        elif phase == Phase.RESULT:
            if observation.continue_available:
                self._button("continue", CONTINUE, now)
            elif now - self.phase_since > 2.0:
                self.stop("结算页没有继续钓鱼按钮，已暂停")

    def _gear(self, observation, now):
        target_rod = self.rod or observation.rod or 1
        target_bait = self.bait or observation.bait or 1
        if observation.rod != target_rod:
            self._button("rod", RODS[target_rod - 1], now)
        elif observation.bait != target_bait:
            self._button("bait", BAITS[target_bait - 1], now)
        elif observation.start_available:
            self._button("start", START, now)
        elif now - self.phase_since > 3.0:
            self.stop("无法开始钓鱼，请检查鱼饵和钓点")

    def _track_fish(self, points, now):
        available = [track for track in self.tracks if now - track.last_seen < 0.24]
        self.tracks = available.copy()
        for point in points:
            nearest = min(available, default=None, key=lambda track: hypot(
                point[0] - track.position[0], point[1] - track.position[1],
            ))
            if nearest is not None and hypot(point[0] - nearest.position[0], point[1] - nearest.position[1]) < 25:
                nearest.position = tuple(previous * 0.35 + new * 0.65 for previous, new in zip(nearest.position, point))
                nearest.last_seen = now
                nearest.hits += 1
                available.remove(nearest)
            else:
                self.tracks.append(FishTrack(point, now, now))

    def confirmed_fish(self, now):
        return [track.position for track in self.tracks if track.hits >= 3
                and track.last_seen - track.first_seen >= 0.24 and now - track.last_seen < 0.16]

    def _aim(self, observation, now):
        reticle = observation.reticle
        self._track_fish(observation.fish_points, now)
        cast_attempt = self.attempts.get("cast")
        if cast_attempt is not None and now - cast_attempt[1] < 1.5:
            self.status = "抛竿中"
            return
        if reticle is None:
            self.castable_frames = 0
            self.empty_since = None
            if now - self.last_reticle_seen > 4.0:
                self.stop("未识别到落点准星，已暂停")
            return
        self.last_reticle_seen = now
        if observation.reticle_blocked:
            self.castable_frames = 0
            self.empty_since = None
            self.status = "落点越界，正在移回可抛竿水域"
            if not self.recovery_steps:
                self.blocked_area = self.target
                self.target = None
                self.release(1)
                self.aim_settle = now
            if now >= self.aim_settle:
                self._recover_aim(reticle, now)
            return

        self.last_castable = reticle
        self.recovery_steps = 0
        self.castable_frames += 1
        if now < self.aim_settle or self.castable_frames < 2:
            self.empty_since = None
            return

        targets = self.confirmed_fish(now)
        if not targets:
            self.target = None
            if observation.return_available:
                if self.empty_since is None:
                    self.empty_since = now
                self.status = "正在检查是否还有鱼标"
                if now - self.empty_since >= 3.0:
                    self.exit_started = now
                    self._exit_pond(observation, now)
            else:
                self.empty_since = None
            return
        self.empty_since = None

        self.target = choose_cast_area(targets, reticle, self.blocked_area)
        if self.target is None:
            self.status = "等待鱼群进入可抛竿区域"
            return
        dx, dy = self.target[0] - reticle[0], (self.target[1] - reticle[1]) * 1.25
        span = hypot(dx, dy)
        if span > 55 and self.aim_nudges < 7:
            self._move_aim(dx, dy, min(105, 105 * span / 85), min(0.31, max(0.115, span / 380)), now)
            self.aim_nudges += 1
            return
        if self._button("cast", ACTION, now):
            self.aim_nudges = 0
            self.status = "抛竿中"

    def _move_aim(self, dx, dy, strength, duration, now):
        span = hypot(dx, dy)
        point = (STICK[0] + strength * dx / span, STICK[1] + strength * dy / span)
        self._hold(1, point, now + duration, origin=STICK)
        self.aim_settle = now + duration + 0.13

    def _recover_aim(self, reticle, now):
        if self.recovery_steps >= 8:
            self.stop("无法移回可抛竿水域，已暂停")
            return
        x, y = self.last_castable or SAFE_AIM
        dx, dy = x - reticle[0], y - reticle[1]
        if hypot(dx, dy) < 25:
            dx, dy = SAFE_AIM[0] - reticle[0], SAFE_AIM[1] - reticle[1]
        if self.recovery_steps >= 4 or hypot(dx, dy) < 16:
            dx, dy = ((0, 1), (-1, 0), (1, 0), (0, -1))[self.recovery_steps % 4]
        duration = min(0.40, max(0.21, hypot(dx, dy) / 310))
        self._move_aim(dx, dy * 1.25, 108, duration, now)
        self.recovery_steps += 1

    def _exit_pond(self, observation, now):
        self.status = "未发现鱼标，正在退出钓点"
        if "close_pond" in self.attempts and observation.phase == Phase.UNKNOWN and now - self.last_seen >= 0.40:
            self.completed = True
            self.stop("钓点无鱼，已退出")
        elif now - self.exit_started > 8.0:
            self.stop("钓点退出未确认，已暂停")
        elif observation.phase == Phase.AIM and observation.return_available:
            self._button("leave_pond", RETURN, now)
        elif observation.phase == Phase.GEAR and observation.close_available:
            self._button("close_pond", GEAR_CLOSE, now)

    def _wait(self, observation, now):
        if now - self.phase_since < 18.0:
            return
        if not observation.return_available or self.no_bites >= 3:
            self.stop("长时间未咬钩，请检查钓点和鱼饵")
        elif self._button("retry", RETURN, now) and self.attempts["retry"][0] == 1:
            self.no_bites += 1
            self.cast_live = False

    def _bite(self, observation, now):
        if self.struck or observation.hook_angle is None:
            return
        angle = observation.hook_angle
        previous = self.last_hook
        self.last_hook = (angle, now)
        if previous is None or not 0.015 <= now - previous[1] <= 0.22:
            return
        low, high = observation.hook_zone
        speed = (angle - previous[0]) / (now - previous[1])
        if not -225 < speed < -20:
            return
        # Aim near the middle of the target band, allowing for the input trip.
        predicted = angle + speed * 0.065
        padding = max(5.0, (high - low) * 0.24)
        if low + padding <= predicted <= high - padding and low + 2 < angle < high + 2 and self._tap(ACTION, now):
            self.struck = True
            self.status = "已刺鱼"

    def _reel(self, observation, now):
        if observation.fish_red:
            self.status = "鱼标变红，停止收线和摆杆"
            self.reel_ready_at = now + 0.18
            self.wait_for_green = True
            self.steer_streak = self.last_steer = 0
            self.release()
            return
        if not observation.fish_visible or now < self.reel_ready_at:
            self.release()
            return
        direction = observation.steer
        self.steer_streak = self.steer_streak + 1 if direction == self.last_steer else 1
        self.last_steer = direction
        if direction and self.steer_streak >= 2:
            self._hold(1, (STICK[0] + 128 * direction, STICK[1]), origin=STICK)
            self.status = "向左摆杆" if direction < 0 else "向右摆杆"
        elif not direction or 1 in self.held:
            self.release(1)

        angle, limit = observation.tension, observation.green_limit
        margin = 16 if 0 in self.held else 30
        if observation.full_green:
            self.wait_for_green = False
        if self.wait_for_green:
            self.release(0)
            if not direction:
                self.status = "等张力表全绿"
            return
        reel = observation.full_green or (angle is not None and limit is not None and angle > limit + margin)
        if observation.release_reel or not reel:
            self.release(0)
            if observation.release_reel:
                self.status = "张力过高，已松手"
            return
        self._hold(0, ACTION)
        if observation.full_green:
            self.status = "全绿，持续收线"
        elif not direction:
            self.status = "调节张力并收线"
