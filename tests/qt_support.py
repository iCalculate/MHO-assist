import atexit

import mho_assist.app  # Configure the bundled Qt plugins before creating QApplication.
from PySide6.QtWidgets import QApplication


def application():
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
        atexit.register(app.shutdown)
    return app
