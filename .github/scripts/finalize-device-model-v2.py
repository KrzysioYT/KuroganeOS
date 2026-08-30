from pathlib import Path

p = Path('kernel/drivers/core/device_manager.cpp')
text = p.read_text()
old = '''    for (size_t index = 0; index < MAXIMUM_DEVICES; ++index) {
        g_devices[index] = {};
    }
'''
new = '''    for (size_t index = 0; index < MAXIMUM_DEVICES; ++index) {
        const uint32_t generation = g_devices[index].generation;
        g_devices[index] = {};
        g_devices[index].generation = generation;
        g_devices[index].id = static_cast<DeviceId>(index);
        g_devices[index].driver = INVALID_DRIVER_ID;
        g_devices[index].parent = INVALID_DEVICE_ID;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f'device generation-preservation anchor count={text.count(old)}')
p.write_text(text.replace(old, new, 1))

todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
bullet = '- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.\n'
while text.count(bullet) > 1:
    first = text.find(bullet)
    second = text.find(bullet, first + len(bullet))
    text = text[:second] + text[second + len(bullet):]
todo.write_text(text)
print('Device Model v2 post-patch applied')
