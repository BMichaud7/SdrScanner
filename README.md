# SdrScanner

Qt6 C++ live band scanner. Continuously sweeps a user-specified frequency
range, collects IQ data via the SdrResourceManager AMQP API, runs
Welch-averaged FFT, detects signals, and displays them in a live table
with age-based colour coding.

## Build

```bash
podman build -t sdr-scanner:1.0 .
```

Requires the SdrResourceManager controller and an Artemis broker running.
Use `SdrScripts/sdr.sh start broker controller` to bring those up.

## Run

```bash
# X11
podman run --rm --network=host \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:z \
  sdr-scanner:1.0

# Wayland
podman run --rm --network=host \
  -e WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
  -e XDG_RUNTIME_DIR=/run/user/1000 \
  -v $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY:/run/user/1000/$WAYLAND_DISPLAY:z \
  sdr-scanner:1.0
```

Or via `SdrScripts/sdr.sh scanner`.

## UI

| Field | Default | Description |
|---|---|---|
| Broker | `amqp://localhost:5672` | AMQP broker URL |
| Start / End | 80 / 200 MHz | Scan range |
| BW/step | 20 MHz | IQ bandwidth and step size per position |
| Dwell | 2000 ms | How long to collect IQ at each position |
| Threshold | 10 dB | Minimum signal height above noise floor |

Settings are saved between runs (QSettings).

## Signal table

| Column | Description |
|---|---|
| Freq (MHz) | Signal centre frequency |
| BW (kHz) | Measured −threshold bandwidth |
| +dBc | Power above noise floor |
| Type | Heuristic classification (WFM, AM, NFM, etc.) |
| Last Seen | Time since last detection |
| Hits | Number of scan passes it appeared in |

Rows turn bright green when freshly detected, fade to grey after 60s,
and are removed after 5 minutes without a detection.

## Architecture

```
ScanWorker (QThread)
  └─ AmqpSession (proton thread)
       sends WIDEBAND task → controller → UDP IQ → Welch FFT → Spectrum::analyse()
       emits signalsFound() → MainWindow (Qt main thread)
```
