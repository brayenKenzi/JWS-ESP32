# Official Kemenag Kotamobagu research — 5 November 2026

The public official webpage https://bimasislam.kemenag.go.id/jadwal-sholat
loads its province/city selectors and timetable using the same-origin public
proxy below. This was confirmed by reading that page's publicly served
`2jioglghi_rbv.js` bundle. No credentials, private API tokens, or third-party
mirrors were used.

- `GET /api/sihat/provinsi` -> `{data:[{id,name},...]}`
- `GET /api/sihat/kabko?prov=<opaque-public-id>` -> same selector shape
- `GET /api/sihat/jadwal?prov=<id>&kabko=<id>&thn=2026&bln=11`
  -> `{status:1,message:"Success",prov,kabko,lintang,bujur,data:{date:row}}`

Select exact names **SULAWESI UTARA** and **KOTA KOTAMOBAGU**. IDs are opaque
public selector values, not private API tokens; resolve them afresh instead of
depending indefinitely on a hardcoded encrypted string. The actual private
SIHAT developer API has its own approval/token/IP rules and is not used here.
The public webpage proxy has no published stability guarantee, so firmware
must retain validated cache and surface fetch failure or missing dates.

Official reference coordinates in the returned table:
`0°44'14.4 N`, `124°18'59.95 E` = approximately latitude `0.7373333333`,
longitude `124.3166527778`. This is the published city reference, not a measured
coordinate of the user's home or mosque. Mongkonai Barat is represented using
the Kota Kotamobagu city/regency timetable, not a separate kelurahan schedule.

`kotamobagu-2026.json` contains all **365 validated official dates** in order,
including raw-response provenance per month. Time field order:
`imsak,subuh,terbit,dzuhur,ashar,maghrib,isya`. `terbit` maps to `Syuruq`.
Minutes are integer minutes after local midnight, WITA / Asia/Makassar / UTC+8.
No ihtiyat or custom correction has been added. Keep manual corrections
separate and default them to zero when using this official timetable.

Official 2026-11-05 row: Imsak03:56,Subuh04:06,Terbit05:20,Dzuhur11:30,
Ashar14:50,Maghrib17:32,Isya18:43.

The normalized annual JSON SHA-256 is
`7d66506aedd89d0b17b7beadeb3aa759e55634a283e465a231e1e4661873a083`.
Raw month files, their source URLs and retrieval times are retained separately.
`fetch_kemenag.py` can fetch and validate the year again or validate the retained
raw evidence with `--from-saved .`.

TLS was verified with Python's default trust store, TLS1.3. The verified chain is
`*.kemenag.go.id` -> `GeoTrust TLS RSA CA G1` -> `DigiCert Global Root G2`.
`kemenag-root-ca.pem` is the last, self-signed public root certificate (expires
2038-01-15). `tls-chain.json` contains subjects, issuers and expiry dates.
Use normal CA and hostname verification, plus a valid system clock; do not use
`setInsecure`. Certificates/upstream configuration can change later.

The normalized dataset is copied to data/kemenag-kotamobagu-2026.json.
include/KemenagData.h is generated from that same verified dataset; runtime corrections are separate.
The original reference repository remains unchanged. Regenerate from project root with:

```powershell
python scripts/fetch_kemenag.py --year 2026 --output data/kemenag-kotamobagu-2026.json
```
For another year, supply both --year and a matching --output filename; the header is regenerated too.
