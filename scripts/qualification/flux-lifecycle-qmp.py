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


def screenshot_region(path, x, y, w, h):
    magic, dimensions, depth, pixels = path.read_bytes().split(b'\n', 3)
    width, height = map(int, dimensions.split())
    if (magic != b'P6' or depth != b'255' or len(pixels) != width * height * 3
            or not (0 <= x < x + w <= width and 0 <= y < y + h <= height)):
        raise ValueError(f'invalid framebuffer sample: {path}, ({x}, {y})')
    return b''.join(pixels[(row * width + x) * 3:(row * width + x + w) * 3]
                    for row in range(y, y + h))


def screenshot_pixel(path, x, y):
    return screenshot_region(path, x, y, 1, 1).hex()


class Harness:
    def __init__(self, args):
        self.serial = Path(args.serial)
        self.output = Path(args.output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.deadline = time.monotonic() + args.timeout
        self.results = []
        self.settings_write = getattr(args, "settings_write", False)
        self.settings_session = getattr(args, "settings_session", False)
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
        # The guest floors tablet samples. Round the encoded value upwards
        # so a test that targets a one-pixel edge hits that exact pixel.
        dx, dy = info['sw'] - 1, info['sh'] - 1
        assert 0 <= x <= dx and 0 <= y <= dy
        self.execute('input-send-event', {'events': [
            {'type': 'abs', 'data': {'axis': 'x', 'value': (x * 32767 + dx - 1) // dx}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': (y * 32767 + dy - 1) // dy}}]})
        time.sleep(.15)

    def button(self, down):
        self.execute('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(.15)

    def scroll_down(self, info, x, y, steps):
        self.pointer(info, x, y)
        for _ in range(steps):
            self.execute('input-send-event', {'events': [
                {'type': 'btn', 'data': {'button': 'wheel-down', 'down': down}}
                for down in (True, False)]})
            time.sleep(.07)

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

    def verify_scaled_files_edge(self, files):
        start = self.mark()
        self.key('ctrl', 'alt', 'f3')
        files = self.wait(start, 'focus', app=2, window=files['id'])
        # Files starts in /, sorts directories first, and its first tile
        # follows the panel and breadcrumb. Click the *last painted pixel*
        # of the tile at 200%, not its center. Previously inverse rounding
        # sent that pixel to the first logical row below the tile.
        tile_x = 16 + 12
        tile_bottom = 16 + (38 + 6) + (22 + 6) + 68
        x = files['x'] + 4 + 2 * (tile_x + 40)
        y = files['y'] + 36 + 2 * tile_bottom - 1
        assert x < files['x'] + files['w'] - 4
        assert y < files['y'] + files['h'] - 4
        self.pointer(files, x, y)
        self.button(True)
        self.button(False)
        self.screenshot('08-files-200-percent')
        # History can go back only if the pointer really opened a folder.
        marker = '[TEST] flux_files_history: PASS'
        previous = self.serial.read_text(errors='replace').count(marker)
        self.key('alt', 'left')
        self.wait_marker(marker, previous)
        print('[lifecycle] Files tile edge at 200% opened a real directory: PASS',
              flush=True)

        # At this logical width there is one column. The next directory
        # moved under the first, so click the new row and verify its slot.
        wrapped = '[FILES] opened_tile=1'
        previous_tile = self.serial.read_text(errors='replace').count(wrapped)
        self.pointer(files, x, y + 2 * (68 + 8))
        self.button(True)
        self.button(False)
        self.wait_marker(wrapped, previous_tile)
        self.key('alt', 'left')
        self.wait_marker(marker, previous + 1)
        print('[lifecycle] Files wrapped directory tile at 200%: PASS', flush=True)

        # Scroll at the left margin, outside the directory tiles. The last
        # page ends with the two mutation buttons followed by a status label.
        button_rows = ((files['h'] - 40) // 2 - 16 - 22) // 40
        assert 2 <= button_rows <= 8

        def click_mutation(rows_from_end):
            previous_scrolls = self.serial.read_text(errors='replace').count(
                '[TEST] flux_files_controls_scroll: PASS')
            self.scroll_down(files, files['x'] + 12, files['y'] + 90, 16)
            self.wait_marker('[TEST] flux_files_controls_scroll: PASS', previous_scrolls)
            row_y = 16 + (button_rows - rows_from_end) * 40 + 17
            self.pointer(files, files['x'] + 100, files['y'] + 36 + 2 * row_y)
            self.button(True)
            self.button(False)

        click_mutation(2)
        for key in 'flx8qa':
            self.key(key)
        self.key('ret')
        self.wait_marker('[TEST] flux_files_mkdir: PASS')
        for key in 'flx8qa':
            self.key(key)
        self.pointer(files, files['x'] + 4 + 2 * (tile_x + 40),
                     files['y'] + 36 + 2 * (tile_bottom - 30))
        for down in (True, False):
            self.execute('input-send-event', {'events': [
                {'type': 'btn', 'data': {'button': 'right', 'down': down}}]})
            time.sleep(.15)
        click_mutation(1)
        for _ in range(6):
            self.key('backspace')
        for key in 'flx8ok':
            self.key(key)
        self.key('ret')
        self.wait_marker('[TEST] flux_files_rename: PASS')
        self.screenshot('09-files-rename-200-percent')
        print('[lifecycle] Files created and renamed a directory at 200%: PASS', flush=True)

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
        if self.settings_session:
            self.screenshot('06-settings-before-color')
            # Default scale. Content starts after 36px of chrome. The next
            # color button is centered 283px down the initial form.
            self.pointer(settings, settings['x'] + 100, settings['y'] + 36 + 283)
            self.button(True)
            self.button(False)
            self.wait_marker('[SETTINGS] theme_session=1')
            self.screenshot('07-settings-after-color')
            for window in (files, reopened, settings):
                # Sample inside content, left of the first layout column.
                x, y = window['x'] + 6, window['y'] + 46
                assert screenshot_pixel(self.output / '06-settings-before-color.ppm', x, y) == '090a0c'
                assert screenshot_pixel(self.output / '07-settings-after-color.ppm', x, y) == '0d0a16', window
            assert screenshot_pixel(self.output / '06-settings-before-color.ppm', 42, 92) == '8b1824'
            assert screenshot_pixel(self.output / '07-settings-after-color.ppm', 42, 92) == '6c46a1'
            # Select the next keyboard control from NEXT COLOR and apply
            # the preceding size (100% wraps to 200%).
            self.key('tab')
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=200')
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=150')
            self.key('tab')
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=200', 1)
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=100')
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=125')
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=150', 1)
            self.key('ret')
            self.wait_marker('[SETTINGS] text_scale_session=200', 2)
            assert '[SETTINGS] theme_saved=' not in self.serial.read_text(errors='replace')
            start = self.mark()
            self.control(settings, 0)
            self.wait(start, 'close', app=6, window=settings['id'])
            # Reveal the Home scene after the change. It was already open
            # before Settings, as were Files and Terminal.
            for app in (reopened, files):
                start = self.mark()
                self.control(app, 0)
                self.wait(start, 'close', app=app['app'], window=app['id'])
            home = next(r for r in self.read() if r['action'] == 'present' and r['app'] == 0)
            self.screenshot('08-home-after-color')
            assert screenshot_pixel(self.output / '08-home-after-color.ppm',
                                    home['x'] + 12, home['y'] + 24) == '0d0a16'
            for char in 'files':
                self.key(char)
            start = self.mark()
            self.key('ret')
            found = self.wait(start, 'present', app=2)
            start = self.mark()
            self.control(found, 0)
            self.wait(start, 'close', app=2, window=found['id'])
            self.scroll_down(home, home['x'] + 65, home['y'] + 70, 16)
            self.wait_marker('[TEST] flux_home_wheel_navigation: PASS')
            # With the Files search filter, the final page has one tile row
            # followed by Pin/Unpin and Log out at the default Home bounds.
            self.pointer(home, home['x'] + 100, home['y'] + 8 + 2 * (16 + 76 + 40 + 17))
            self.button(True)
            self.button(False)
            self.wait_marker('[TEST] desktop_logout_requested: PASS')
            self.wait_marker('[TEST] red_flux_login_surface: PASS', 1)
            self.key('ret')
            self.wait_marker('[SESSION] flux_home_first_frame: PASS', 1)
            (self.output / 'lifecycle.json').write_text(json.dumps(self.results, indent=2) + '\n')
            print('[lifecycle] PASS: live color and scale applied through Settings IPC', flush=True)
            return
        self.key('tab')
        self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS')
        if self.settings_write:
            self.key('ret')
            self.wait_marker('[TEST] flux_theme_persistence: PASS')
            self.key('tab')
            self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS', 1)
            # TEXT SIZE - wraps 100% to 200%, where a last-pixel hit
            # exercises the most visible edge error.
            self.key('ret')
            self.wait_marker('[TEST] flux_text_scale_persistence: PASS')
            self.wait_marker('[SETTINGS] text_scale_saved=200')
            start = self.mark()
            x = settings['x'] + settings['w'] - 5
            y = settings['y'] + settings['h'] - 5
            previous_height = settings['h']
            self.pointer(settings, x, y)
            self.button(True)
            self.pointer(settings, x, y - 150)
            self.button(False)
            settings = self.wait(start, 'resize', app=6, window=settings['id'])
            assert settings['h'] < previous_height
            self.key('shift', 'tab')
            self.wait_marker('[INPUT] flux_settings_keyboard_focus: PASS', 2)
        # Return to the color controls after exercising text-scale persistence.
        self.key('shift', 'tab')
        self.wait_marker(
            '[INPUT] flux_settings_keyboard_focus: PASS',
            3 if self.settings_write else 1)
        self.screenshot('06-settings-keyboard-scroll')
        self.key('tab')
        self.wait_marker(
            '[INPUT] flux_settings_keyboard_focus: PASS',
            4 if self.settings_write else 2)
        self.screenshot('07-settings-keyboard-wrap')
        if self.settings_write:
            self.key('spc')
            self.wait_marker('[TEST] flux_theme_persistence: PASS', 1)
        start = self.mark()
        self.control(settings, 0)
        self.wait(start, 'close', app=6, window=settings['id'])
        if self.settings_write:
            self.verify_scaled_files_edge(files)
            start = self.mark()
            self.key('ctrl', 'alt', 'f2')
            terminal = self.wait(start, 'focus', window=reopened['id'])
            output_samples = []
            for index, command in enumerate(('ls', 'echo', 'echo j', 'echo k')):
                for char in command:
                    self.key('spc' if char == ' ' else char)
                if command == 'ls':
                    self.screenshot('10-terminal-input-200-percent')
                    # The selected input border is Crimson after the installed
                    # Settings sequence, and fits below three output rows.
                    x = terminal['x'] + 4 + 2 * 16 + 24
                    y = terminal['y'] + 36 + 2 * (16 + 44 + 3 * 28)
                    assert screenshot_pixel(self.output / '10-terminal-input-200-percent.ppm',
                                            x, y) == 'bd2849'
                self.key('ret')
                if command == 'ls':
                    self.screenshot('11-terminal-ls-200-percent')
                else:
                    name = f'terminal-echo-{index}'
                    self.screenshot(name)
                    x = terminal['x'] + 4 + 2 * 16
                    y = terminal['y'] + 36 + 2 * (16 + 44 + 2 * 28) + 6
                    output_samples.append(screenshot_region(
                        self.output / (name + '.ppm'), x, y, 30, 20))
            assert len(set(output_samples)) == 3, 'empty echo, j and k must have distinct output'
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
    parser.add_argument('--settings-session', action='store_true',
                        help='click a color and exercise temporary UI scale on a Live ISO')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    if args.settings_write and args.settings_session:
        parser.error('settings storage modes are mutually exclusive')
    harness = Harness(args)
    try:
        harness.run()
    finally:
        harness.stream.close()
        harness.connection.close()


if __name__ == '__main__':
    main()
