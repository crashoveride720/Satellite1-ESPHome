# Satellite1 diagnostics

The shared Dade base configuration exposes passive diagnostic entities in
Home Assistant. They update at a modest interval and do not change audio
routing, microphone policy, or device operation.

## System health

- **Device Info**, **Firmware Version**, **Reset Reason**, and **Uptime** identify
  the running build and help explain restarts.
- **Heap Free**, **Heap Max Block**, and **Free PSRAM** show available memory;
  the largest free block can reveal fragmentation that total free memory hides.
- **Loop Time** and **Internal ESP Temperature** give basic processor health
  context.
- **Wi-Fi Signal Strength**, **Wi-Fi Connected**, and **Home Assistant API
  Connected** help separate network issues from device issues.

## Voice and audio

- **Wake Word Engine Ready/Running**, **Wake Word Detections Since Boot**, and
  **Last Wake Word** show whether local detection is loaded and active and
  whether it has detected a model since boot.
- **Micro Wake Word Ring Buffer** reports the configured buffer duration.
- **Voice Assistant Phase/Running**, **Voice Assistant Errors Since Boot**, and
  **Last Voice Assistant Error** track assistant activity and failures.
- **XMOS Audio Availability**, **Microphone Mute Diagnostics**, **Speaker Output
  Phase/Ready**, and **Active Audio Outputs** show the main audio-path gates and
  selected physical outputs.
- Existing media-player entities report Sendspin and local playback state.

The micro-wake-word ring-buffer overflow warning remains available in ESPHome
logs. ESPHome 2026.11.0 does not expose that warning as a YAML trigger, so this
configuration reports the buffer size and detection activity but does not
provide an overflow counter.

## Board and peripherals

- **USB-C Power Supply**, **XMOS Firmware**, and **Line-Out Connected** report
  power-delivery, HAT firmware, and jack state.
- Radar firmware, target, energy, and distance diagnostics are exposed by the
  radar component when supported by the attached module.

Wake-word and Voice Assistant error counts are since boot and reset after a
restart. XMOS reset, flash, and erase controls remain opt-in in
`config/common/debug.yaml` because they initiate maintenance operations.
