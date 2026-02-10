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

## Important limitations

AirPlay audio decoding/forwarding is not implemented in this first version. The component currently handles discovery/control/session setup and volume translation, but does not decode RAOP audio frames into local playback.

In practice, this works best when paired with a backend that can consume session details (for example through your own relay pipeline) and expose a playable `media_url`, which you can inject with `media_url_template`.

## Directory layout

- `components/airplay_bridge/__init__.py` - ESPHome config schema + codegen.
- `components/airplay_bridge/airplay_bridge.h` - component declarations.
- `components/airplay_bridge/airplay_bridge.cpp` - RTSP server, mDNS advertising, media_player control mapping.
- `examples/basic.yaml` - reference ESPHome config.

## Usage

1. Copy this repo (or just `components/airplay_bridge`) into your project.
2. Add it as a local external component:

```yaml
external_components:
  - source:
      type: local
      path: ./components
    components: [airplay_bridge]
```

3. Configure `airplay_bridge` with at least one `media_player` target.
4. Compile and flash with ESPHome.

## Example config

See `examples/basic.yaml`.

## Notes

- Current implementation targets Arduino framework builds.
- ESP32 has the best mDNS support for multiple service instances.
