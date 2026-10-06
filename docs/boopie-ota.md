# Boopie OTA

Boopie updates itself from its own server, a Cloudflare Worker in front of an
R2 bucket (`cloud/ota-worker`). Muse's update commands (BLE, Wi-Fi setup, the
control channel) are turned away: `CONFIG_HOMEHUB_OTA_ENABLED=n`.

## On the board (`components/boopie/ota`)

- Once a day (first check three minutes after Wi-Fi comes up), and from
  Settings > 系统更新, it fetches `manifest.json` and `manifest.sig`, checks the
  ECDSA P-256 signature against `CONFIG_BOOPIE_OTA_PUBKEY`, and compares the
  version with its own. Something newer: the pet says so, the page shows the
  notes, and nothing is installed until 立即更新 is tapped twice.
- The app is written to the other OTA slot and checked against its SHA-256;
  the assets pack is downloaded to the user data partition (Range requests
  resume a dropped download) and written over the assets partition at the
  first boot of the app it came with, header last, before anything reads it.
- A broken assets pack is put right without asking.
- Each release has two builds: 正常版 and 解锁版 (`BOOPIE_UNLOCK_ALL`, every
  skin, accessory, colour and background open). A board follows its own and
  can switch to the other on the 系统更新 page; saved data is kept either way.
- The server's name goes through the VPN when it's on; TLS is checked end to
  end as for any other site.
- A new app is kept once the UI has run for 45 s with Wi-Fi up (or not set
  up); otherwise the bootloader goes back to the old one.

## Releasing

1. Once: `python3 tools/boopie/ota_keygen.py` on your own computer. Put the
   whole of `boopie-ota-key.pem` in the GitHub secret `BOOPIE_OTA_KEY` and keep
   the file safe and offline. Add `CLOUDFLARE_API_TOKEN` (Workers Scripts:
   Edit, Workers R2 Storage: Edit) and `CLOUDFLARE_ACCOUNT_ID`.
2. Each release: `git tag -a v1.2.0 -m "what's new"` and push the tag.
   `.github/workflows/boopie-release.yml` builds that version with the
   server's address and the public key, signs the manifest, uploads to R2,
   deploys the worker and makes a GitHub release with the full flash image.
3. Boards flashed by hand need a build with the key in it once: flash the
   release's `boopie-<version>-merged.bin` at 0x0 (it erases nothing on the
   user data partition beyond what the image covers; see boopie-storage.md).

Local builds have no server or key (`CONFIG_BOOPIE_OTA_URL` empty) and say
so on the page.
