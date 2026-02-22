# Binary Builds

hydrasdr_433 is built from source. See [BUILDING.md](BUILDING.md) for full build instructions.

## Prerequisites

- CMake 3.16+ (CMake 4.0+ for VS2026)
- C99 compiler (GCC, Clang, or MSVC)
- HydraSDR library v1.1.0+
- libusb
- OpenSSL (optional, for MQTT/InfluxDB TLS)

## Quick Build

### Windows (MinGW64)

```bash
cmake -B build -G Ninja -DHYDRASDR_DIR="C:/hydrasdr-host"
ninja -C build
```

### Windows (Visual Studio 2022)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 -DHYDRASDR_DIR=C:/hydrasdr-host
cmake --build build --config Release
```

### Linux / macOS

```bash
cmake -B build -GNinja -DHYDRASDR_DIR=/path/to/hydrasdr-host
cmake --build build
```

## Upstream rtl_433 binaries

For pre-built rtl_433 binaries with RTL-SDR and SoapySDR support, see the upstream
[rtl_433 releases](https://github.com/merbanan/rtl_433/releases).
