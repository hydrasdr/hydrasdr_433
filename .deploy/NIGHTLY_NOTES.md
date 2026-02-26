## hydrasdr_433 — Nightly Build

> Wideband ISM data receiver based on [rtl_433](https://github.com/merbanan/rtl_433/), optimized for [HydraSDR](https://hydrasdr.com/) hardware.

**Version:** `%VERSION%` · **Commit:** [`%SHORT_SHA%`](%REPO_URL%/commit/%SHA%)

Decodes 433.92 MHz, 868 MHz (SRD), 315 MHz, 345 MHz, and 915 MHz ISM bands with **290+ protocols** out of the box.

> **This is an automated nightly build from the `master` branch. It may contain untested changes.**

---

### What's different from rtl_433?

| Feature | rtl_433 | hydrasdr_433 |
|---------|---------|--------------|
| **Wideband scanning** | Single frequency | Scan entire ISM band simultaneously (`-B` flag) |
| **Channelizer** | — | 2x oversampled polyphase filter bank (2/4/8/16 ch) |
| **Sample format** | CU8 / CS16 (CF32 for files) | Native CF32 end-to-end from hardware (higher dynamic range) |
| **SIMD acceleration** | — | Runtime dispatch: SSE2, AVX2+FMA, AVX-512, NEON, SVE |
| **Cross-channel dedup** | — | FNV-1a hash, 500 ms window |
| **Web UI** | Built-in HTTP (`-F http`) | Self-hosted, embedded in binary (< 50 KB, zero deps) |
| **Hardware** | RTL-SDR / SoapySDR | [HydraSDR](https://hydrasdr.com/) RFOne (native) |

---

### Key Features

#### Wideband Scanning (`-B`)

Monitor an entire ISM band with parallel narrowband channels:

```
hydrasdr_433 -B 433.92M:2M:8    # EU 433 ISM — 8 channels over 2 MHz
hydrasdr_433 -B 915M:8M:16      # US 915 ISM — 16 channels over 8 MHz
```

#### Polyphase Filter Bank Channelizer (OS-PFB)

2x oversampled analysis PFB — 48 taps/branch, Kaiser window, 80 dB stopband attenuation, >41 dB adjacent channel rejection, <0.1 dB passband ripple.

#### Embedded Web UI

Self-hosted web interface — no CDN, no Google Fonts, no internet required:

```
hydrasdr_433 -F http    # open http://127.0.0.1:8433/
```

6 tabs (Monitor, Devices, Syslog, Protocols, Stats, System) + 3 overlay panels (Help, Settings, Debug).
Real-time event streaming, device tracking with activity charts, protocol browser, and radio configuration.
Vanilla ES5 JavaScript, < 50 KB total, works offline.

#### Runtime SIMD Dispatch

A single binary auto-selects the best instruction set at startup — SSE2 / AVX2+FMA / AVX-512 on x86-64, NEON / SVE on AArch64.

---

### Quick Start

```
hydrasdr_433                     # 433.92 MHz, 250k sample rate
hydrasdr_433 -f 868.5M           # EU 868 SRD band
hydrasdr_433 -B 433.92M:2M:8     # wideband scan
hydrasdr_433 -F http             # web UI at http://localhost:8433/
hydrasdr_433 -F mqtt://localhost # MQTT output
hydrasdr_433 -M level -M noise   # RSSI/SNR + noise levels
hydrasdr_433 -R help             # list all 290+ protocols
```

---

### Platforms

| Platform | File |
|----------|------|
| Windows x64 | `hydrasdr_433-Windows-x64-*.zip` |
| macOS ARM64 (Apple Silicon) | `hydrasdr_433-macOS-ARM64-*.tar.gz` |
| macOS x86_64 (Intel) | `hydrasdr_433-macOS-x86_64-*.tar.gz` |
| Ubuntu 24.04 / 22.04 | `.deb` packages |
| Debian 13 / 12 / 11 | `.deb` packages |
| Linux Mint 22.1 / 22 / 21.3 | `.deb` packages |
| Fedora 42 / 41 | `.rpm` packages |
| AlmaLinux 9 | `.rpm` package |
| openSUSE Tumbleweed | `.rpm` package |
| Arch Linux | `.pkg.tar.zst` package |
| Linux ARM64 | `hydrasdr_433-Linux-ARM64-*.tar.gz` |

### Install

| Platform | Command |
|----------|---------|
| **Windows** | Extract zip, run `hydrasdr_433.exe` from terminal |
| **macOS** | Extract tar.gz, run `./hydrasdr_433` |
| **Debian/Ubuntu/Mint** | `sudo dpkg -i hydrasdr-433_*.deb && sudo apt-get install -f` |
| **Fedora/AlmaLinux/openSUSE** | `sudo rpm -i hydrasdr-433-*.rpm` |
| **Arch Linux** | `sudo pacman -U hydrasdr-433-*.pkg.tar.zst` |
| **ARM64** | Extract tar.gz, run `./hydrasdr_433` |

### Requirements

- [HydraSDR](https://hydrasdr.com/) hardware + libusb
- Optional: OpenSSL for TLS (MQTT/InfluxDB)

### Documentation

- [README](%REPO_URL%/blob/master/README.md) — project overview and usage
- [Building](%REPO_URL%/blob/master/docs/BUILDING.md) — build instructions for all platforms
- [Protocols](%REPO_URL%/blob/master/docs/PROTOCOLS.md) — full 290+ protocol reference
- [Web UI](%REPO_URL%/blob/master/webui/WEBUI.md) — embedded web interface documentation
- [Operation](%REPO_URL%/blob/master/docs/OPERATION.md) — detailed operation guide
- [Integration](%REPO_URL%/blob/master/docs/INTEGRATION.md) — MQTT, InfluxDB, Home Assistant
