# Dade Firmware — Initial Hardware Smoke Test

**Test date:** 2026-11-18 (Phoenix local time, MST)  
**Device:** Oracle / `satellite1-d8b450`  
**Device address:** `192.168.0.74`  
**Firmware reported by device:** ESPHome `2026.11.0`  
**Build timestamp reported by device:** `2026-11-18 19:20:51 -0700`  
**Test log:** User-supplied ESPHome API log, `satellite1-d8b450-logs(2).txt`  
**Branch under test:** `Dade` (identified by the operator as the source of this firmware)

## Result

**PASS — initial boot, network/API connectivity, Music Assistant transport connection, and basic sensor/radar reporting observed.**

The device was reachable at its configured address. ESPHome resolved the node, connected, and completed the API handshake. The device then reported a successful Sendspin Noise handshake and an active transport connection to Music Assistant.

## Observed checks

| Check | Observed result | Status |
|---|---|---|
| ESPHome API discovery | Node resolved at `192.168.0.74` | PASS |
| ESPHome API connection | Connected successfully | PASS |
| API handshake | Successful | PASS |
| Firmware startup | ESPHome `2026.11.0`; ESP32-S3 rev 0.2, dual core | PASS |
| Music Assistant / Sendspin | Noise handshake completed; transport active; server connection admitted | PASS |
| Music Assistant group state | Group update received; group `Oracle`, state `stopped` | PASS |
| Ambient light sensor | Reported `558.0 lx` | PASS |
| USB-C power text sensor | Reported `1.5A (max) @ 20V` | PASS |
| Radar target counts | Still/moving counts reported and changed during the capture | PASS |
| Radar target direction | Reported `Moving Away`, then `Approaching` | OBSERVED |

## Scope and limitations

This is an **initial smoke test**, not a complete firmware qualification.

The captured log contains 24 lines spanning approximately 4 seconds. It demonstrates successful startup and the specific communications/sensor reports listed above. It does **not** establish:

- audible speaker output or sustained audio playback;
- wake-word detection or microphone performance;
- long-duration stability or behavior after repeated reboots;
- correctness of USB-C negotiation beyond the reported sensor text;
- radar accuracy against a controlled physical test;
- operation of every Satellite1 peripheral or every code path changed on the Dade branch.

The log reports project version `dev` and does not include a Git commit SHA or firmware image checksum. The branch attribution is therefore recorded as the operator's identification of the firmware source, not as independently verified binary provenance.

## Follow-up validation

1. Confirm audible playback through Music Assistant.
2. Exercise wake-word detection and microphone capture.
3. Observe the device during an extended run and after a reboot.
4. Verify USB-C power reporting against the actual power source.
5. Test radar behavior with controlled stationary and moving targets.

## Evidence

The test evidence is the user-provided ESPHome log. Key entries include:

- `19:26:47.840` — Sendspin Noise handshake completed.
- `19:26:47.841` — Noise transport active and Music Assistant server connected.
- `19:26:47.845` — ESPHome `2026.11.0` build and ESP32-S3 identification reported.
- `19:26:50.116` — USB-C power supply text sensor reported `1.5A (max) @ 20V`.
- `19:26:50.117`–`19:26:51.777` — radar target counts and direction updates reported.
