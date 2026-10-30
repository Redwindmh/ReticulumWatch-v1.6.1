# M4 Source Audit

M4 starts from the hardware-accepted `ReticulumWatch-M3-build3` tree and deliberately leaves the proven radio/Reticulum boundary unchanged.

## Frozen hashes

- `src/radio/RadioService.cpp` — `6d1a966c2b7d192831b679e3666fbd49bab7843c09b075ae2c2ee409c7fee415`
- `src/radio/RadioService.h` — `3e39860839dee2b75d0befd052b7fb18b72020e7076869a87267b18ea4a767fc`
- `src/config/RadioConfig.h` — `1e09d23c2c07d4dd0ae472e49353fa901b652ec913f3f0e1fc12059801e0da4e`
- `src/reticulum/WatchLoRaInterface.cpp` — `a0ec923752b65a02e9cdc267cbdbdbaba9e8bfdcf68f5fc95d8e00f1560a2e49`
- `src/reticulum/WatchLoRaInterface.h` — `f2ab916e2bdafa2cd3ad265ce537f275a71e9b9b1285008e01bc7c3548eb625c`
- `src/reticulum/ReticulumService.cpp` — `b10f3a3f81606b67a06c34fbd68be3fa233a105bf86e7c85a1d007b114a6e311`
- `src/reticulum/ReticulumService.h` — `66f67783c564a4280ef338cfa880140f22f899f1e97f16884f2203f810f4b910`
- `partitions/reticulum_watch_16mb.csv` — `c108905af498a1d2e348f66af2ebd03ac98c3c8b65ebdba9a177dbb652b21dff`
- `partitions/reticulum_watch_8mb.csv` — `e8109c3a8dbe1af484cf78fbcf43ebb222f356dffe34a595a6257e8ab7e60223`

## M4 change boundary

M4 changes only build metadata/fonts, watch HAL display/touch helpers, PowerManager, power-profile persistence, WatchUI/navigation, top-level power configuration, and documentation/version diagnostics. LXMF's protocol/send/receive implementation is unchanged except for the milestone name in its startup diagnostic.

LMX1/LMX2/LMX3 structures are unchanged. New power state persists separately at `/app/power.cfg`.
