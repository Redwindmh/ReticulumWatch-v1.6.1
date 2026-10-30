# M12-build3 — Sleep-State-Aware Lift Wake

## Why this build exists

M12-build2 still failed lift-to-wake on hardware. The key architectural mistake was treating wake detection as periodic application polling while the screen was off. A wake source must instead be driven by hardware and remain meaningful across power-state transitions.

The T-Watch S3 already initialises the BMA423 feature engine with TILT and WAKEUP features and maps them to the dedicated `SENSOR_INT` line on GPIO14. M12-build3 uses that path directly.

## Runtime model

- ReticulumWatch screen-off mode remains **CONNECTED-IDLE**, not ESP light sleep.
- ESP32, SX1262 and Reticulum/LXMF continue running.
- Display backlight is off.
- Lift wake does **not** poll XYZ acceleration.
- BMA423 evaluates its motion feature internally and asserts GPIO14.
- A GPIO ISR only latches the edge; I2C/status reads occur in normal task context.
- `INT_TILT` or `INT_WAKEUP` while lift wake is armed wakes the display.

This structure intentionally maps onto a future true light-sleep implementation: GPIO14 can be configured as an ESP external wake source without rewriting lift detection.

## Diagnostics

Expected standby lines:

```text
[m12:motion] standby IRQ arm feature=OK tilt_irq=OK wake_irq=OK gpio=14 level=0 stale=0x.... mode=CONNECTED-IDLE
[m12:power] standby mode=CONNECTED-IDLE cpu=running poll_motion=no sensor_irq=armed
```

While armed, diagnostic-only 1 Hz XYZ telemetry proves whether the CPU and BMA423 remain live. It is not used for wake decisions.

Expected lift event:

```text
[m12:motion] irq_edges=... gpio=14 ... status=0x0008 tilt=YES wakeup=no armed=yes
[m12:motion] hardware lift event latched status=0x0008
[m12:power] wake source=lift
```

`0x0008` is the BMA423 TILT bit. `0x0020` is the BMA423 WAKEUP bit. Other BMA423 feature interrupts are logged at a bounded rate but do not wake the display.

## Preserved behavior

- crown short-press path from M12-build2 retained;
- touch still does not wake a sleeping display;
- incoming messages still wake the display;
- Reticulum/radio/partition boundary unchanged;
- lift-to-wake remains user-configurable and defaults off.

## Acceptance

Do not advance to M13 until actual hardware shows GPIO14 feature interrupts while the display is off and at least one normal wrist lift produces a display wake without breaking Reticulum RX.
