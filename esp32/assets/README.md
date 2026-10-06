# Boopie's assets

Everything here is packed by `tools/boopie/pack_assets.py` into the `assets`
partition (docs/boopie-storage.md): sounds and themes. The build also packs
the UI font (components/boopie/font/ui.otf), the pixel font's bitmaps and the
small world's backgrounds from their sources (`--firmware-data`): the device
reads those from flash instead of keeping them in PSRAM. `VERSION` is the
pack's version; bump it whenever something here changes, and the firmware
asks for at least the version it was built with.

Paths here are the names the firmware asks for (`sounds/eat.wav`).
