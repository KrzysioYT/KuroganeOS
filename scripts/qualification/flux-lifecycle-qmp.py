#!/usr/bin/env python3
"""Exercise real Flux windows through USB input; assert guest-owned WM state."""
import argparse
import json
from pathlib import Path
import re
import socket
import time


def records(text):
    result = []
    for line in text.splitlines(keepends=True):
        if not line.endswith('\n') or not line.startswith('[WM] '):
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        required = {'action', 'id', 'pid', 'app', 'state', 'x', 'y', 'w', 'h',
                    'focus', 'windows', 'surfaces', 'bytes', 'sw', 'sh'}
        if not required <= fields.keys():
            raise RuntimeError(f'incomplete WM record: {line!r}')
        result.append({k: v if k == 'action' else int(v) for k, v in fields.items()})
    return result


class Harness:
    def __init__(self, args):
        self.serial = Path(args.serial)
        self.output = Path(args.output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.deadline = time.monotonic() + args.timeout
        self.results = []
        self.settings_write = getattr(args, "settings_write", False)
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.connection.settimeout(3)
        self.connection.connect(args.qmp)
        self.stream = self.connection.makefile('rwb', buffering=0)
        if 'QMP' not in json.loads(self.stream.readline()):
            raise RuntimeError('missing QMP greeting')
        self.execute('qmp_capabilities')

    def execute(self, command, arguments=None):
        self.stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError('QMP disconnected')
            message = json.loads(line)
            if 'error' in message:
                raise RuntimeError(message['error'])
            if 'return' in message:
                return message['return']

    def read(self):
        text = self.serial.read_text(errors='replace')
        if re.search(r'PANIC_BEGIN|FATAL ERROR|\[(?:TEST|SESSION|INPUT)\][^\n]*: FAIL', text):
            raise RuntimeError('guest reported a failure; inspect serial.log')
        return records(text)

    def wait_marker(self, marker, previous=0):
        deadline = min(self.deadline, time.monotonic() + 30)
        while time.monotonic() < deadline:
            self.read()
            if self.serial.read_text(errors='replace').count(marker) > previous:
                return
            time.sleep(.1)
        raise TimeoutError('no fresh marker: ' + marker)

    def mark(self):
        return len(self.read())

    def wait(self, start, action, app=1, window=None):
        deadline = min(self.deadline, time.monotonic() + 30)
        while time.monotonic() < deadline:
            for event in self.read()[start:]:
                if (event['action'] == action and event['app'] == app
                        and (window is None or event['id'] == window)):
                    self.results.append(event)
                    print('[lifecycle] ' + json.dumps(event), flush=True)
                    return event
            time.sleep(.1)
        raise TimeoutError(f'no fresh {action} for app={app}, window={window}')

    def key(self, *keys):
        self.execute('send-key', {'keys': [{'type': 'qcode', 'data': key} for key in keys], 'hold-time': 100})
        time.sleep(.2)

    def pointer(self, info, x, y):
        self.execute('input-send-event', {'events': [
            {'type': 'abs', 'data': {'axis': 'x', 'value': round(x * 32767 / (info['sw'] - 1))}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': round(y * 32767 / (info['sh'] - 1))}}]})
        time.sleep(.15)

    def button(self, down):
        self.execute('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(.15)

    def control(self, info, index):
        # Production chrome: 22px controls, 4px gaps, 8px right inset, 36px header.
        self.pointer(info, info['x'] + info['w'] - 19 - index * 26, info['y'] + 18)
        self.button(True)
        self.button(False)

    def screenshot(self, name):
        target = self.output / (name + '.ppm')
        if target.exists():
            raise FileExistsError(target)
        time.sleep(.3)
        self.execute('screendump', {'filename': str(target)})

    def run(self):
        time.sleep(2)  # Let Home's normal autostart finish before resource checks.
        start = self.mark()
        self.key('ctrl', 'alt', 'f2')
        terminal = self.wait(start, 'present')
        ident = terminal['id']
        assert terminal['pid'] > 0 and terminal['state'] == 0
        self.screenshot('01-terminal')

        start = self.mark()
        self.control(terminal, 2)
        minimized = self.wait(start, 'minimize', window=ident)
        assert minimized['state'] == 1 and minimized['focus'] != ident
        start = self.mark()
        self.key('ctrl', 'alt', 'f2')
        restored = self.wait(start, 'restore', window=ident)
        assert restored['state'] == 0 and restored['focus'] == ident
        assert all(restored[k] == terminal[k] for k in ('x', 'y', 'w', 'h', 'pid'))

        start = self.mark()
        self.control(restored, 1)
        maximized = self.wait(start, 'maximize', window=ident)
        assert maximized['state'] == 2 and maximized['w'] > terminal['w']
        self.screenshot('02-maximized')
        start = self.mark()
        self.control(maximized, 2)
        assert self.wait(start, 'minimize', window=ident)['state'] == 1
        start = self.mark()
        self.key('ctrl', 'alt', 'f2')
        maximized = self.wait(start, 'restore', window=ident)
        assert maximized['state'] == 2
        start = self.mark()
        self.control(maximized, 1)
        restored = self.wait(start, 'restore', window=ident)
        assert restored['state'] == 0
        assert all(restored[k] == terminal[k] for k in ('x', 'y', 'w', 'h'))

        start = self.mark()
        x, y = restored['x'] + 100, restored['y'] + 18
        self.pointer(restored, x, y)
        self.button(True)
        self.pointer(restored, x + 50, y + 40)
        self.button(False)
        moved = self.wait(start, 'move', window=ident)
        assert moved['x'] > restored['x'] and moved['y'] > restored['y']
        start = self.mark()
        x, y = moved['x'] + moved['w'] - 5, moved['y'] + moved['h'] - 5
        self.pointer(moved, x, y)
        self.button(True)
        self.pointer(moved, x + 35, y + 25)
        self.button(False)
        resized = self.wait(start, 'resize', window=ident)
        assert resized['w'] > moved['w'] and resized['h'] > moved['h']
        self.screenshot('03-resized')

        start = self.mark()
        self.key('ctrl', 'alt', 'f3')
        files = self.wait(start, 'present', app=2)
        assert files['focus'] == files['id'] and files['pid'] != terminal['pid']
        start = self.mark()
        self.key('ctrl', 'alt', 'f2')
        assert self.wait(start, 'focus', window=ident)['focus'] == ident
        start = self.mark()
        self.control(resized, 0)
        closed = self.wait(start, 'close', window=ident)
        assert closed['focus'] != ident
        assert closed['windows'] == files['windows'] - 1
        assert closed['surfaces'] == files['surfaces'] - 1
        assert closed['bytes'] < files['bytes']
        time.sleep(1)
        start = self.mark()
        self.key('ctrl', 'alt', 'f2')
        reopened = self.wait(start, 'present')
        assert reopened['id'] != ident and reopened['pid'] != terminal['pid']
        assert reopened['windows'] == files['windows']
        self.screenshot('04-reopened')
        for cycle in range(3):
            start = self.mark()
            self.control(reopened, 0)
            closed = self.wait(start, 'close', window=reopened['id'])
            assert closed['windows'] == files['windows'] - 1
            assert closed['surfaces'] == files['surfaces'] - 1
            assert closed['bytes'] < files['bytes']
            # Compare the actual release baseline, without assuming payload size.
            if cycle == 0:
                released_bytes = closed['bytes']
            else:
                assert closed['bytes'] == released_bytes
            previous = reopened
            time.sleep(1)
            start = self.mark()
            self.key('ctrl', 'alt', 'f2')
            reopened = self.wait(start, 'present')
            assert reopened['pid'] != previous['pid'] and reopened['id'] != previous['id']
            assert all(reopened[k] == files[k] for k in ('windows', 'surfaces', 'bytes'))
        self.screenshot('05-churn-complete')
        start = self.mark()
        self.key('ctrl', 'alt', 'f7')
        settings = self.wait(start, 'present', app=6)
        self.wait_marker('[TEST] flux_settings_keyboard_ready: PASS')
        self.key('tab')
        self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS')
        if self.settings_write:
            self.key('ret')
            self.wait_marker('[TEST] flux_theme_persistence: PASS')
        # Focus survives the theme rebuild. Reverse Tab wraps to the final
        # profile control and scrolls it into view. Space then exercises
        # activation on the first control after forward wrap (writable root).
        self.key('shift', 'tab')
        self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS', 1)
        self.screenshot('06-settings-keyboard-scroll')
        self.key('tab')
        self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS', 2)
        self.screenshot('07-settings-keyboard-wrap')
        if self.settings_write:
            self.key('spc')
            self.wait_marker('[TEST] flux_theme_persistence: PASS', 1)
        start = self.mark()
        self.control(settings, 0)
        self.wait(start, 'close', app=6, window=settings['id'])
        (self.output / 'lifecycle.json').write_text(json.dumps(self.results, indent=2) + '\n')
        self.read()  # Fail even if a fault follows the last accepted transition.
        print('[lifecycle] PASS: minimize/restore/maximize/move/resize/focus/close/reopen/settings-keyboard', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('qmp', 'serial', 'output'):
        parser.add_argument('--' + field, required=True)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--settings-write', action='store_true',
                        help='also activate and save themes; requires writable root')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    harness = Harness(args)
    try:
        harness.run()
    finally:
        harness.stream.close()
        harness.connection.close()


if __name__ == '__main__':
    main()
