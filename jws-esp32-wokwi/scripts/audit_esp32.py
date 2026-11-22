"""Read-only ESP32/Wokwi consistency audit; never flashes or erases a board."""
from pathlib import Path
import configparser
import hashlib
import json
import re

root = Path(__file__).resolve().parents[1]
ini = configparser.ConfigParser(interpolation=None)
ini.read(root/'platformio.ini', encoding='utf-8-sig')
diagram = json.loads((root/'diagram.json').read_text(encoding='utf-8-sig'))
source = (root/'src/SimulationDrivers.cpp').read_text(encoding='utf-8-sig')
header = (root/'src/SimulationDrivers.h').read_text(encoding='utf-8-sig')
connections = {frozenset(c[:2]) for c in diagram['connections']}
expected = {18:'SCK',23:'MOSI',19:'MISO',27:'CS',26:'D/C'}
checks = []
def check(name, passed, detail):
    checks.append({'name':name,'passed':bool(passed),'detail':detail})

check('board', ini['env:sim']['board']=='esp32doit-devkit-v1', ini['env:sim']['board'])
check('framework', ini['env:sim']['framework']=='arduino', ini['env:sim']['framework'])
check('SPI firmware', re.search(r'SPI\.begin\(\s*18\s*,\s*19\s*,\s*23\s*,\s*27\s*\)',source), 'SCK18 / MISO19 / MOSI23 / CS27')
check('display constructor', re.search(r'tft_\{&SPI,\s*26,\s*27,\s*-1\}',header), 'DC26 / CS27 / no reset pin')
for pin, signal in expected.items():
    check('wire '+signal, frozenset([f'esp:D{pin}',f'tft:{signal}']) in connections, f'GPIO{pin} to TFT {signal}')
check('TFT power', frozenset(['esp:VIN','tft:VCC']) in connections and frozenset(['esp:GND.1','tft:GND']) in connections, 'Virtual TFT VCC=VIN, common ground; physical module supply is not validated')
outputs = {18,23,26,27}
check('strapping/output pins', not outputs.intersection({0,2,5,12,15,34,35,36,37,38,39}), 'TFT outputs avoid ESP32 boot strapping and input-only pins')
for file, environment in [('wokwi.toml','sim'),('wokwi.mock.toml','sim_mock')]:
    contents = (root/file).read_text(encoding='utf-8-sig')
    check(file+' path', f'.pio/build/{environment}/firmware.bin' in contents and f'.pio/build/{environment}/firmware.elf' in contents, environment)
    check(file+' HTTP forward', "localhost:8180" in contents and "target:80" in contents, 'localhost:8180 -> ESP32 virtual HTTP80')

source_files = list((root/'src').glob('*'))+list((root/'include').glob('*.h'))+list((root/'lib/JwsCore/src').glob('*'))+[root/'platformio.ini']
newest_source = max(p.stat().st_mtime_ns for p in source_files if p.is_file())
artifacts = []
for environment in ['sim','sim_mock']:
    for suffix in ['bin','elf']:
        p = root/f'.pio/build/{environment}/firmware.{suffix}'
        artifacts.append({'path':str(p.relative_to(root)), 'exists':p.exists(),
                          'bytes':p.stat().st_size if p.exists() else None,
                          'olderThanSource':p.stat().st_mtime_ns < newest_source if p.exists() else True,
                          'sha256':hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None})

platformio = Path.home()/'.platformio'
board = json.loads((platformio/'platforms/espressif32/boards/esp32doit-devkit-v1.json').read_text())
framework = json.loads((platformio/'packages/framework-arduinoespressif32/package.json').read_text())
sdk_header = (platformio/'packages/framework-arduinoespressif32/tools/sdk/esp32/include/esp_common/include/esp_idf_version.h').read_text()
sdk = '.'.join(re.search(r'ESP_IDF_VERSION_'+part+r'\s+(\d+)',sdk_header).group(1) for part in ['MAJOR','MINOR','PATCH'])
report = {'skill':'kenzi-esp32-lab','target':{'board':ini['env:sim']['board'],'chipTarget':board['build']['mcu'],
          'variant':board['build']['variant'],'cpuHz':int(board['build']['f_cpu'].rstrip('L')),
          'flash':board['upload']['flash_size'],'flashMode':board['build']['flash_mode'],
          'flashHz':int(board['build']['f_flash'].rstrip('L')),'psram':'not configured; physical module not inspected',
          'platform':ini['env:sim']['platform'],'frameworkPackage':framework['version'],'espIdf':sdk},
          'checks':checks,'artifacts':artifacts,'hardwareFlashed':False,'hardwareValidated':False,
          'limits':['Static checks and firmware build do not prove simulator execution.',
                    'P5/P10, DFPlayer, RTC hardware, radio, audio timing and physical power are not validated.']}
output = root/'evidence/esp32-lab-audit.json'
output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
for entry in checks: print(('PASS' if entry['passed'] else 'FAIL')+' '+entry['name'])
for artifact in artifacts:
    print(('STALE' if artifact['olderThanSource'] else 'CURRENT')+' '+artifact['path'])
print('Audit report:',output)
raise SystemExit(0 if all(c['passed'] for c in checks) else 1)
