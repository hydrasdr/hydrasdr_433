# Building hydrasdr_433

hydrasdr_433 is a fork of rtl_433 with native HydraSDR support and wideband scanning capabilities.

## Supported Input Types

* [HydraSDR](https://hydrasdr.com/) (recommended for wideband scanning)
* Files: CU8, CS16, CF32 I/Q data, U16 AM data (built-in)
* rtl_tcp remote data servers (built-in)

## Prerequisites

* CMake 3.16 or later (CMake 4.0+ required for VS2026)
* C99 compiler (GCC, Clang, or MSVC)
* HydraSDR library v1.1.0+ (auto-fetched from GitHub, or local build)

## HydraSDR Library

By default, CMake automatically fetches and builds the HydraSDR library from GitHub via FetchContent. No manual setup is needed.

To use a local build instead, pass `-DHYDRASDR_DIR=/path/to/hydrasdr-host`:

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
cmake -B build_mingw64 -G Ninja
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
    -DENABLE_OPENSSL=OFF
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

### Visual Studio 2026 Build (build_vs2026)

Uses MSVC 19.50 compiler. Requires CMake 4.0+ (the version bundled with MSYS2/MinGW64 or VS2026).

```cmd
:: Use cmd.exe (not MSYS2 terminal) to avoid header conflicts

git clone https://github.com/hydrasdr/hydrasdr_433.git
cd hydrasdr_433
cmake -B build_vs2026 -G "Visual Studio 18 2026" -A x64 ^
    -DENABLE_OPENSSL=OFF
cmake --build build_vs2026 --config Release

:: Run tests
build_vs2026\tests\Release\channelizer-bench.exe
build_vs2026\external\hydrasdr-lfft\Release\lfft_test.exe
```

**Output:**
```
build_vs2026/src/Release/
├── hydrasdr_433.exe
├── hydrasdr.dll         # Copied automatically
└── libusb-1.0.dll       # Copied automatically
```

**Note:** `-DENABLE_OPENSSL=OFF` is required when MSYS2 is in the system PATH. Without it, CMake finds MinGW's OpenSSL headers (`D:\msys64\mingw64\include`) which are incompatible with MSVC and cause compile errors in system headers.

### Build from MSYS2 Terminal (Alternative)

If using MSYS2 terminal for VS builds, disable OpenSSL to avoid MinGW header conflicts:

```bash
# VS2022
/d/msys64/mingw64/bin/cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 \
    -DENABLE_OPENSSL=OFF
/d/msys64/mingw64/bin/cmake --build build_vs2022 --config Release

# VS2026 (requires cmake 4.0+)
/d/msys64/mingw64/bin/cmake -B build_vs2026 -G "Visual Studio 18 2026" -A x64 \
    -DENABLE_OPENSSL=OFF
/d/msys64/mingw64/bin/cmake --build build_vs2026 --config Release
```

## Linux / macOS

```bash
git clone https://github.com/hydrasdr/hydrasdr_433.git
cd hydrasdr_433
cmake -B build -GNinja
cmake --build build
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HYDRASDR_DIR` | (auto-fetch) | Path to local HydraSDR library (overrides auto-fetch) |
| `HYDRASDR_FETCH_FROM_GIT` | ON | Fetch HydraSDR from GitHub if not found locally |
| `ENABLE_OPENSSL` | ON | Enable TLS support (MQTT, InfluxDB) |
| `ENABLE_NATIVE_OPTIMIZATIONS` | OFF | Use `-march=native` (non-portable) |
| `BUILD_TESTING` | ON | Build test suite |

## Static Runtime Configuration

All builds use static C runtime by default:

- **MinGW64**: Uses `-static-libgcc` to avoid libgcc DLL dependency
- **VS2022/VS2026**: Uses `MultiThreaded` runtime (`/MT`) instead of `MultiThreadedDLL` (`/MD`)

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

### Visual Studio 2022/2026 (build_vs2022 or build_vs2026)

```cmd
cd build_vs2022   &:: or build_vs2026

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

Tested on AMD Ryzen 9 7950X3D (Zen 4, AVX-512).

| Test | MinGW64 (GCC 15.2) | VS2022 (MSVC 19.42) | VS2026 (MSVC 19.50) |
|------|---------------------|----------------------|----------------------|
| FFT N=8 | 28 ns/call | 27 ns/call | 24 ns/call |
| FFT N=16 | 59 ns/call | 71 ns/call | 59 ns/call |
| Channelizer 4-ch | 73.1 MSps | 71.9 MSps | 31.5 MSps |
| Channelizer 8-ch | 81.9 MSps | 70.6 MSps | 35.3 MSps |
| Channelizer 16-ch | 85.3 MSps | 68.3 MSps | 33.2 MSps |

## Platform-Specific Dependencies

### Debian/Ubuntu

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libtool libusb-1.0-0-dev libssl-dev
```

### Fedora/RHEL

```bash
sudo dnf install cmake ninja-build gcc libusb1-devel openssl-devel
```

### macOS (Homebrew)

```bash
brew install cmake ninja pkg-config openssl libusb
```

### Windows (MSYS2)

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja mingw-w64-x86_64-openssl mingw-w64-x86_64-libusb
```

## Troubleshooting

### HydraSDR Not Found

By default, HydraSDR is fetched automatically from GitHub. If the fetch fails (e.g., no internet), you can use a local build by passing `-DHYDRASDR_DIR=/path/to/hydrasdr-host`.

When using a local build, ensure the directory contains:
- `libhydrasdr/src/hydrasdr.h` (header)
- `build_mingw64/libhydrasdr/src/libhydrasdr.dll.a` (MinGW)
- `build_VS2022/libhydrasdr/src/Release/hydrasdr.lib` (VS2022/VS2026)

### VS2022/VS2026 Build Links Against MinGW Library

The FindHydraSDR module automatically selects the correct library based on compiler. If issues persist, clear CMakeCache.txt and reconfigure:

```cmd
:: VS2022
del build_vs2022\CMakeCache.txt
cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 -DENABLE_OPENSSL=OFF

:: VS2026
del build_vs2026\CMakeCache.txt
cmake -B build_vs2026 -G "Visual Studio 18 2026" -A x64 -DENABLE_OPENSSL=OFF
```

### Missing DLLs at Runtime

All builds copy DLLs automatically. If DLLs are missing, reconfigure with a clean CMakeCache.txt:

```bash
# MinGW64
rm build_mingw64/CMakeCache.txt
cmake -B build_mingw64 -G Ninja

# VS2022
del build_vs2022\CMakeCache.txt
cmake -B build_vs2022 -G "Visual Studio 17 2022" -A x64 -DENABLE_OPENSSL=OFF

# VS2026
del build_vs2026\CMakeCache.txt
cmake -B build_vs2026 -G "Visual Studio 18 2026" -A x64 -DENABLE_OPENSSL=OFF
```

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines on submitting changes.
