# Dade Firmware — Oracle Hardware Validation

**Test date:** 2026-11-18 (Phoenix local time, MST)  
**Device:** Oracle / `satellite1-d8b450`  
**Device address:** `192.168.0.74`  
**Firmware reported by device:** ESPHome `2026.11.0`  
**Build timestamp reported by device:** `2026-11-18 19:20:51 -0700`  
**Branch under test:** `Dade` (operator-confirmed firmware source)  
**Test evidence:** ESPHome API log supplied by the operator, plus operator confirmation of functional testing

## Overall result

**PASS — operator reports that the Dade firmware is working across the device's tested functionality.**

The operator confirmed that audio playback, wake-word operation, the web interface, and the rest of the tested Satellite1 functionality are working. This is broader validation than the initial startup log alone demonstrates.

## Functional validation

| Subsystem / feature | Result | Evidence |
|---|---|---|
| Firmware boot and initialization | PASS | ESPHome log |
| ESPHome API discovery, connection, and handshake | PASS | ESPHome log |
| Music Assistant / Sendspin connection | PASS | ESPHome log; operator reports audio playback works |
| Audio playback | PASS | Operator-confirmed functional test |
| Wake-word detection | PASS | Operator-confirmed functional test |
| Web interface | PASS | Operator-confirmed functional test |
| Ambient light sensor | PASS | ESPHome log: 558.0 lx |
| USB-C power reporting | PASS | ESPHome log: 1.5A (max) @ 20V |
| Radar target reporting | PASS | ESPHome log: still/moving counts and direction updates |
| Other tested Satellite1 functionality | PASS | Operator reports all tested functionality is working |

## Boot and connectivity evidence

The supplied log records:

- `19:26:47.840` — Sendspin Noise handshake completed.
- `19:26:47.841` — Noise transport active and Music Assistant server connected.
- `19:26:47.845` — ESPHome `2026.11.0` build and ESP32-S3 rev 0.2 (dual core) reported.
- `19:26:50.116` — USB-C power supply text sensor reported `1.5A (max) @ 20V`.
- `19:26:50.117`–`19:26:51.777` — Radar target counts and direction updates reported.

The device resolved and connected at `192.168.0.74`, completed the ESPHome API handshake, and connected to Music Assistant using the Sendspin Noise transport.

## Test scope and provenance

This report combines two evidence types:

1. **Direct log evidence:** The supplied 24-line ESPHome log captures approximately four seconds of successful connection and sensor activity.
2. **Operator functional confirmation:** The operator reports that audio playback, wake-word operation, the web interface, and all other tested functionality are working.

The log reports project version `dev` and does not include a Git commit SHA or firmware image checksum. The association with branch `Dade` is based on the operator's confirmation of which firmware was flashed; binary provenance was not independently verified from the log.

This report records a successful functional test on the device. It is not a claim of exhaustive qualification of every possible operating condition or long-duration reliability.

## Result summary

**Dade firmware: FUNCTIONAL TEST PASSED on Oracle.**

No failures were reported by the operator during the tested functionality.