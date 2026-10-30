# M9-build3 Source Audit

## Parent artifact

`ReticulumWatch-M9-build2.zip`

M9-build2 remains immutable. M9-build3 is a compile-correction build created from that exact source tree.

## Observed compiler failure

The only `error:` in the supplied PlatformIO log was in `src/lxmf/LxmfService.cpp`:

```text
error: 'class arduino::msgpack::Unpacker' has no member named 'isFloat'; did you mean 'isFloat32'?
```

No linker error was reached. The repeated ArduinoJson deprecation, DebugLog default-level, and microReticulum neighbor-probe overflow messages are warnings and remain outside this narrow correction.

## Intentional source changes from M9-build2

Functional source change:

```text
src/lxmf/LxmfService.cpp
```

The generic MessagePack skip helper replaces one unsupported predicate with width-specific float handling.

Release/version/documentation changes:

```text
src/config/BuildConfig.h
src/ui/WatchUI.cpp
src/lxmf/LxmfService.h
src/storage/StorageService.h
src/audio/VoiceService.h
platformio.ini
scripts/release_preflight.py
README.md
THIRD_PARTY.md
docs/M9-BUILD3-NOTES.md
docs/M9-BUILD3-SOURCE-AUDIT.md
docs/MISTAKE-LEDGER.md
docs/MILESTONES.md
docs/MASTER-BUILD-PLAN.md
```

## Frozen endpoint boundary

The release preflight still verifies byte-identical SHA-256 values for the accepted Reticulum service, LoRa interface, radio service, RNS/radio configuration and both partition maps. M9-build3 does not patch microReticulum or alter the frozen endpoint boundary.

## Voice/LXMF scope

M9-build3 retains M9-build2's standard LXMF `FIELD_AUDIO = 0x07`, Codec2-450 mode `0x02`, 240-byte encoded cap, runtime active-Link MDU check, CRC-protected audio sidecars and active-Link-only first transport scope. LMX1/LMX2/LMX3 layouts remain unchanged.

## New audit gate

Static preflight now requires:

- no `unpacker.isFloat()` call in `LxmfService.cpp`;
- `unpacker.isFloat32()` present;
- `unpacker.isFloat64()` present.

This directly guards the API mistake that caused M9-build2 to fail compilation.
