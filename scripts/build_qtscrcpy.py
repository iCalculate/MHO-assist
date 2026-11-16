"""Build the complete MHO QtScrcpy Windows x64 release."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from urllib.request import Request, urlopen
from zipfile import ZipFile

from scripts.build_release import copy_python_notices, system_environment

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT / 'qtscrcpy'
CORE = SOURCE / 'QtScrcpy/QtScrcpyCore'
NATIVE = SOURCE / 'output/x64/RelWithDebInfo'
OUTPUT = PROJECT / 'dist'
BUILD = OUTPUT / 'qtscrcpy-build'
WORKER = OUTPUT / 'qtscrcpy-worker'
CORE_REVISION = '9e388b8aa48e4e1c2cfdef408ef00c4c6b45c921'
APP_NAME = 'MHO-QtScrcpy'
NATIVE_FILES = (
    'QtScrcpy.exe', 'adb.exe', 'AdbWinApi.dll', 'AdbWinUsbApi.dll', 'scrcpy-server',
    'avcodec-58.dll', 'avformat-58.dll', 'avutil-56.dll', 'swscale-5.dll', 'swresample-3.dll',
    'sndcpy.apk', 'sndcpy.bat',
)
RUNTIME_FILES = (
    'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Multimedia.dll', 'Qt5Network.dll',
    'Qt5Svg.dll', 'Qt5Widgets.dll', 'D3Dcompiler_47.dll', 'libEGL.dll', 'libGLESv2.dll',
    'opengl32sw.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
)
PLUGIN_DIRS = ('audio', 'bearer', 'imageformats', 'mediaservice', 'platforms', 'playlistformats', 'styles')


def restore_core_dependencies():
    dependencies = CORE / 'src/third_party'
    if all((dependencies / path).is_file() for path in (
        'scrcpy-server', 'adb/win/adb.exe',
    )):
        return
    url = f'https://codeload.github.com/barry-ran/QtScrcpyCore/zip/{CORE_REVISION}'
    with tempfile.TemporaryDirectory(prefix='mho-core-') as temporary:
        folder = Path(temporary)
        archive = folder / 'core.zip'
        print(f'Downloading QtScrcpyCore build dependencies from {CORE_REVISION}', flush=True)
        with urlopen(url, timeout=40) as response, archive.open('wb') as target:
            shutil.copyfileobj(response, target)
        with ZipFile(archive) as downloaded:
            downloaded.extractall(folder)
        supplied = folder / f'QtScrcpyCore-{CORE_REVISION}' / 'src/third_party'
        shutil.copytree(supplied / 'adb', dependencies / 'adb', dirs_exist_ok=True)
        shutil.copy2(supplied / 'scrcpy-server', dependencies / 'scrcpy-server')


def compile_programs(qt, ffmpeg):
    restore_core_dependencies()
    subprocess.run(
        ['cmake', '-S', str(SOURCE), '-B', str(BUILD), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=RelWithDebInfo', f'-DCMAKE_PREFIX_PATH={qt.as_posix()}',
         '-DQT_DESIRED_VERSION=5', f'-DMHO_FFMPEG_ROOT={ffmpeg.as_posix()}'],
        cwd=PROJECT, check=True,
    )
    subprocess.run(['cmake', '--build', str(BUILD), '--parallel', '4'], cwd=PROJECT, check=True)
    environment = system_environment()
    environment['PYINSTALLER_CONFIG_DIR'] = str(OUTPUT / 'qtscrcpy-worker-cache')
    subprocess.run(
        [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--log-level', 'WARN', '--distpath',
         str(WORKER), '--workpath', str(OUTPUT / 'qtscrcpy-worker-build'),
         str(PROJECT / 'scripts/freeze/DirectionWorker.spec')],
        cwd=PROJECT, env=environment, check=True,
    )


def stage_files(app, runtime, pointer, version):
    app.mkdir()
    for item in NATIVE_FILES:
        shutil.copy2(NATIVE / item, app / item)
    for item in RUNTIME_FILES:
        shutil.copy2(runtime / item, app / item)
    for folder in PLUGIN_DIRS:
        (app / folder).mkdir()
        for plugin in sorted((runtime / folder).glob('*.dll')):
            shutil.copy2(plugin, app / folder / plugin.name)
    (app / 'qt.conf').write_text('[Paths]\nPlugins=.\n', encoding='ascii')
    shutil.copy2(pointer, app / 'qtscrcpy-pointer.apk')
    shutil.copytree(SOURCE / 'config', app / 'config', ignore=shutil.ignore_patterns('userdata.ini'))
    shutil.copytree(SOURCE / 'keymap', app / 'keymap', ignore=shutil.ignore_patterns('*.bak'))
    shutil.copytree(WORKER / 'DirectionWorker', app / 'DirectionWorker')
    editor = PROJECT / 'integrations/qtscrcpy/keymap-editor'
    shutil.copytree(editor, app / 'keymap-editor')
    shutil.copy2(PROJECT / 'README.md', app / 'README.md')
    (app / 'VERSION.txt').write_text(f'MHO QtScrcpy {version}\n', encoding='ascii')

    notices = app / 'licenses'
    manifest = copy_python_notices(notices, ('numpy', 'opencv-python', 'PyInstaller'))
    shutil.copy2(SOURCE / 'LICENSE', notices / 'QtScrcpy-Apache-2.0.txt')
    shutil.copy2(CORE / 'LICENSE', notices / 'QtScrcpyCore-Apache-2.0.txt')
    shutil.copy2(editor / 'LICENSE', notices / 'ScrcpyKeyMapper-MIT.txt')
    shutil.copy2(SOURCE / 'licenses/FFmpeg-LGPL-2.1.txt', notices / 'FFmpeg-LGPL-2.1.txt')
    shutil.copytree(PROJECT / 'scripts/freeze/licenses', notices / 'Qt')
    manifests = [
        'Modified QtScrcpy 4.2.1 client and QtScrcpyCore are provided in the release source.\n'
        'https://github.com/barry-ran/QtScrcpy/tree/v4.2.1\n'
        f'https://github.com/barry-ran/QtScrcpyCore/tree/{CORE_REVISION}',
        'The client uses the Qt 5.15.2 libraries from the upstream Windows release under LGPL-3.0.\n'
        'Qt DLLs are adjacent to QtScrcpy.exe and can be replaced with compatible versions.\n'
        'Qt source: https://download.qt.io/archive/qt/5.15/5.15.2/submodules/',
        'Platform tools, scrcpy-server and sndcpy originate from QtScrcpy and QtScrcpyCore.\n'
        'https://android.googlesource.com/platform/packages/modules/adb/\n'
        'https://github.com/Genymobile/scrcpy\n'
        'https://github.com/rom1v/sndcpy',
        'The Qt client dynamically links FFmpeg 4.4.2 under LGPL-2.1-or-later.\n'
        'https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.xz\n'
        f'https://github.com/Chauncy-Du/MHO-assist/blob/qtscrcpy-{version}/scripts/build_ffmpeg.sh',
        'The bundled editor uses the MIT-licensed ScrcpyKeyMapper and its vendored UI libraries.\n'
        'https://github.com/w4po/ScrcpyKeyMapper',
    ]
    (notices / 'README.txt').write_text('\n\n'.join(manifests + manifest) + '\n', encoding='utf-8')


def verify_keymap_editor(app, profile, environment):
    worker = app / 'DirectionWorker/DirectionWorker.exe'
    with subprocess.Popen(
        [str(worker), '--keymap-editor', str(app), str(profile), '--no-browser'],
        cwd=app, env=environment, stdout=subprocess.PIPE, text=True, encoding='utf-8',
    ) as server, ThreadPoolExecutor(max_workers=1) as reader:
        try:
            url = reader.submit(server.stdout.readline).result(timeout=15).strip()
            if not url.startswith('http://127.0.0.1:'):
                raise RuntimeError('The packaged keymap editor did not start on loopback')
            with urlopen(url, timeout=5) as response:
                if b'js/app.js' not in response.read():
                    raise RuntimeError('Keymap editor page did not load')
            with urlopen(url + '/api/profile', timeout=5) as response:
                mapping = json.load(response)
            mapping['switchKey'] = 'Key_F1'
            update = Request(url + '/api/profile', json.dumps(mapping).encode(),
                             headers={'Origin': url, 'Content-Type': 'application/json'})
            with urlopen(update, timeout=5) as response:
                if not json.load(response).get('ok'):
                    raise RuntimeError('Keymap editor did not save the profile')
            if json.loads(profile.read_text(encoding='utf-8'))['switchKey'] != 'Key_Control':
                raise RuntimeError('The exported profile did not preserve the mapping toggle')
        finally:
            server.terminate()


def verify_ffmpeg(app):
    with os.add_dll_directory(app):
        utility = ctypes.CDLL(str(app / 'avutil-56.dll'))
        utility.av_version_info.restype = ctypes.c_char_p
        if utility.av_version_info().lstrip(b'n') != b'4.4.2':
            raise RuntimeError('Expected FFmpeg 4.4.2')
        codec = ctypes.CDLL(str(app / 'avcodec-58.dll'))
        codec.avcodec_license.restype = ctypes.c_char_p
        if not codec.avcodec_license().startswith(b'LGPL'):
            raise RuntimeError('The staged FFmpeg build must be redistributable under LGPL')
        codec.avcodec_find_decoder.argtypes = [ctypes.c_int]
        codec.avcodec_find_decoder.restype = ctypes.c_void_p
        if not codec.avcodec_find_decoder(27):  # AV_CODEC_ID_H264 in FFmpeg 4.4
            raise RuntimeError('The bundled FFmpeg is missing the H.264 decoder')
        formats = ctypes.CDLL(str(app / 'avformat-58.dll'))
        formats.av_guess_format.argtypes = [ctypes.c_char_p] * 3
        formats.av_guess_format.restype = ctypes.c_void_p
        if not formats.av_guess_format(b'mp4', None, None):
            raise RuntimeError('The bundled FFmpeg is missing MP4 recording support')


def verify(archive, folder):
    import cv2
    import numpy as np

    with ZipFile(archive) as zipped:
        if zipped.testzip() is not None:
            raise RuntimeError('The release ZIP failed its CRC test')
        zipped.extractall(folder)
    app = folder / APP_NAME
    environment = system_environment()
    environment.update(
        QT_QPA_PLATFORM='offscreen',
        QTSCRCPY_CONFIG_PATH=str(folder / 'check-config'),
        QTSCRCPY_KEYMAP_PATH=str(folder / 'check-keymap'),
    )
    subprocess.run(
        [sys.executable, '-c',
         'from pathlib import Path; import sys; from scripts.build_qtscrcpy import verify_ffmpeg; verify_ffmpeg(Path(sys.argv[1]))',
         str(app)], cwd=PROJECT, env=environment, check=True, timeout=15,
    )
    screen = folder / 'qt-startup.png'
    subprocess.run([str(app / 'QtScrcpy.exe'), '--smoke-test', str(screen)],
                   cwd=app, env=environment, check=True, timeout=30)
    if not screen.is_file():
        raise RuntimeError('The extracted QtScrcpy did not render its main window')
    profile = folder / 'check-keymap/Generic-Alt.json'
    verify_keymap_editor(app, profile, environment)
    sample = (PROJECT / 'tests/fixtures/frame_36.png').read_bytes()
    frame = cv2.imdecode(np.frombuffer(sample, np.uint8), cv2.IMREAD_COLOR)
    fixture = folder / 'arrow-frame.yuv'
    fixture.write_bytes(cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420).tobytes())
    for name in ('input_reset_test', 'panel_layout_test', 'integrated_assist_test'):
        test = app / f'{name}.exe'
        shutil.copy2(BUILD / test.name, test)
        command = [str(test)]
        if name == 'integrated_assist_test':
            command.append(str(fixture))
        elif name == 'input_reset_test':
            command.append(str(app / 'keymap/Generic-Alt.json'))
        subprocess.run(command, cwd=app, env=environment, check=True, timeout=30)
    print('QtScrcpy startup, keymap editor, HUD, input and embedded recognition passed.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt', type=Path, required=True, help='Qt 5.15.2 MSVC x64 kit')
    parser.add_argument('--runtime', type=Path, required=True, help='Extracted upstream QtScrcpy 4.2.1 Windows x64 ZIP')
    parser.add_argument('--ffmpeg', type=Path, required=True, help='FFmpeg 4.4.2 LGPL SDK built by build_ffmpeg.sh')
    parser.add_argument('--pointer-apk', type=Path, required=True, help='Signed qtscrcpy-pointer.apk')
    parser.add_argument('--version', required=True, help='Release version, for example v4.2.1-mho.1')
    args = parser.parse_args()
    if sysconfig.get_platform() != 'win-amd64' or sys.version_info < (3, 11):
        parser.error('Run with 64-bit CPython 3.11+ in an MSVC x64 developer shell')
    if not re.fullmatch(r'v4\.2\.1-mho\.\d+', args.version):
        parser.error('Use a version in the form v4.2.1-mho.1')
    for label, path in (
        ('Qt', args.qt / 'lib/cmake/Qt5/Qt5Config.cmake'),
        ('runtime', args.runtime / 'Qt5Core.dll'),
        ('FFmpeg SDK', args.ffmpeg / 'lib/avcodec.lib'),
        ('pointer APK', args.pointer_apk),
    ):
        if not path.is_file():
            parser.error(f'Missing {label}: {path}')
    OUTPUT.mkdir(exist_ok=True)
    compile_programs(args.qt.resolve(), args.ffmpeg.resolve())
    with tempfile.TemporaryDirectory(prefix='mho-qtscrcpy-', dir=OUTPUT) as temporary:
        folder = Path(temporary)
        app = folder / APP_NAME
        stage_files(app, args.runtime.resolve(), args.pointer_apk.resolve(), args.version)
        stem = f'{APP_NAME}-{args.version}-windows-x64'
        archive = Path(shutil.make_archive(str(folder / stem), 'zip', folder, APP_NAME))
        verify(archive, folder / 'extract check')
        with archive.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        published = OUTPUT / archive.name
        archive.replace(published)
        published.with_suffix('.zip.sha256').write_text(f'{digest}  {published.name}\n', encoding='ascii')
        print(published)


if __name__ == '__main__':
    main()
