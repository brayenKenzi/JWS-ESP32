// Reference executable is kept outside this project; never bundled into firmware.
// Usage: node scripts/generate-fixtures.cjs ../reference/PrayTimes.js
const fs = require('node:fs');
const vm = require('node:vm');
const crypto = require('node:crypto');
const path = require('node:path');
const source = fs.readFileSync(process.argv[2], 'utf8');
const sandbox = {};
vm.createContext(sandbox);
vm.runInContext(source, sandbox);
const cases = [
  ['Makassar', [2026,11,3], -5.1477, 119.4327, 480],
  ['Jakarta', [2026,3,20], -6.2088, 106.8456, 420],
  ['Jayapura', [2026,6,21], -2.5337, 140.7181, 540],
  ['Makkah', [2026,1,15], 21.4225, 39.8262, 180],
  ['Makassar leap', [2024,2,29], -5.1477, 119.4327, 480],
];
const fixtures = cases.map(([name,date,latitude,longitude,utcOffsetMinutes]) => {
  const calculator = new sandbox.PrayTimes('Karachi');
  calculator.adjust({fajr:20, isha:18, asr:'Standard', imsak:'10 min', highLats:'None'});
  calculator.tune({imsak:2, fajr:2, dhuhr:2, asr:2, maghrib:2, isha:2});
  const times = calculator.getTimes(date, [latitude,longitude,0], utcOffsetMinutes/60, 0, 'Float');
  const minutes = ['imsak','fajr','sunrise','dhuhr','asr','maghrib','isha'].map(k => Math.floor(times[k]*60+0.5));
  return {name,date,latitude,longitude,utcOffsetMinutes,minutes};
});
fs.writeFileSync(path.join(__dirname, '../test/fixtures.json'), JSON.stringify({
  reference: 'PrayTimes.js v2.5, Hamid Zarrabi-Zadeh / PrayTimes.org, LGPL-3.0',
  url: 'https://praytimes.org/code/v2/js/PrayTimes.js',
  sha256: crypto.createHash('sha256').update(source).digest('hex'),
  settings: 'fajr=20, isha=18, imsak=10min, ihtiyat=2 except sunrise, asr=Standard, elevation=0, highLats=None, DST=0',
  fixtures
}, null, 2) + '\n');
