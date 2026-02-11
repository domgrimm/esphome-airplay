# ESPHome AirPlay Bridge (AirPlay 1 / RAOP)

This repository contains an ESPHome `external_components` custom component that advertises configured `media_player` entities as AirPlay (RAOP) targets.

## What it does

- Registers one RAOP endpoint per configured target with mDNS.
- Accepts key RTSP commands used by AirPlay 1 clients (`OPTIONS`, `ANNOUNCE`, `SETUP`, `RECORD`, `SET_PARAMETER`, `FLUSH`, `TEARDOWN`).
- Maps AirPlay control events to ESPHome `media_player` calls:
  - `RECORD` -> `PLAY`
  - `FLUSH` / `TEARDOWN` -> `STOP`
  - `SET_PARAMETER volume` -> `set_volume()`
- Optionally sets `media_url` via a template before issuing `PLAY`.
- **Local playback**: When you add a `speaker` reference to a target, the component decodes AirPlay ALAC audio and feeds it directly to the speaker. Requires ESP32 with esp-idf framework and `esp_audio_codec` (see below).

## Local playback setup

For AirPlay audio decoding and local playback:

1. Use **esp-idf framework** (not Arduino)
2. The component includes `idf_component.yml` with the `esp_audio_codec` dependency; the IDF Component Manager fetches it automatically during build. If the build fails to find `esp_audio_codec`, add a project-root `idf_component.yml` with `dependencies: espressif/esp_audio_codec: "^2.0.3"`.
3. Add `speaker` to your target and match `output_sample_rate` to your speaker:

```yaml
airplay_bridge:
  port_base: 7000
  output_sample_rate: 16000
  targets:
    - media_player: {media_player_id}
      speaker: {speaker_id}
      name: "Speaker"
```

## Directory layout

- `components/airplay_bridge/__init__.py` - ESPHome config schema + codegen.
- `components/airplay_bridge/airplay_bridge.h` - component declarations.
- `components/airplay_bridge/airplay_bridge.cpp` - RTSP server, mDNS, media_player control, ALAC decode.
- `examples/basic.yaml` - reference ESPHome config.

## Usage

1. Add repo as an external component:

```yaml
external_components:
  - source: github://domgrimm/esphome-airplay
    components: ["airplay_bridge"]
```

2. Configure `airplay_bridge` with at least one `media_player` target (add `speaker` for local playback):

```yaml
airplay_bridge:
  port_base: 7000
  targets:
    - media_player: {media_player_id}
      speaker: {speaker_id}  # optional, for local playback
      name: "Speaker"
```

3. Compile and flash with ESPHome.

## Example config

See `examples/basic.yaml`.

## Notes

- Current implementation supports:
  - ESP32 Arduino builds (control only, no local audio decode)
  - ESP32 esp-idf builds (full local playback when speaker + esp_audio_codec)
- ESP32 has the best mDNS support for multiple service instances.
- ESP8266 remains Arduino-only.
