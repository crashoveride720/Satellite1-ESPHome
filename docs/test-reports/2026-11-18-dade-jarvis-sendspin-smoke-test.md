# Dade Firmware — Jarvis Sendspin and Hardware Smoke Test

**Test date:** 2026-11-18 (Phoenix local time, MST)  
**Device:** Jarvis / `satellite1-4fd490`  
**Device address:** `192.168.0.73`  
**Firmware reported by device:** ESPHome `2026.11.0`  
**Build timestamp reported by device:** `2026-11-18 19:07:58 -0700`  
**Branch under test:** `Dade` (operator-identified firmware source)  
**Evidence:** Operator-supplied ESPHome log, `satellite1-4fd490-logs.txt`

## Overall result

**PASS — Jarvis booted, connected to ESPHome and Music Assistant, and completed a Sendspin audio-stream session.**

The log records a successful WebSocket upgrade, completed Noise handshake, active encrypted transport, Music Assistant connection admission, a FLAC stream starting, and the media player entering PLAYING and later returning to IDLE after the stream ended.

## Observed checks

| Subsystem / check | Observed result | Status |
|---|---|---|
| ESPHome API discovery | Jarvis resolved at `192.168.0.73` | PASS |
| ESPHome API connection | Connected successfully | PASS |
| API handshake | Successful | PASS |
| Firmware startup | ESPHome `2026.11.0`; ESP32-S3 rev 0.2, dual core | PASS |
| Sendspin WebSocket | Client connection and WebSocket upgrade completed | PASS |
| Sendspin Noise handshake | Completed successfully | PASS |
| Sendspin transport | Noise transport active; connection admitted | PASS |
| Music Assistant | Connected to the configured Music Assistant server | PASS |
| Group state | Jarvis group changed from stopped to playing, then back to stopped | PASS |
| Audio stream | Stream started; FLAC, 48 kHz, 2-channel, 16-bit codec header processed | PASS |
| Media player state | PLAYING at 90% volume, then IDLE at 90% | PASS |
| Sendspin synchronization | Brief sync loss reported, then sync regained | PASS, RECOVERED |
| Boot health | ESPHome safe-mode component reported boot successful and reset boot-loop counter | PASS |
| USB-C power reporting | `1.5A (max) @ 20V` | PASS |
| Ambient light | `12.7 lx` | PASS |
| Wi-Fi signal | `-55 dBm` | PASS |
| Temperature | `19.22 °C` | PASS |
| Humidity | `24.70 %` | PASS |

## Sendspin playback sequence

The supplied log records this sequence:

1. **19:38:09.888–19:38:09.890** — Sendspin WebSocket client connected and upgrade completed.
2. **19:38:09.893–19:38:09.973** — Connection admitted and Noise handshake completed.
3. **19:38:09.991** — Noise transport became active.
4. **19:38:09.996–19:38:10.003** — Music Assistant connection admitted; Jarvis group state reported as stopped.
5. **19:38:51.253** — Jarvis group state changed to playing.
6. **19:38:51.258** — Sendspin stream started.
7. **19:38:51.275** — FLAC codec header processed: 48 kHz, 2 channels, 16-bit.
8. **19:38:51.472** — ESPHome media player reported PLAYING at 90% volume.
9. **19:38:51.514–19:38:51.560** — Sendspin reported a brief synchronization loss followed by successful sync recovery.
10. **19:39:16.269–19:39:16.402** — Stream ended, group returned to stopped, and media player returned to IDLE.

## Warning observed

At **19:38:51.474**, the micro-wake-word component reported:

`Not enough free bytes in ring buffer to store incoming audio data. Resetting the ring buffer. Wake word detection accuracy will temporarily be reduced.`

This is a recorded warning during the audio playback session. The log does not show a crash or reboot associated with it. It indicates that wake-word detection accuracy may be temporarily reduced when the audio stream places pressure on the incoming-audio ring buffer. It should be monitored during further simultaneous playback and wake-word testing.

## Scope and limitations

This report confirms the startup, connection, stream negotiation, playback-state transitions, synchronization recovery, and sensor reporting shown in the supplied log.

The log demonstrates that Sendspin delivered a FLAC audio stream and that the ESPHome media player entered PLAYING. It does not, by itself, prove the physical speaker output was audible; that requires the operator's listening test. Likewise, this log contains a micro-wake-word buffer warning but does not contain a wake-word detection event, so wake-word recognition should be recorded separately when directly tested.

The log reports project version `dev` and does not include a Git commit SHA or firmware image checksum. The association with branch `Dade` is based on the operator's identification of the flashed firmware, not independent binary provenance verification.

## Result summary

**Dade firmware on Jarvis: Sendspin and hardware smoke test PASSED for the behaviors recorded above.**

The only notable issue in this capture is the transient micro-wake-word ring-buffer warning during playback. Sendspin synchronization recovered, the stream ended normally, and the media player returned to IDLE.

## Follow-up diagnosis

The warning means the microphone producer could not fit a complete audio capture chunk in the micro-wake-word ring buffer. ESPHome 2026.11.0 defaults that buffer to 120 ms. With the configured 16 kHz, 16-bit, mono wake-word stream, that is 3,840 bytes. When a complete write does not fit, the producer requests that the inference task clear buffered audio, which temporarily interrupts detection continuity.

This capture records one warning at 19:38:51.474, immediately after the media player entered `PLAYING` at 19:38:51.472. It records no further overflow during the remaining playback. The timing points to a transient scheduling backlog during playback startup: the Dade microphone capture task runs at priority 17, the I2S speaker task at priority 19, and ESPHome's micro-wake-word inference task at priority 3. The report contains no task scheduling trace or buffer occupancy measurement, so the exact stall duration and scheduling event were not directly measured.

Dade now sets `ring_buffer_duration` to 250 ms (8,000 bytes for this stream) in `config/common/voice_assistant.yaml`. This gives inference additional room to absorb a short startup burst while preserving the existing audio routes, sample format, and playback configuration. It does not establish that the warning is eliminated under every load.

**Focused retest:** on Jarvis, run a wake-word detection session while starting a 48 kHz stereo Sendspin stream; record whether the ring-buffer warning recurs during startup and sustained playback, and confirm wake-word detection still works before, during, and after playback. This firmware-only validation requires the device log; the original smoke-test capture did not include a wake-word detection event.
