# M9-build2 Source Audit

## Parent artifact

`ReticulumWatch-M9.zip`

Parent SHA-256:

`730c2c5137c4e90a5ebc4bb8f3cd83f75b2bd4315b4f647fb49483da339639ef`

M9 is the hardware-accepted local-audio parent. M9-build2 is a new immutable version and does not replace it.

## Frozen byte-identical boundary

Release preflight retains exact accepted SHA-256 checks for:

```text
src/reticulum/ReticulumService.cpp
src/reticulum/ReticulumService.h
src/reticulum/WatchLoRaInterface.cpp
src/reticulum/WatchLoRaInterface.h
src/radio/RadioService.cpp
src/radio/RadioService.h
src/config/RnsBuildConfig.h
src/config/RadioConfig.h
partitions/reticulum_watch_16mb.csv
partitions/reticulum_watch_8mb.csv
```

No source patch is applied to microReticulum. No interface mode, Transport-node, radio framing, announce or partition change is introduced.

## Intentional protocol-facing change

Unlike the local-only M9 parent, M9-build2 intentionally changes:

```text
src/lxmf/LxmfService.cpp
src/lxmf/LxmfService.h
```

The change is narrowly scoped to documented LXMF field behavior:

- parse the fourth LXMF payload item as a field map;
- recognise `FIELD_AUDIO = 0x07`;
- accept `[mode, binary]` audio field values;
- skip unknown fields recursively;
- construct/sign a standard voice-only field message;
- enforce the active Link's actual MDU before send;
- persist encoded audio in a separate sidecar.

Existing text send paths, Link establishment callbacks, destination/signature semantics, announce behavior and opportunistic text behavior are intentionally retained.

## Journal compatibility

Static release checks require the definitions of:

```text
PersistedLxmfRecordV1
PersistedLxmfRecordV2
PersistedLxmfRecordV3
```

to remain unchanged.

Audio payloads are metadata overlays, not a journal migration.

## Added/changed implementation surface

```text
platformio.ini
src/audio/AudioService.cpp
src/audio/AudioService.h
src/audio/VoiceService.cpp
src/audio/VoiceService.h
src/config/BuildConfig.h
src/lxmf/LxmfService.cpp
src/lxmf/LxmfService.h
src/main.cpp
src/storage/StorageService.cpp
src/storage/StorageService.h
src/ui/WatchUI.cpp
src/ui/WatchUI.h
scripts/release_preflight.py
README.md
THIRD_PARTY.md
docs/M9-BUILD2-NOTES.md
docs/M9-BUILD2-SOURCE-AUDIT.md
docs/MISTAKE-LEDGER.md
docs/MILESTONES.md
docs/MASTER-BUILD-PLAN.md
```

## Codec dependency

M9-build2 pins:

```ini
sh123/esp32_codec2 @ 1.0.7
```

The code uses the library's standard C API (`codec2_create`, frame geometry queries, encode/decode) and selects `CODEC2_MODE_450` internally while putting LXMF's separate audio-mode identifier `0x02` on the wire.

## Release audit gates

Preflight checks include:

- frozen Reticulum/radio/config/partition hashes;
- no dependency source patch;
- exact Codec2 dependency pin;
- dedicated codec worker;
- codec worker cannot directly call LXMF/Reticulum send;
- standard LXMF field/mode constants;
- active-Link-only voice send;
- final Link MDU check;
- 240-byte encoded voice cap;
- no Resource-transfer path introduced;
- CRC-protected audio sidecar;
- audio sidecar retention pruning;
- unknown LXMF field skipping;
- unchanged LMX1/LMX2/LMX3 structs;
- optional voice startup cannot gate the text communicator;
- source delimiter/brace/NUL sanity;
- existing M7/M8/M9 regression guards.

## Verification status

Static preflight and partition checks are run before packaging and again from a fresh extraction of the final ZIP. A source manifest is generated for the released tree.

PlatformIO is not installed in the assistant execution environment. Therefore this audit does **not** claim a successful firmware compile. The user's real PlatformIO build is the compile gate, and physical watch/peer testing is the hardware/interoperability gate.
