# Building hydrasdr_433

hydrasdr_433 is a fork of rtl_433 with native HydraSDR support and wideband scanning capabilities.

## Supported Input Types

* [HydraSDR](https://hydrasdr.com/) (recommended for wideband scanning)
* [RTL-SDR](http://sdr.osmocom.org/trac/wiki/rtl-sdr) (optional)
* [SoapySDR](https://github.com/pothosware/SoapySDR/wiki) (optional)
* Files: CU8, CS16, CF32 I/Q data, U16 AM data (built-in)
* rtl_tcp remote data servers (built-in)

## Prerequisites

* CMake 3.16 or later
* C99 compiler (GCC, Clang, or MSVC)
* HydraSDR library v1.1.0 (local build required)
* Optional: librtlsdr, SoapySDR libraries

## HydraSDR Library

HydraSDR v1.1.0 must be built locally before building hydrasdr_433. The library path is specified with `-DHYDRASDR_DIR`.

Example path structure (using `hydrasdr-host`):
```
C:\hydrasdr-host
├── build_mingw64/          # MinGW64 build
│   └── libhydrasdr/src/
│       └── libhydrasdr.dll.a
└── build_VS2022/           # Visual Studio 2022 build
    └── libhydrasdr/src/Release/
        └── hydrasdr.lib
```

## Windows Build

Two build configurations are supported on Windows:

### MinGW64 Build (build_mingw64)

Uses GCC compiler via MSYS2/MinGW64. Recommended for development.

```bash
# Open MINGW64 terminal from MSYS2

# Install dependencies
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja mingw-w64-x86_64-openssl mingw-w64-x86_64-libusb

# Clone and build
git clone https://github.com/hydrasdr/hydrasdr_433.git
cd hydrasdr_433
cmake -B build_mingw64 -G Ninja \
    -DHYDRASDR_DIR="C:/hydrasdr-host"
ninja -C build_mingw64

# Run tests
./build_mingw64/external/hydrasdr-lfft/lfft_test.exe
./build_mingw64/tests/channelizer-bench.exe
```

**Output:**
```
build_mingw64/src/
├── hydrasdr_433.exe
├── libhydrasdr.dll      # Copied automatically
├── libusb-1.0.dll       # Copied automatically
├── libcrypto-3-x64.dll  # Copied automatically (OpenSSL)
└── libssl-3-x64.dll     # Copied automatically (OpenSSL)
```

### Visual Studio 2022 Build (build_vs2022)

Uses MSVC compiler. Best performance, recommended for release builds.

```cmd
:: Use cmd.exe (not MSYS2 terminal) to avoid header conflicts

git clone https://github.com/hydrasdr/hydrasdr_433.git
cd hydrasdr_433
cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 ^
    -DENABLE_OPENSSL=OFF ^
    -DHYDRASDR_DIR=C:/hydrasdr-host
cmake --build build_vs2022 --config Release

:: Run tests
build_vs2022\tests\Release\channelizer-bench.exe
build_vs2022\external\hydrasdr-lfft\Release\lfft_test.exe
```

**Output:**
```
build_vs2022/src/Release/
├── hydrasdr_433.exe
├── hydrasdr.dll         # Copied automatically
└── libusb-1.0.dll       # Copied automatically
```

### Build from MSYS2 Terminal (Alternative)

If using MSYS2 terminal for VS2022 builds, use explicit cmake path to avoid MinGW header conflicts:

```bash
# Clear MinGW from PATH for VS2022
export PATH="/d/msys64/usr/bin:$PATH"

/d/msys64/mingw64/bin/cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 \
    -DENABLE_OPENSSL=OFF \
    -DHYDRASDR_DIR="C:/hydrasdr-host"

/d/msys64/mingw64/bin/cmake --build build_vs2022 --config Release
```

## Linux / macOS

```bash
git clone https://github.com/hydrasdr/hydrasdr_433.git
cd hydrasdr_433
cmake -B build -GNinja -DHYDRASDR_DIR=/path/to/hydrasdr-host
cmake --build build
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HYDRASDR_DIR` | - | **Required.** Path to HydraSDR library source/build |
| `ENABLE_RTLSDR` | OFF | Enable RTL-SDR support |
| `ENABLE_SOAPYSDR` | OFF | Enable SoapySDR support |
| `ENABLE_OPENSSL` | ON | Enable TLS support (MQTT, InfluxDB) |
| `BUILD_TESTING` | ON | Build test suite |

## Static Runtime Configuration

Both MinGW64 and VS2022 builds use static C runtime by default:

- **MinGW64**: Uses `-static-libgcc` to avoid libgcc DLL dependency
- **VS2022**: Uses `MultiThreaded` runtime (`/MT`) instead of `MultiThreadedDLL` (`/MD`)

This produces standalone executables that only require the HydraSDR-specific DLLs:
- `libhydrasdr.dll` / `hydrasdr.dll` - HydraSDR library
- `libusb-1.0.dll` - USB library for device access
- OpenSSL DLLs (MinGW64 only, if TLS enabled)

## Running Tests

### MinGW64 (build_mingw64)

```bash
cd build_mingw64

# Run all tests
ctest

# Run specific tests
./external/hydrasdr-lfft/lfft_test      # FFT library tests (45 tests)
./tests/channelizer-bench               # Channelizer tests (48 tests)
./tests/resampler-test                  # Resampler tests
```

### Visual Studio 2022 (build_vs2022)

```cmd
cd build_vs2022

:: Run all tests
ctest -C Release

:: Run specific tests
external\hydrasdr-lfft\Release\lfft_test.exe
tests\Release\channelizer-bench.exe
tests\Release\resampler-test.exe
```

### Test Coverage

* **lfft_test** (45 tests): FFT correctness for sizes 2-32
  - DC input, impulse response, single tones
  - Reference DFT comparison, roundtrip
  - Parseval's theorem, linearity, time shift
  - Real input symmetry
  - Benchmarks: 150-286 MSps depending on compiler

* **channelizer-bench** (48 tests): PFB channelizer
  - Initialization, frequency mapping
  - DC/channel routing, isolation
  - Multi-tone separation, decimation
  - Continuous processing, energy conservation
  - Benchmarks: 12-37x real-time margin

### Performance Comparison

| Test | MinGW64 (GCC 15) | VS2022 (MSVC 19) |
|------|------------------|------------------|
| FFT N=8 | 174 MSps | 222 MSps |
| FFT N=32 | 123 MSps | 150 MSps |
| Channelizer 4-ch | 31x RT | 34x RT |
| Channelizer 16-ch | 10x RT | 12x RT |

## Platform-Specific Dependencies

### Debian/Ubuntu

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libtool libusb-1.0-0-dev librtlsdr-dev rtl-sdr libssl-dev
```

### Fedora/RHEL

```bash
sudo dnf install cmake ninja-build gcc rtl-sdr-devel libusb1-devel openssl-devel
```

### macOS (Homebrew)

```bash
brew install cmake ninja rtl-sdr pkg-config openssl libusb
```

### Windows (MSYS2)

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja mingw-w64-x86_64-openssl mingw-w64-x86_64-libusb
```

## Troubleshooting

### HydraSDR Not Found

Ensure `HYDRASDR_DIR` points to the hydrasdr-host directory containing:
- `libhydrasdr/src/hydrasdr.h` (header)
- `build_mingw64/libhydrasdr/src/libhydrasdr.dll.a` (MinGW)
- `build_VS2022/libhydrasdr/src/Release/hydrasdr.lib` (VS2022)

Example: `C:/hydrasdr-host`

### VS2022 Build Links Against MinGW Library

The FindHydraSDR module automatically selects the correct library based on compiler. If issues persist, clear CMakeCache.txt and reconfigure:

```cmd
del build_vs2022\CMakeCache.txt
cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 -DENABLE_OPENSSL=OFF ^
    -DHYDRASDR_DIR=C:/hydrasdr-host
```

### Missing DLLs at Runtime

Both MinGW64 and VS2022 builds copy DLLs automatically. If DLLs are missing, reconfigure with a clean CMakeCache.txt:

```bash
# MinGW64
rm build_mingw64/CMakeCache.txt
cmake -B build_mingw64 -G Ninja \
    -DHYDRASDR_DIR="C:/hydrasdr-host"

# VS2022
del build_vs2022\CMakeCache.txt
cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 \
    -DENABLE_OPENSSL=OFF \
    -DHYDRASDR_DIR=C:/hydrasdr-host
```

### SoapySDR Version Mismatch

If you experience trouble with SoapySDR, you may have mixed 0.7 and 0.8 headers/libs.
Purge all SoapySDR packages and install only from packages (0.7) or source (0.8).

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines on submitting changes.
