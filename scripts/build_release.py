"""Build and test a portable Windows x64 release archive."""
import argparse
import hashlib
from importlib import metadata
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from zipfile import ZipFile

PROJECT = Path(__file__).resolve().parents[1]
RUNTIME_SOURCES = {
    'numpy': 'https://github.com/numpy/numpy',
    'opencv-python': 'https://github.com/opencv/opencv-python',
    'mss': 'https://github.com/BoboTiG/python-mss',
    'PySide6-Essentials': 'https://code.qt.io/cgit/pyside/pyside-setup.git/',
    'shiboken6': 'https://code.qt.io/cgit/pyside/pyside-setup.git/',
    'PyInstaller': 'https://github.com/pyinstaller/pyinstaller',
}


def copy_notices(bundle):
    from PySide6.QtCore import qVersion

    notices = bundle / 'licenses'
    notices.mkdir()
    python_version = sys.version.split()[0]
    manifest = [f'Python {python_version}\nhttps://www.python.org/downloads/source/']
    shutil.copy2(Path(sys.base_prefix) / 'LICENSE.txt', notices / 'Python-LICENSE.txt')
    for package, url in RUNTIME_SOURCES.items():
        distribution = metadata.distribution(package)
        manifest.append(f'{package} {distribution.version}\n{url}')
        for entry in distribution.files:
            if not (entry.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE'))
                    or 'licenses' in {part.lower() for part in entry.parts[:-1]}):
                continue
            source = distribution.locate_file(entry)
            if source.is_file():
                destination = notices / package / entry
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
    shutil.copytree(PROJECT / 'scripts/freeze/licenses', notices / 'Qt')
    qt_version = qVersion()
    qt_series = '.'.join(qt_version.split('.')[:2])
    pyside_version = metadata.version('PySide6-Essentials')
    manifest.append(
        f'This application uses Qt {qt_version} through PySide6 under LGPL-3.0.\n'
        'The dynamically loaded Qt runtime libraries are in _internal/PySide6.\n'
        f'Qt source: https://download.qt.io/official_releases/qt/{qt_series}/{qt_version}/\n'
        f'PySide6 and Shiboken source: https://download.qt.io/official_releases/QtForPython/pyside6/PySide6-{pyside_version}-src/\n'
        'Qt third-party notices: https://doc.qt.io/qt-6/licenses-used-in-qt.html\n'
        'Qt for Python third-party notices: https://doc.qt.io/qtforpython-6/licenses.html'
    )
    (notices / 'README.txt').write_text('\n\n'.join(manifest) + '\n', encoding='utf-8')


def verify_archive(archive, folder):
    with ZipFile(archive) as zipped:
        invalid = zipped.testzip()
        if invalid:
            raise RuntimeError(f'Corrupt archive member: {invalid}')
        zipped.extractall(folder)

    app_dir = folder / 'MHO-Assist'
    environment = os.environ.copy()
    for variable in ('PYTHONHOME', 'PYTHONPATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH'):
        environment.pop(variable, None)
    windows = Path(os.environ['SystemRoot'])
    environment['PATH'] = os.pathsep.join([str(windows / 'System32'), str(windows)])
    environment['QT_QPA_PLATFORM'] = 'offscreen'
    subprocess.run(
        [str(app_dir / 'ArrowAssist.exe'), '--smoke-test'],
        cwd=app_dir, env=environment, check=True, timeout=60,
    )
    if not (app_dir / 'analysis/ui.png').is_file():
        raise RuntimeError('The extracted application did not render its smoke test window')


def build(version, output):
    output.mkdir(parents=True, exist_ok=True)
    stem = f'MHO-Assist-{version}-windows-x64'
    with tempfile.TemporaryDirectory(prefix='mho-build-', dir=output) as work:
        staging = Path(work)
        environment = {**os.environ, 'PYINSTALLER_CONFIG_DIR': str(staging / 'cache')}
        frozen = staging / 'frozen'
        subprocess.run(
            [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--log-level', 'WARN',
             '--distpath', str(frozen), '--workpath', str(staging / 'work'),
             str(PROJECT / 'scripts/freeze/ArrowAssist.spec')],
            cwd=PROJECT, env=environment, check=True,
        )
        bundle = frozen / 'MHO-Assist'
        for relative in ('README.md', 'docs/qtscrcpy.md', 'integrations/qtscrcpy/MHO-Dance.json'):
            target = bundle / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(PROJECT / relative, target)
        copy_notices(bundle)
        archive = Path(shutil.make_archive(str(staging / stem), 'zip', frozen, bundle.name))
        verify_archive(archive, staging / '解压 验证')
        with archive.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        published = output / archive.name
        archive.replace(published)
        checksum = published.with_suffix('.zip.sha256')
        checksum.write_text(f'{digest}  {published.name}\n', encoding='ascii')
    print(f'Smoke test passed.\n{published}\n{checksum}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True, help='Git tag, for example v5.0.0')
    parser.add_argument('--output', type=Path, default=PROJECT / 'dist')
    args = parser.parse_args()
    if sysconfig.get_platform() != 'win-amd64' or sys.version_info < (3, 11):
        parser.error('Use 64-bit CPython 3.11 or newer on Windows')
    if not re.fullmatch(r'v\d+\.\d+\.\d+', args.version):
        parser.error('Pass a version in the form v5.0.0')
    build(args.version, args.output.resolve())


if __name__ == '__main__':
    main()
