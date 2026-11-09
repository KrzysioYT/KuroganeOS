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
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
INSTALLED_PROFILE = '[TEST] installed_account_profile: PASS'
BAD_PASSWORD = '[TEST] installed_login_bad_password_rejected: PASS'
GOOD_PASSWORD = '[TEST] installed_login_password: PASS'
SESSION_CREATED = '[SESSION] flux_session_create: PASS'
HOME_FRAME = '[SESSION] flux_home_first_frame: PASS'
DESKTOP = '[TEST] red_flux_login_to_desktop: PASS'
INVALID_PROFILE = '[TEST] installed_account_profile_invalid_blocked: PASS'
REJECTED_PROFILE = '[TEST] installed_login_invalid_profile_rejected: PASS'
# Public keyboard fixture for disposable VM disks; never accept real passwords.
FIXTURE_PASSWORD = 'flux8'


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


def saved_theme(text):
    values = [int(value) for value in re.findall(r'^\[SETTINGS\] theme_saved=(\d+)\r?$', text, re.M)]
    if len(values) != 2 or values[0] == values[1] or values[-1] == 0:
        raise RuntimeError('expected two distinct theme changes ending in a nondefault theme')
    return values[-1]


def saved_text_scale(text):
    values = [int(value) for value in re.findall(
        r'^\[SETTINGS\] text_scale_saved=(\d+)\r?$', text, re.M)]
    if len(values) != 1 or values[0] not in (125, 150, 200):
        raise RuntimeError('expected one persisted nondefault text scale')
    return values[0]


def verify_password_login(text, rejected_first):
    sequence = [INSTALLED_PROFILE]
    if rejected_first:
        sequence.append(BAD_PASSWORD)
    sequence.extend([GOOD_PASSWORD, SESSION_CREATED, HOME_FRAME, DESKTOP])
    watched = set(sequence) | {BAD_PASSWORD}
    events = [line for line in text.splitlines() if line in watched]
    if events != sequence:
        raise RuntimeError('installed password login did not reject/authenticate in order')


def disk_boot_command(disk, log_dir, timeout):
    return ['bash', str(ROOT / 'scripts/smoke-uefi-iso-qemu.sh'), str(disk),
            '--disk', '--persistent-disk', '--smp', '4', '--nic', 'none', '--no-ps2',
            '--usb-keyboard', '--usb-tablet', '--timeout', str(timeout),
            '--log-dir', str(log_dir)]


def send_keys_after(cmd, marker, keys):
    for key in keys:
        cmd.extend(['--send-key-after-marker-count', marker, '1', key])


def verify_missing_profile(disk, output, env, timeout):
    # Modify only a new copy of the just-installed disposable disk, never a
    # device or the disk used to demonstrate the successful installation.
    damaged = output / 'missing-profile.img'
    original_hash = digest(disk)
    with disk.open('rb') as source, damaged.open('xb') as copy:
        shutil.copyfileobj(source, copy)
    partition = subprocess.check_output(
        ['sgdisk', '--info=2', str(damaged)], text=True, env=dict(env, LC_ALL='C'))
    match = re.search(r'^First sector: (\d+)\b', partition, re.M)
    if not match or "Partition name: 'Kurogane Root'" not in partition:
        raise RuntimeError('disposable disk does not contain expected FAT32 root partition')
    offset = int(match[1]) * 512
    if not 0 < offset < damaged.stat().st_size:
        raise RuntimeError('invalid root partition offset in disposable disk')
    subprocess.run(
        ['mdel', '-i', f'{damaged}@@{offset}', '::/etc/user.cfg', '::/etc/locale.cfg'],
        check=True, env=env)
    log_dir = output / 'missing-profile-boot'
    cmd = disk_boot_command(damaged, log_dir, timeout)
    cmd.extend([
        '--send-key-after-marker', INVALID_PROFILE, 'ret',
        '--require-marker', INVALID_PROFILE,
        '--require-marker', REJECTED_PROFILE,
        '--require-marker', '[ACCOUNTD] installed_profile_invalid: FAIL',
        '--screendump-after-marker', REJECTED_PROFILE, str(output / 'missing-profile.ppm'),
    ])
    with (output / 'missing-profile-runner.log').open('wb') as log:
        subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    text = (log_dir / 'serial.log').read_text(errors='replace')
    if any(marker in text for marker in (
            'PANIC_BEGIN', 'FATAL ERROR', '[TEST] live_login_profile: PASS',
            INSTALLED_PROFILE, SESSION_CREATED, HOME_FRAME, DESKTOP)):
        raise RuntimeError('damaged installation booted a session or suffered a fatal fault')
    if digest(disk) != original_hash:
        raise RuntimeError('successful installation disk changed during profile fault test')
    print('[installer-qemu] missing installed profile: Login refused the session: PASS',
          flush=True)


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


def verify_read_only_target_recovery(iso, output, code, variables, timeout):
    disk = output / 'readonly-target.img'
    with disk.open('xb') as stream:
        stream.truncate(64 * 1024 * 1024)
    original_hash = digest(disk)
    vars_copy = output / 'readonly-vars.fd'
    shutil.copyfile(variables, vars_copy)
    serial = output / 'readonly-serial.log'
    with tempfile.TemporaryDirectory(prefix='ku-readonly-') as sockets:
        qmp_path = Path(sockets) / 'qmp.sock'
        command = ['qemu-system-x86_64', '-machine', 'q35,accel=tcg,i8042=off',
            '-cpu', 'max', '-smp', '4', '-m', '1024',
            '-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={code}',
            '-drive', f'if=pflash,format=raw,unit=1,file={vars_copy}',
            '-drive', f'if=none,id=target,format=raw,file={disk},readonly=on',
            '-device', 'virtio-blk-pci,drive=target',
            '-drive', f'if=none,id=cd,media=cdrom,readonly=on,file={iso}',
            '-device', 'ide-cd,drive=cd,bus=ide.1,bootindex=1',
            '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
            '-device', 'usb-tablet,bus=xhci.0', '-net', 'none', '-display', 'none',
            '-serial', f'file:{serial}', '-qmp', f'unix:{qmp_path},server=on,wait=off',
            '-no-reboot', '-no-shutdown']
        with (output / 'readonly-qemu.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            qmp = None
            try:
                deadline = time.monotonic() + timeout
                wait_marker(serial, 'installer: select setup mode:', process, deadline)
                qmp = QMP(qmp_path)
                qmp.key('down')
                qmp.key('ret')
                while time.monotonic() < deadline:
                    text = serial.read_text(errors='replace') if serial.exists() else ''
                    if 'installer: select language:' in text:
                        raise RuntimeError(
                            'read-only target entered destructive installation setup')
                    if '[TEST] installer_target_disk_unavailable: PASS' in text:
                        break
                    if process.poll() is not None:
                        raise RuntimeError(
                            'QEMU exited before read-only target recovery')
                    time.sleep(.1)
                else:
                    raise TimeoutError('waiting for read-only target fallback')
                if '[TEST] live_package_root: PASS' not in text:
                    raise RuntimeError('read-only target did not enter the live session')
                qmp.key('ret')
                wait_marker(serial, '[TEST] live_login_profile: PASS', process, deadline)
                qmp.key('ret')
                wait_marker(serial, DESKTOP, process, deadline)
                if digest(disk) != original_hash:
                    raise RuntimeError('read-only target changed during recovery')
                print('[installer-qemu] read-only target -> live recovery and Home: PASS',
                      flush=True)
            finally:
                if qmp:
                    qmp.close()
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


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
    parser.add_argument('--desktop-lifecycle', action='store_true',
                        help='exercise windows and Settings keyboard on first installed boot')
    parser.add_argument('--password-profile', action='store_true',
                        help='qualify wrong/correct passwords and a damaged profile on disposable disks')
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
    verify_read_only_target_recovery(
        iso, output, code, variables, args.timeout)
    serial = output / 'install-serial.log'
    # macOS UNIX socket paths have a short limit; use a separate disposable dir.
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
                for marker in ('installer: select language:', 'installer: enter username:'):
                    wait_marker(serial, marker, process, deadline)
                    qmp.key('ret')
                wait_marker(serial, 'installer: select password mode:', process, deadline)
                if args.password_profile:
                    qmp.key('down')
                qmp.key('ret')
                if args.password_profile:
                    wait_marker(serial, 'installer: enter password:', process, deadline)
                    for key in FIXTURE_PASSWORD:
                        qmp.key(key)
                    qmp.key('ret')
                wait_marker(serial, 'installer: select target disk index:', process, deadline)
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
    expected_theme = None
    expected_text_scale = None
    for phase, marker in (('first-boot', 'installed_first_boot'), ('second-boot', 'installed_persistence')):
        cmd = disk_boot_command(disk, output / phase, args.timeout)
        cmd.extend([
            '--require-marker', f'[TEST] {marker}: PASS',
            '--require-marker', HOME_FRAME,
            '--require-marker', DESKTOP,
            '--screendump-after-marker', DESKTOP, str(output / (phase + '.ppm')),
        ])
        rejected_first = phase == 'first-boot'
        if args.password_profile:
            after = INSTALLED_PROFILE
            if rejected_first:
                send_keys_after(cmd, INSTALLED_PROFILE, ['n', 'o', 'ret'])
                cmd.extend(['--require-marker', BAD_PASSWORD])
                after = BAD_PASSWORD
            send_keys_after(cmd, after, [*FIXTURE_PASSWORD, 'ret'])
            cmd.extend(['--require-marker', GOOD_PASSWORD])
        else:
            cmd.extend(['--send-key-after-marker', INSTALLED_PROFILE, 'ret'])
        if args.desktop_lifecycle and phase == 'first-boot':
            cmd.extend(['--flux-lifecycle', '--flux-settings-write'])
        if expected_theme is not None:
            cmd.extend(['--require-marker', f'[SETTINGS] theme_restored={expected_theme}'])
        if expected_text_scale is not None:
            cmd.extend([
                '--require-marker',
                f'[SETTINGS] text_scale_restored={expected_text_scale}',
                '--require-marker',
                '[TEST] flux_text_scale_restore: PASS'])
            cmd.extend(['--require-marker', '[SETTINGS] wallpaper_restored=1',
                        '--require-marker', '[TEST] flux_background_restore: PASS'])
        with (output / (phase + '-runner.log')).open('wb') as log:
            subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        text = (output / phase / 'serial.log').read_text(errors='replace')
        if re.search(r'PANIC_BEGIN|FATAL ERROR|\[(?:TEST|SESSION)\][^\n]*: FAIL', text):
            raise RuntimeError(phase + ' reported guest failure')
        if '[TEST] installer_package_preflight: PASS' in text or '[TEST] live_login_profile: PASS' in text:
            raise RuntimeError('booted installer/live media instead of installed disk')
        if args.password_profile:
            verify_password_login(text, rejected_first)
            print('[installer-qemu] ' + phase + ' password authentication: PASS',
                  flush=True)
        if args.desktop_lifecycle and phase == 'first-boot':
            expected_theme = saved_theme(text)
            expected_text_scale = saved_text_scale(text)
            if re.findall(r'^\[SETTINGS\] wallpaper_saved=(\d+)\r?$', text, re.M) != ['1']:
                raise RuntimeError('expected the wallpaper shortcut to save Graphite Grid')
        if expected_theme is not None and phase == 'second-boot':
            restored = re.findall(r'^\[SETTINGS\] theme_restored=(\d+)\r?$', text, re.M)
            if restored != [str(expected_theme)]:
                raise RuntimeError('reboot restored a different or ambiguous theme')
            print('[installer-qemu] exact theme restored after reboot: PASS', flush=True)
            restored_scale = re.findall(
                r'^\[SETTINGS\] text_scale_restored=(\d+)\r?$', text, re.M)
            if restored_scale != [str(expected_text_scale)]:
                raise RuntimeError(
                    'reboot restored a different or ambiguous text scale')
            print('[installer-qemu] exact text scale restored after reboot: PASS',
                  flush=True)
            if re.findall(r'^\[SETTINGS\] wallpaper_restored=(\d+)\r?$', text, re.M) != ['1']:
                raise RuntimeError('reboot restored a different or ambiguous wallpaper')
            print('[installer-qemu] exact wallpaper restored after reboot: PASS', flush=True)
        print('[installer-qemu] ' + phase + ' and authenticated Home: PASS', flush=True)
    if args.password_profile:
        verify_missing_profile(disk, output, env, args.timeout)
    if args.desktop_lifecycle:
        partition = subprocess.check_output(
            ['sgdisk', '--info=2', str(disk)], text=True, env=dict(env, LC_ALL='C'))
        match = re.search(r'^First sector: (\d+)\b', partition, re.M)
        if not match or "Partition name: 'Kurogane Root'" not in partition:
            raise RuntimeError('cannot verify Files mutation on the installed root partition')
        image = f'{disk}@@{int(match[1]) * 512}'
        listing = subprocess.check_output(['mdir', '-i', image, '::/'], text=True, env=env)
        if not re.search(r'^flx8ok\s+<DIR>', listing, re.I | re.M) or re.search(
                r'^flx8qa\s+<DIR>', listing, re.I | re.M):
            raise RuntimeError('Files mkdir/rename did not persist exactly one final directory')
        print('[installer-qemu] Files mkdir/rename persisted after reboot: PASS', flush=True)
    manifest = {'iso': str(iso), 'iso_sha256': digest(iso), 'disk_sha256': digest(disk),
                'source_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'harness_sha256': digest(Path(__file__)), 'restored_theme': expected_theme,
                'restored_text_scale': expected_text_scale,
                'restored_wallpaper': 1 if args.desktop_lifecycle else None,
                'password_profile': args.password_profile,
                'result': 'PASS'}
    (output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('[installer-qemu] ISO -> new disk -> first boot -> persistent reboot: PASS', flush=True)


if __name__ == '__main__':
    main()
