# hydrasdr_433

hydrasdr_433 is a wideband ISM data receiver based on [rtl_433](https://github.com/merbanan/rtl_433/), optimized for [HydraSDR](https://hydrasdr.com/) hardware.

It decodes signals on the 433.92 MHz, 868 MHz (SRD), 315 MHz, 345 MHz, and 915 MHz ISM bands, with support for **290+ protocols** out of the box.

This is a dedicated HydraSDR fork with native CF32 support, wideband scanning, a polyphase filter bank channelizer, and hardware-accelerated signal processing. For RTL-SDR/SoapySDR support, see the upstream [rtl_433](https://github.com/merbanan/rtl_433/).

## HydraSDR-Specific Features

### Wideband Scanning Mode

Monitor an entire ISM band simultaneously with a single wideband capture.
The `-B` option splits the RF spectrum into parallel narrowband channels, each decoded independently:

```bash
hydrasdr_433 -B 433.92M:2M:8 # EU 433 ISM band, 8 channels over 2 MHz
hydrasdr_433 -B 915M:8M:16   # US 915 ISM band, 16 channels over 8 MHz
```

Use `-B` when the ISM band is wider than single-frequency capture:

| Band | ISM Width | Single-freq | Recommendation |
|------|-----------|-------------|----------------|
| 433 MHz | 1.74 MHz | 250 kHz | `-B` wideband needed |
| 868 MHz | 600 kHz | 1 MHz (auto) | `-f 868.5M` covers full band |
| 915 MHz | 26 MHz | 1 MHz (auto) | `-B` wideband needed |

Frequencies above 800 MHz automatically use 1 MSps (instead of the 250k default) for wider FSK coverage.

Wideband mode decodes all channels in parallel, with cross-channel deduplication suppressing duplicates from overlapping channels.

### Polyphase Filter Bank Channelizer (OS-PFB)

A 2x oversampled analysis polyphase filter bank splits the wideband input into M narrowband channels (M = 2, 4, 8, or 16):

- **48 taps per branch**, Kaiser window with 80 dB stopband attenuation
- **>41 dB adjacent channel rejection**, >49 dB non-adjacent
- **<0.1 dB passband ripple** within 90% of channel spacing
- 2x oversampled output (channel rate = input rate / (M/2)) preserves signals at channel edges

### Runtime CPU ISA Dispatch

The channelizer hot-path automatically selects the best SIMD instruction set at startup:

| Platform | ISA Levels |
|----------|------------|
| x86-64 | SSE2 (baseline), AVX2+FMA, AVX-512 |
| AArch64 | NEON (baseline), SVE (Linux only) |

No recompilation needed -- a single binary adapts to the host CPU.

### Native CF32 Pipeline

HydraSDR delivers samples as complex float32 (CF32) natively. hydrasdr_433 processes CF32 end-to-end without format conversion, providing higher dynamic range than the CU8/CS16 paths used by RTL-SDR.

### Polyphase Resampler

A per-channel polyphase resampler converts from the channelizer output rate to each decoder's expected sample rate:

- 32 taps per branch, Kaiser window, 60 dB design stopband (measured 74-76 dB)
- GCD-based L/M ratio reduction for minimal computation
- Bypass mode when channelizer output matches decoder rate

### Cross-Channel Deduplication

The 2x oversampled PFB intentionally overlaps adjacent channels so signals at the boundary are not lost. A deduplication filter (FNV-1a hash, 500 ms window) suppresses the resulting duplicate decodes while preserving legitimate retransmissions on the same frequency.

### Wideband Observability

- **Per-channel power and noise floor** tracking (exponential moving average)
- **Text-mode spectrum display** (`-M noise` in wideband mode)
- **Wideband IQ recording** to CF32 file for post-analysis

### hydrasdr-lfft

A lightweight FFT library optimized for the small transform sizes (2-32 points) used in the PFB channelizer.
Stockham autosort algorithm, radix-4 with radix-2 cleanup, split real/imaginary (SoA) format for SIMD-friendly memory access.

## Web UI

hydrasdr_433 includes a self-hosted web UI embedded directly in the binary.
No external CDN, no Google Fonts, no internet connection required, the entire UI loads from memory in under 50 ms.

```bash
hydrasdr_433 -F http # start with embedded web UI
```

Open `http://127.0.0.1:8433/` in any browser.
Workspace tabs (Monitor, Devices, Syslog, Protocols, Stats, System) plus 3 overlay panels (Help, Settings, Debug) provide real-time event monitoring, device tracking with activity charts, protocol browser with modulation details (OOK-PCM, FSK-MC, etc.), configurable limits, radio settings, decoder statistics, and system information.

Vanilla ES5 JavaScript, 15 modular source files concatenated and minified at build time into a single IIFE.
All assets minified and gzip-compressed, total embedded size **< 50 KB**.
Zero external dependencies.
Works offline.

## Supported Input Sources

| Source | Library | Notes |
|--------|---------|-------|
| **HydraSDR** | libhydrasdr v1.1.0+ | Native CF32, wideband scanning |
| rtl_tcp | built-in | Remote SDR via network (TCP) |
| Files | built-in | CU8, CS16, CF32 I/Q and AM data formats |

## Building / Installation

hydrasdr_433 is written in portable C (C99) and builds on Linux, macOS, and Windows.

See [BUILDING.md](docs/BUILDING.md) for full instructions.

### Quick Start (MinGW64)

```bash
cmake -B build -G Ninja
ninja -C build
```

### Quick Start (Visual Studio 2022)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The HydraSDR library is fetched automatically from GitHub via CMake FetchContent.
To use a local build instead, pass `-DHYDRASDR_DIR=/path/to/hydrasdr-host`.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HYDRASDR_DIR` | (auto-fetch) | Path to local HydraSDR library (overrides auto-fetch) |
| `HYDRASDR_FETCH_FROM_GIT` | ON | Fetch HydraSDR from GitHub if not found locally |
| `ENABLE_OPENSSL` | ON | Enable TLS (MQTT, InfluxDB) |
| `ENABLE_NATIVE_OPTIMIZATIONS` | OFF | Use `-march=native` (non-portable) |

## Running

```
hydrasdr_433 -h
```

### Examples

| Command | Description |
|---------|-------------|
| `hydrasdr_433` | Default: listen at 433.92 MHz, 250k sample rate |
| `hydrasdr_433 -B 433.92M:2M:8` | Wideband: scan 2 MHz around 433.92 MHz with 8 channels |
| `hydrasdr_433 -f 868.5M` | EU 868 SRD band (auto 1 MSps covers full 600 kHz band) |
| `hydrasdr_433 -B 915M:8M:16` | Wideband: scan 8 MHz around 915 MHz with 16 channels |
| `hydrasdr_433 -C si` | Convert units to metric |
| `hydrasdr_433 -R 1 -R 8 -R 43` | Enable only specific protocol decoders |
| `hydrasdr_433 -A` | Pulse analyzer mode |
| `hydrasdr_433 -S all -T 120` | Save all signals, run for 2 minutes |
| `hydrasdr_433 -M level -M noise` | Report RSSI/SNR and noise levels |
| `hydrasdr_433 -F http` | Web UI at http://localhost:8433/ |
| `hydrasdr_433 -F mqtt://localhost:1883` | MQTT output |
| `hydrasdr_433 -F json -M utc \| mosquitto_pub -t home/hydrasdr -l` | JSON via MQTT |

### Configuration File

A `hydrasdr_433.conf` file is searched in:
1. Current directory (`./hydrasdr_433.conf`)
2. XDG config (`$HOME/.config/hydrasdr_433/`)
3. System config (`/usr/local/etc/hydrasdr_433/`)

See [hydrasdr_433.example.conf](conf/hydrasdr_433.example.conf) for all options.

## Protocol Compatibility

hydrasdr_433 supports all 290+ protocols from rtl_433, verified with a 1828-test reference suite:

- **99.9% pass rate** (1809 exact match, 18 extra decodes, 1 field-level mismatch)
- 96 protocols actively tested across all ISM bands

See [PROTOCOLS.md](docs/PROTOCOLS.md) for the full protocol reference, or run `hydrasdr_433 -R help`.

## Performance

Channelizer throughput on AMD Ryzen 9 7950X3D (Zen 4, AVX-512), GCC 15.2:

| Channels | Throughput | Real-Time Margin (2.5 MSps input) |
|----------|------------|-----------------------------------|
| 4-ch | 73 MSps | 29x |
| 8-ch | 82 MSps | 33x |
| 16-ch | 85 MSps | 17x (5 MSps input) |

## How to Add Support for Unsupported Sensors

See [CONTRIBUTING.md](./docs/CONTRIBUTING.md).

## Security

hydrasdr_433 should not be assumed secure. RF data is literally pulled from thin air and should not be trusted. If you feed downstream systems, validate edge cases. Network outputs (MQTT, HTTP, InfluxDB) are designed for trusted local networks and may contain unfiltered data.

## Upstream

hydrasdr_433 is based on [rtl_433](https://github.com/merbanan/rtl_433/) by Benjamin Larsson and contributors.
For the upstream project, documentation, and community see https://triq.org/.

## License

GPLv2, same as rtl_433.
The embedded hydrasdr-lfft library is MIT licensed.
