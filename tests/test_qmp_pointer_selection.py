"""Exercise the actual embedded pointer selector without a QEMU/socket dependency."""
import ast
from pathlib import Path
import re
source = (Path(__file__).resolve().parents[1] / 'scripts/smoke-uefi-iso-qemu.sh').read_text()
script = source.split('send_qemu_click() {', 1)[1].split("<<'PY'\n", 1)[1].split('\nPY', 1)[0]
tree = ast.parse(script)
assignment = next(node for node in ast.walk(tree) if isinstance(node, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == 'pattern' for t in node.targets))
code = compile(ast.Expression(assignment.value), '<pointer-pattern>', 'eval')
for kind, device in [('usb-tablet', 'Tablet'), ('usb-mouse', 'Mouse')]:
    pattern = eval(code, {'pointer_kind': kind})
    for brand in ['USB', 'HID']:
        text = f'  Mouse #0: PS/2 Mouse\r\n* Mouse #2: QEMU {brand} {device} (absolute)\r\n'
        assert re.search(pattern, text, re.IGNORECASE).group(1) == '2'
    other = 'Mouse' if device == 'Tablet' else 'Tablet'
    assert re.search(pattern, f'Mouse #3: QEMU HID {other}\n', re.IGNORECASE) is None
    assert re.search(pattern, 'Mouse #0: PS/2 Mouse\n', re.IGNORECASE) is None
print('QMP pointer selection: PASS')
