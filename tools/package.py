"""Package already compiled IDF images without touching a connected board."""
import hashlib
import json
from pathlib import Path
import shutil
import sys

out, board, target = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
root = Path(__file__).resolve().parents[1]
release = root / 'releases' / '0.1.0' / board
release.mkdir(parents=True, exist_ok=True)
meta = json.loads((out / 'flasher_args.json').read_text())
factory = bytearray(b'\xff' * max(int(a, 0) + (out / p).stat().st_size for a, p in meta['flash_files'].items()))
for address, source in meta['flash_files'].items():
    data = (out / source).read_bytes()
    start = int(address, 0)
    factory[start:start+len(data)] = data
    shutil.copy2(out / source, release / Path(source).name)
(release / 'factory.bin').write_bytes(factory)
shutil.copy2(out / 'dependencies.lock', release / 'dependencies.lock')
manifest = {'version': '0.1.0', 'board': board, 'chip': target, 'flash_size': '4MB',
            'factory_offset': '0x0', 'status': 'BENCH ONLY - physical commissioning required',
            'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in release.glob('*.bin')}}
manifest['source_sha256'] = {str(p.relative_to(root)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                           for base in (root / 'common', root / 'firmware') for p in base.rglob('*')
                           if p.is_file() and ('managed_components' not in p.parts) and p.name not in ('sdkconfig', 'sdkconfig.old')}
(release / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
(release / 'CTI-ME.txt').write_text(
    f'ESP TERMOSTAT 0.1.0 / {board} / {target}\n'
    'Zkusebni firmware. Pred pripojenim ke kamnum projdi docs/UVEDENI_DO_PROVOZU.md.\n'
    'Vychozi rele: 5V monostabilni pres MOSFET, aktivni HIGH, kontakt NC.\n'
    'Bez commissioning je pozadavek cerpadla trvale ON. Wi-Fi neni prednastavena.\n'
    'Tento balicek nebyl nahrany ani overeny na fyzicke desce.\n'
    'factory.bin je pro prvni instalaci: prepisuje i oblast NVS a tim maze nastaveni a vazbu sondy.\n'
    'Pro aktualizaci stejne tabulky oddilu na stole pouzij esp_termostat.bin na adrese 0x10000.\n'
    f'USB: python -m esptool --chip {target} --port COMx write_flash 0x0 factory.bin\n'
    'Port COMx nejprve identifikuj. Jiny cilovy chip je nepripustny.\n', encoding='utf-8')
print(f'Packaged {release}')
