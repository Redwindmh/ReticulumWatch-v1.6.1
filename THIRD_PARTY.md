# Third-party dependencies — ReticulumWatch M9-build5

ReticulumWatch uses PlatformIO-managed dependencies. Protocol/storage and M9 voice dependencies are explicitly pinned where reproducibility matters.

Current notable dependencies include:

- `attermann/microReticulum @ 0.5.0`
- `attermann/microStore @ 0.1.7`
- `sh123/esp32_codec2 @ 1.0.7`
- `bblanchon/ArduinoJson @ 7.4.2`
- `hideakitai/MsgPack @ 0.4.2`
- `rweather/Crypto @ 0.4.0`
- `lvgl/lvgl @ 9.4.0`
- `jgromes/RadioLib @ 7.4.0`
- `xinyuan-lilygo/LilyGoLib @ 0.2.0`
- `lewisxhe/XPowersLib @ 0.3.1`
- `lewisxhe/SensorLib @ 0.3.3`
- `mikalhart/TinyGPSPlus @ 1.1.0`
- IRremoteESP8266's `feature/C++20-compatibility` branch archive as retained by the accepted baseline.

Consult each upstream project for its authoritative license text and notices.

## M9-build5 Codec2 dependency

Recorded LXMF voice uses `sh123/esp32_codec2 @ 1.0.7`, whose upstream project and PlatformIO registry entry identify the library as **GPL-3.0** licensed.

M9-build5 uses its public Codec2 C API with `CODEC2_MODE_700C` for outbound/Sideband-compatible voice and `CODEC2_MODE_450` only for playback compatibility with existing M9-build4 sidecars. ReticulumWatch does not vendor or modify the library source inside this repository; PlatformIO resolves the pinned dependency during the build.

Because GPL-licensed code is now linked into firmware, binary/source redistribution should be reviewed against the GPL-3.0 obligations and the licenses of the rest of the project before a public binary release. This file is dependency documentation, not legal advice.

Upstream:

- https://github.com/sh123/esp32_codec2_arduino
- https://registry.platformio.org/libraries/sh123/esp32_codec2

## Current microReticulum / microStore filesystem contract

The hardware-accepted endpoint uses:

```text
USTORE_USE_NOOPFS=1
```

and application-owned persistence. ReticulumWatch owns its persistent identity/history/configuration paths; optional internal RNS filesystem persistence must not gate protocol startup. Earlier M6 experiments with POSIX/chdir-based internal persistence were reverted during M6-build3 recovery and are retained only in historical milestone notes.

M9-build5 does not patch microReticulum source and does not enable Resource transfer, Transport Node mode, or any alternate Reticulum persistence lifecycle.
