#!/usr/bin/env python3
from pathlib import Path

path = Path('.github/workflows/qualify-network-events.yml')
text = path.read_text()
old = '''          command('qmp_capabilities')
          filters = command('query-rx-filter')
          nic_name = None
          for entry in filters:
              if str(entry.get('main-mac', '')).lower() == '52:54:00:4b:55:01':
                  nic_name = entry.get('name')
                  break
          if not nic_name:
              raise RuntimeError('E1000 net client not found in query-rx-filter: ' + repr(filters))
          print('[network-events] QMP net client:', nic_name)

          wait_marker('[TEST] network_events_armed: PASS', 180.0)
          command('set_link', {'name': nic_name, 'up': False})
'''
new = '''          command('qmp_capabilities')
          wait_marker('[TEST] network_events_armed: PASS', 180.0)

          # QEMU 8.2 can return an empty query-rx-filter list for this E1000
          # setup. Resolve the set_link target by asking QMP itself. Testing an
          # already-up link with up=true is a no-op and only accepts a name that
          # QEMU has actually registered; no guest state or event is faked.
          def set_link_if_known(name, up):
              global sequence
              sequence += 1
              ident = f'kuro-{sequence}'
              payload = {
                  'execute': 'set_link',
                  'arguments': {'name': name, 'up': up},
                  'id': ident,
              }
              stream.write((json.dumps(payload) + '\\n').encode('utf-8'))
              while True:
                  reply = read_object()
                  if reply.get('id') != ident:
                      continue
                  return 'error' not in reply, reply.get('error')

          nic_name = None
          rejected = []
          for candidate in ('kurogane_nic', 'e1000.0', 'kurogane_net'):
              accepted, error = set_link_if_known(candidate, True)
              if accepted:
                  nic_name = candidate
                  break
              rejected.append((candidate, error))
          if nic_name is None:
              raise RuntimeError('QMP set_link could not resolve E1000 target: ' + repr(rejected))
          print('[network-events] QMP set_link target:', nic_name)

          command('set_link', {'name': nic_name, 'up': False})
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'network-events QMP anchor count={count}, expected 1')
path.write_text(text.replace(old, new, 1))
print('network-events QMP target resolver patched')
