# M9-build3 Notes — MsgPack API Compile Correction

## Why this build exists

M9-build2 is immutable and failed its first real PlatformIO compile. The compiler reached the new LXMF unknown-field skip helper and reported:

```text
src/lxmf/LxmfService.cpp:33:18: error: 'class arduino::msgpack::Unpacker' has no member named 'isFloat'; did you mean 'isFloat32'?
```

The surrounding ArduinoJson, microReticulum and DebugLog messages in the same build are warnings inherited from existing pinned dependencies; they were not the build-stopping error.

## Root cause

The M9-build2 release audit checked the MessagePack parsing structure but used a generic `Unpacker::isFloat()` predicate that is not present in the resolved `hideakitai/MsgPack @ 0.4.2` API used by this firmware. The pinned API exposes width-specific float predicates.

## Correction

`skipMsgPackValue()` now handles MessagePack floating-point values as:

```cpp
if (unpacker.isFloat32()) {
    (void)unpacker.unpackFloat<float>();
    return true;
}
if (unpacker.isFloat64()) {
    (void)unpacker.unpackFloat<double>();
    return true;
}
```

This is a compile/API compatibility correction only. It does not alter standard LXMF voice field IDs, Codec2 mode, message signing, active-Link MDU policy, persistence, radio framing, Reticulum startup, partitions, or UI flow.

## New regression gate

`release_preflight.py` now explicitly rejects the nonexistent generic `unpacker.isFloat()` call and requires both `isFloat32()` and `isFloat64()` in the generic skip helper.

## Verification status

The assistant environment can run static/preflight, partition, manifest and archive checks, but does not contain PlatformIO. Therefore M9-build3 is not compile-verified until the user's real `pio run -e twatch_s3_16mb` succeeds. Hardware/interoperability acceptance remains a separate later gate.
