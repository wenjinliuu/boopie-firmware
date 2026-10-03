# Boopie's assets

Everything here is packed by `tools/boopie/pack_assets.py` into the `assets`
partition (docs/boopie-storage.md): sounds and themes. The UI font is built
into the firmware instead (components/boopie/font/ui.otf). `VERSION` is the
pack's version; bump it whenever something here changes, and the firmware
asks for at least the version it was built with.

Paths here are the names the firmware asks for (`sounds/eat.wav`).
