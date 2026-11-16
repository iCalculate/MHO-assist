"""Serve the bundled keymap editor on a local port for one device."""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import webbrowser


class EditorHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, directory, profile, adb, serial):
        self.profile = profile
        self.adb = adb
        self.serial = serial
        super().__init__(*args, directory=directory)

    def log_message(self, *_args):
        pass

    def reply(self, body, content_type='application/json', status=200):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/api/profile':
            try:
                self.reply(self.profile.read_bytes())
            except OSError:
                self.send_error(404, 'Could not load Generic-Alt.json')
        elif self.path == '/api/screenshot':
            command = [str(self.adb)]
            if self.serial:
                command.extend(['-s', self.serial])
            command.extend(['exec-out', 'screencap', '-p'])
            try:
                result = subprocess.run(
                    command, capture_output=True, timeout=20, check=True,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
                )
                if not result.stdout.startswith(b'\x89PNG'):
                    raise ValueError('ADB returned an invalid screenshot')
                self.reply(result.stdout, 'image/png')
            except (OSError, ValueError, subprocess.SubprocessError):
                self.reply('请确认手机 USB 连接和调试授权正常'.encode(), 'text/plain; charset=utf-8', 502)
        else:
            super().do_GET()

    def do_POST(self):
        if self.path != '/api/profile':
            self.send_error(404)
            return
        origin = f'http://127.0.0.1:{self.server.server_port}'
        if self.headers.get('Origin') != origin or self.headers.get_content_type() != 'application/json':
            self.send_error(403)
            return
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size < 200000:
                raise ValueError('Invalid keymap size')
            profile = json.loads(self.rfile.read(size))
            if not isinstance(profile, dict) or not isinstance(profile.get('keyMapNodes'), list):
                raise ValueError('Invalid keymap')
            profile['switchKey'] = 'Key_Control'
            small_eyes = profile.get('mouseMoveMap', {}).get('smallEyes', {})
            if small_eyes.get('key') == 'Key_Control':
                small_eyes['key'] = 'Key_Alt'
            self.save(profile)
        except (OSError, ValueError, AttributeError) as error:
            self.reply(str(error).encode(), 'text/plain; charset=utf-8', 400)
        else:
            self.reply(b'{"ok":true}')

    def save(self, profile):
        backup = self.profile.with_suffix('.json.bak')
        if self.profile.exists():
            shutil.copy2(self.profile, backup)
        with tempfile.NamedTemporaryFile(
            mode='w', encoding='utf-8', dir=self.profile.parent, suffix='.tmp', delete=False,
        ) as output:
            pending = Path(output.name)
            try:
                json.dump(profile, output, indent=2, ensure_ascii=False)
            except Exception:
                output.close()
                pending.unlink()
                raise
        try:
            pending.replace(self.profile)
        finally:
            if pending.exists():
                pending.unlink()


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('profile', type=Path)
    parser.add_argument('serial', nargs='?', default='')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args(argv)
    editor = args.app / 'keymap-editor'
    if not (editor / 'index.html').is_file() or not args.profile.is_file():
        parser.error('The keymap editor assets or Generic-Alt.json are missing')

    def handler(*connection):
        return EditorHandler(
            *connection, directory=editor, profile=args.profile,
            adb=args.app / 'adb.exe', serial=args.serial,
        )

    with ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
        server.timeout = 30
        url = f'http://127.0.0.1:{server.server_port}'
        print(url, flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        # A detached editor exits after 30 minutes without a connection.
        last_request = time.monotonic()
        while time.monotonic() - last_request < 1800:
            started = time.monotonic()
            server.handle_request()
            if time.monotonic() - started < server.timeout:
                last_request = time.monotonic()
