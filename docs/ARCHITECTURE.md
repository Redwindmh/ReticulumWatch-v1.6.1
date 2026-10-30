# Reticulum Watch Architecture

Reticulum Watch is a standalone communications firmware. It is not part of Game Watch.

## Design boundaries

- `hal/` owns T-Watch hardware access.
- `power/` owns system power policy and sleep states.
- `radio/` owns SX1262 configuration and packet I/O.
- `reticulum/` owns microReticulum, identities, announces, paths, and later LXMF integration.
- `storage/` owns persistent configuration, identities, contacts, paths, and message history.
- `transport/` owns host-controlled modes such as BLE/USB RNode.
- `ui/` owns LVGL screens and input; it must not become the networking engine.

The UI may sleep independently. Reticulum must remain capable of running when the screen is off in connected modes.

## Planned runtime model

The exact task/core placement will be profiled before it is fixed. The intended logical separation is:

1. Network services: radio events, Reticulum/LXMF, path/announce processing.
2. UI services: LVGL, touch, notifications, keyboard.
3. Power policy: coordinates display, ESP32 sleep level, radio reachability, Wi-Fi/BLE state.

## Power states

- **ACTIVE** — UI awake, network fully available.
- **CONNECTED STANDBY** — display off, SX1262 remains reachable and wakes processing through DIO1 where feasible.
- **DUTY-CYCLED** — periodic RX policy for additional battery savings; protocol behavior must be measured before tuning.
- **DEEP SLEEP** — Reticulum offline; manual/timer wake.

M0.1 intentionally does not enter CPU deep/light sleep. Radio interrupt behavior must be proven before power savings are allowed to compromise reachability.
