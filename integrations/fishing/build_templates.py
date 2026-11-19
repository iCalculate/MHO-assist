"""Extract the small HUD signatures used by the fishing recognizer from the calibration video."""

import argparse
from pathlib import Path

import cv2
import numpy as np


# Coordinates refer to the unscaled 1920 × 862 recording.
SIGNATURES = {
    "gear": (5.0, (1379, 103, 1504, 134)),
    "gear_close": (5.0, (1739, 17, 1790, 66)),
    "start": (5.0, (1530, 776, 1669, 808)),
    "cast": (10.0, (1556, 628, 1647, 710)),
    "hook": (20.25, (1551, 630, 1650, 711)),
    "reel": (65.2, (1562, 646, 1634, 707)),
    "hold": (65.0, (1538, 484, 1656, 557)),
    "release": (75.0, (1561, 483, 1630, 548)),
    "tension_left_tip": (56.65, (855, 149, 878, 173)),
    "fish": (60.0, (917, 220, 1008, 273)),
    "red_fish": (90.0, (916, 221, 1011, 274)),
    "glint_fish": (92.0, (916, 221, 1011, 274)),
    "left": (70.0, (435, 290, 591, 323)),
    "right": (145.0, (1357, 290, 1513, 322)),
    "continue": (165.0, (1619, 773, 1725, 809)),
    "exit": (165.0, (1385, 773, 1452, 806)),
    "caught": (165.0, (262, 226, 481, 288)),
    "catch_great_whetfish": (27.1, (265, 546, 491, 584)),
    "catch_glutton_tuna_king": (103.0, (265, 546, 491, 584)),
    "catch_glutton_tuna": (165.0, (280, 546, 475, 584)),
    "catch_rank_good": (27.1, (265, 551, 331, 581)),
    "catch_rank_rare": (165.0, (281, 551, 347, 581)),
    "catch_rank_excellent": (103.0, (265, 551, 331, 581)),
    "catch_new": (27.1, (500, 546, 528, 567)),
    "catch_size_giant": (27.1, (332, 594, 423, 624)),
    "hook_rank_normal": (112.98, (880, 285, 942, 313)),
    "hook_whetfish": (112.98, (951, 285, 1038, 313)),
    "return": (20.0, (250, 174, 317, 199)),
    "miss": (115.0, (897, 291, 1025, 327)),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path)
    args = parser.parse_args()
    capture = cv2.VideoCapture(str(args.video))
    patches = {}
    try:
        for name, (seconds, (x1, y1, x2, y2)) in SIGNATURES.items():
            capture.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000)
            ok, frame = capture.read()
            if not ok or frame.shape[:2] != (862, 1920):
                raise ValueError(f"Invalid calibration frame at {seconds}s")
            patch = cv2.cvtColor(frame[y1:y2, x1:x2], cv2.COLOR_BGR2GRAY)
            patches[name] = patch
            patches[name + "_xy"] = np.array([x1, y1], dtype=np.int32)
    finally:
        capture.release()
    np.savez_compressed(Path(__file__).with_name("templates.npz"), **patches)


if __name__ == "__main__":
    main()
