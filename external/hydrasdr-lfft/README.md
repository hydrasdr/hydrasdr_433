# hydrasdr-lfft

Light FFT library optimized for small sizes (2-32 points) used in PFB channelizers.

## Features

- **Stockham algorithm**: Cache-friendly, in-place-compatible, no bit-reversal
- **Radix-4 + Radix-2**: Efficient handling of any power-of-2 size
- **On-the-fly twiddles**: Reduced memory footprint
- **Portable C99**: Works on any platform, relies on compiler auto-vectorization
- **MIT License**: No GPL dependencies

## Supported FFT Sizes

- Minimum: 2 points
- Optimized for: 2, 4, 8, 16, 32 points
- Constraint: Must be power of 2

## API

```c
#include "hydrasdr_lfft.h"

// Initialize (optional - auto-initialized on first use)
hlfft_init();

// Create plan for 8-point FFT
hlfft_plan_t *plan = hlfft_plan_create(8, NULL);

// Prepare data (interleaved I/Q format)
hlfft_complex_t input[8], output[8];

// Execute forward FFT
hlfft_forward(plan, input, output);

// Execute inverse FFT (result not normalized)
hlfft_inverse(plan, output, input);

// Cleanup
hlfft_plan_destroy(plan);
hlfft_shutdown();
```

## Data Format

Interleaved complex format (I/Q pairs):

```
Memory: [re0, im0, re1, im1, ..., re(N-1), im(N-1)]
```

Compatible with SDR I/Q streams. Cast `float*` directly:

```c
float *iq_data = ...;
hlfft_forward(plan, (hlfft_complex_t *)iq_data, output);
```

## Building

Built as part of hydrasdr_433:

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja
```

## Files

```
hydrasdr-lfft/
├── CMakeLists.txt          Build configuration
├── README.md               This file
├── hydrasdr_lfft.h         Public API header
├── hydrasdr_lfft.c         Public API implementation
├── stockham_scalar.c       Scalar Stockham FFT core
├── stockham_internal.h     Internal types and declarations
├── compat_opt.h            Portable optimization macros
└── lfft_test.c             Correctness and benchmark tests
```

## Algorithm

Stockham autosort FFT with radix-4 butterflies:

1. **Radix-4 stages**: 4 elements per butterfly, log₄(N) stages
2. **Radix-2 cleanup**: Final stage for non-power-of-4 sizes (N=2, 8, 32)
3. **Ping-pong buffers**: Alternates between work buffers
4. **On-the-fly twiddles**: W^{2k} and W^{3k} computed from stored W^k

## Performance

Designed for small FFTs in PFB channelizers:

| Size | Typical Use | Performance |
|------|-------------|-------------|
| 2 | 2-ch channelizer | ~50 M-FFT/s |
| 4 | 4-ch channelizer | ~25 M-FFT/s |
| 8 | 8-ch channelizer | ~12 M-FFT/s |
| 16 | 16-ch channelizer | ~5 M-FFT/s |
| 32 | 32-ch channelizer | ~2 M-FFT/s |

For large FFTs (≥64 points), use hydrasdr-fft with SIMD backends.

## License

MIT License

Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
