#!/usr/bin/env python3
"""Install ISO onto a newly created file, then boot that disk twice without ISO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def firmware():
    code = os.environ.get('OVMF_CODE')
    variables = os.environ.get('OVMF_VARS')
    candidates = [Path('/usr/share/OVMF/OVMF_CODE_4M.fd'), Path('/usr/share/OVMF/OVMF_CODE.fd')]
    if shutil.which('brew'):
        prefix = subprocess.check_output(['brew', '--prefix', 'qemu'], text=True).strip()
        candidates.append(Path(prefix) / 'share/qemu/edk2-x86_64-code.fd')
    if not code:
        code = next((str(p) for p in candidates if p.is_file()), None)
    if code and not variables:
        p = Path(code)
        name = {'OVMF_CODE_4M.fd': 'OVMF_VARS_4M.fd', 'OVMF_CODE.fd': 'OVMF_VARS.fd',
                'edk2-x86_64-code.fd': 'edk2-i386-vars.fd'}.get(p.name)
        if name:
            variables = str(p.with_name(name))
    if not code or not variables or not Path(code).is_file() or not Path(variables).is_file():
        raise RuntimeError('set OVMF_CODE and OVMF_VARS to a matched firmware pair')
    return code, variables


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def wait_marker(serial, marker, process, deadline):
    while time.monotonic() < deadline:
        text = serial.read_text(errors='replace') if serial.exists() else ''
        if re.search(r'PANIC_BEGIN|FATAL ERROR|\[TEST\] installer_complete: FAIL', text):
            raise RuntimeError('installer failed; inspect install-serial.log')
        if marker in text:
            return
        if process.poll() is not None:
            raise RuntimeError('QEMU exited before ' + marker)
        time.sleep(.1)
    raise TimeoutError('waiting for ' + marker)


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile('rwb', buffering=0)
        if 'QMP' not in json.loads(self.stream.readline()):
            raise RuntimeError('missing QMP greeting')
        self.command('qmp_capabilities')

    def command(self, name, arguments=None):
        self.stream.write((json.dumps({'execute': name, 'arguments': arguments or {}}) + '\n').encode())
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError('QMP disconnected')
            response = json.loads(line)
            if 'error' in response:
                raise RuntimeError(response['error'])
            if 'return' in response:
                return response['return']

    def key(self, key):
        self.command('send-key', {'keys': [{'type': 'qcode', 'data': key}], 'hold-time': 100})
        time.sleep(.2)

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path, help='new directory, must not exist')
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    if not 60 <= args.timeout <= 240:
        parser.error('timeout must be 60..240 seconds per phase')
    iso = args.iso.resolve()
    if not iso.is_file():
        parser.error('ISO must be a regular file')
    code, variables = firmware()
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    output = output.resolve()
    disk = output / 'installed.img'
    # Never accept an existing target or physical disk. This is the only writable
    # drive exposed to the installer, and the directory itself was just created.
    with disk.open('xb') as stream:
        stream.truncate(512 * 1024 * 1024)
    blank_hash = digest(disk)
    vars_copy = output / 'install-vars.fd'
    shutil.copyfile(variables, vars_copy)
    serial = output / 'install-serial.log'
    # macOS UNIX socket paths have a short limit; use a separate disposable dir.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='ku-install-') as sockets:
        qmp_path = Path(sockets) / 'qmp.sock'
        command = ['qemu-system-x86_64', '-machine', 'q35,accel=tcg,i8042=off',
            '-cpu', 'max', '-smp', '4', '-m', '1024',
            '-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={code}',
            '-drive', f'if=pflash,format=raw,unit=1,file={vars_copy}',
            '-drive', f'if=none,id=target,format=raw,file={disk}',
            '-device', 'ide-hd,drive=target,bus=ide.0',
            '-drive', f'if=none,id=cd,media=cdrom,readonly=on,file={iso}',
            '-device', 'ide-cd,drive=cd,bus=ide.1,bootindex=1',
            '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
            '-device', 'usb-tablet,bus=xhci.0', '-net', 'none', '-display', 'none',
            '-serial', f'file:{serial}', '-qmp', f'unix:{qmp_path},server=on,wait=off',
            '-no-reboot', '-no-shutdown']
        with (output / 'install-qemu.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            qmp = None
            try:
                deadline = time.monotonic() + args.timeout
                wait_marker(serial, 'installer: select setup mode:', process, deadline)
                qmp = QMP(qmp_path)
                qmp.key('down')
                qmp.key('ret')
                for marker in ('installer: select language:', 'installer: enter username:',
                               'installer: select password mode:', 'installer: select target disk index:'):
                    wait_marker(serial, marker, process, deadline)
                    qmp.key('ret')
                wait_marker(serial, 'installer: type INSTALL to confirm:', process, deadline)
                # Exercise cancellation before granting the destructive confirmation.
                qmp.key('esc')
                wait_marker(serial, '[TEST] installer_cancel_safe: PASS', process, deadline)
                if digest(disk) != blank_hash:
                    raise RuntimeError('installer wrote target before INSTALL confirmation')
                print('[installer-qemu] cancellation preserved blank disk: PASS', flush=True)
                qmp.key('ret')  # Select the same sole disposable disk again.
                time.sleep(.3)
                for key in 'install':
                    qmp.key(key)
                qmp.key('ret')
                wait_marker(serial, '[TEST] installer_complete: PASS', process, deadline)
                qmp.command('screendump', {'filename': str(output / 'installed.ppm')})
                print('[installer-qemu] installation: PASS', flush=True)
            finally:
                if qmp:
                    qmp.close()
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    env = dict(os.environ, OVMF_CODE=code, OVMF_VARS=variables)
    for phase, marker in (('first-boot', 'installed_first_boot'), ('second-boot', 'installed_persistence')):
        cmd = ['bash', str(ROOT / 'scripts/smoke-uefi-iso-qemu.sh'), str(disk),
            '--disk', '--persistent-disk', '--smp', '4', '--nic', 'none', '--no-ps2',
            '--usb-keyboard', '--usb-tablet', '--timeout', str(args.timeout),
            '--log-dir', str(output / phase),
            '--send-key-after-marker', '[TEST] installed_account_profile: PASS', 'ret',
            '--require-marker', f'[TEST] {marker}: PASS',
            '--require-marker', '[SESSION] flux_home_first_frame: PASS',
            '--screendump-after-marker', '[SESSION] flux_home_first_frame: PASS', str(output / (phase + '.ppm'))]
        with (output / (phase + '-runner.log')).open('wb') as log:
            subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (output / phase / 'serial.log').read_text(errors='replace')
        if re.search(r'PANIC_BEGIN|FATAL ERROR|\[(?:TEST|SESSION)\][^\n]*: FAIL', text):
            raise RuntimeError(phase + ' reported guest failure')
        if '[TEST] installer_package_preflight: PASS' in text or '[TEST] live_login_profile: PASS' in text:
            raise RuntimeError('booted installer/live media instead of installed disk')
        print('[installer-qemu] ' + phase + ' and authenticated Home: PASS', flush=True)
    manifest = {'iso': str(iso), 'iso_sha256': digest(iso), 'disk_sha256': digest(disk),
                'source_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'harness_sha256': digest(Path(__file__)), 'result': 'PASS'}
    (output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('[installer-qemu] ISO -> new disk -> first boot -> persistent reboot: PASS', flush=True)


if __name__ == '__main__':
    main()
