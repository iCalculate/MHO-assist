from pathlib import Path

import cv2
import numpy as np

TEMPLATE_DIR = Path(__file__).resolve().with_name('templates')


def load_template(name, flags=cv2.IMREAD_GRAYSCALE):
    path = TEMPLATE_DIR / name
    # OpenCV's Windows filename handling can fail on non-ASCII paths.
    encoded = np.frombuffer(path.read_bytes(), dtype=np.uint8)
    image = cv2.imdecode(encoded, flags)
    if image is None:
        raise ValueError(f'Invalid recognition template: {path}')
    return image
