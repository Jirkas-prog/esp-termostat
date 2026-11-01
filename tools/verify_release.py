"""Check packaged images against final source, staged builds, partitions and hashes."""
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import yaml

root = Path(__file__).resolve().parents[1]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

for board, chip_id, boot_offset in [('tdisplay', 0, 0x1000), ('c3', 5, 0), ('c6', 13, 0)]:
    release = root / 'releases' / '0.1.0' / board
    manifest = json.loads((release / 'manifest.json').read_text())
    for name, expected in manifest['sha256'].items():
        assert digest(release / name) == expected, (board, name)
    for name, expected in manifest['source_sha256'].items():
        assert digest(root / name) == expected, ('outdated source', board, name)
        stage_name = name.removeprefix('firmware/') if name.startswith('firmware/') else name
        assert digest(Path('C:/Espressif/thermostat-40') / board / stage_name) == expected, ('stale build', board, name)
    image = (release / 'esp_termostat.bin').read_bytes()
    assert image[0] == 0xe9 and struct.unpack_from('<H', image, 12)[0] == chip_id
    factory = (release / 'factory.bin').read_bytes()
    assert factory[0x10000:0x10000+len(image)] == image
    boot = (release / 'bootloader.bin').read_bytes()
    assert factory[boot_offset:boot_offset+len(boot)] == boot
    parts = (release / 'partition-table.bin').read_bytes()
    assert factory[0x8000:0x8000+len(parts)] == parts
    entries = []
    for i in range(0, len(parts), 32):
        entry = parts[i:i+32]
        if len(entry) != 32 or entry[:2] != b'\xaaP': break
        _, typ, sub, address, size, name, flags = struct.unpack('<HBBII16sI', entry)
        assert address + size <= 4 * 1024 * 1024
        entries.append((typ, sub, address, size, name.split(b'\0')[0]))
    app = next(p for p in entries if p[0] == 0)
    assert app[2] == 0x10000 and len(image) <= app[3]
    print(f'PASS {board}: {len(image):,} B app, {len(factory):,} B factory, chip ID {chip_id}, hashes and source match')

# Reject duplicated YAML mapping keys rather than accepting silent overrides.
class UniqueLoader(yaml.SafeLoader):
    pass
def mapping(loader, node, deep=False):
    out = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        assert key not in out, ('duplicate YAML key', key)
        out[key] = loader.construct_object(value_node, deep=deep)
    return out
UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
config = yaml.load((root / 'home-assistant/termostat.yaml').read_text(encoding='utf-8'), Loader=UniqueLoader)
assert len(config['automation']) == 3
assert len(config['mqtt']['sensor']) == 6 and len(config['mqtt']['binary_sensor']) == 7
ids = [e['unique_id'] for values in config['mqtt'].values() for e in values]
assert len(ids) == len(set(ids))
for a in config['automation'][:2]:
    assert a['actions'][0]['data']['retain'] is False
ET.parse(root / 'docs/ZAPOJENI.svg')
print('PASS YAML structure, unique entity IDs, non-retained ACKs, SVG XML')
