# HydraSDR-433 Protocol Compatibility Testing

## Overview

`run_comparison.py` validates hydrasdr_433 against the rtl_433 reference test
suite. It runs each test file through hydrasdr_433, compares the JSON output
against expected reference data, and generates a detailed Markdown report.

## Prerequisites

- Python 3 (no external dependencies)
- hydrasdr_433 built (MinGW64/Ninja or VS2022)
- rtl_433_tests repository cloned alongside hydrasdr_433:
  ```
  <parent>/
      hydrasdr_433/         # this repo
      rtl_433_tests/        # https://github.com/merbanan/rtl_433_tests
  ```

Clone the test suite if not already present:
```bash
cd <parent>
git clone https://github.com/merbanan/rtl_433_tests
```

## Usage

### From the hydrasdr_433 directory (recommended)

```bash
python3 tests/run_comparison.py \
    -c build/src/hydrasdr_433.exe \
    -t ../rtl_433_tests/tests \
    -I time \
    -o TEST_HYDRASDR_433.md
```

### From the rtl_433_tests directory

```bash
python3 ../hydrasdr_433/tests/run_comparison.py \
    -c ../hydrasdr_433/build/src/hydrasdr_433.exe \
    -I time \
    -o ../hydrasdr_433/TEST_HYDRASDR_433.md
```

When run from the rtl_433_tests directory, the `-t` option can be omitted
(defaults to `tests/` in the current directory).

### Print to stdout (no file)

Omit `-o` to print the report to the console:
```bash
python3 tests/run_comparison.py \
    -c build/src/hydrasdr_433.exe \
    -t ../rtl_433_tests/tests \
    -I time
```

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `-c` | `rtl_433` | Path to the hydrasdr_433 executable |
| `-t` | `tests/` | Path to the test directory (rtl_433_tests/tests) |
| `-I` | `time` | Field(s) to ignore in comparison (repeatable) |
| `-o` | stdout | Output Markdown file path |
| `-C` | `../conf` | Config file search path |
| `--first-line` | off | Only compare the first output line per test |

## Output

The report (`TEST_HYDRASDR_433.md`) contains:

- **Summary table** with pass/fail/error counts and percentages
- **Per-protocol results** showing pass, extra decode, mismatch, missing, error
- **Extra decodes** section listing tests with duplicate decode sensitivity
- **Detailed failures** with specific field differences
- **False positives** showing cross-decoder matches
- **Methodology** and **conclusion** with overall compatibility percentage

## Result Categories

| Category | Meaning |
|----------|---------|
| **PASS (exact)** | Output matches reference JSON line-for-line |
| **PASS (extra decode)** | All expected data present + extra duplicate decode(s) |
| **FAIL (value mismatch)** | Field values differ from reference |
| **FAIL (missing decode)** | Fewer decoded lines than expected |
| **No output** | No matching decoder output produced |
| **Error** | Timeout (30s) or executable launch failure |
| **Missing input** | No .cu8/.ook/.cs16/.cf32 file for reference |

## Typical Run

```
$ python3 tests/run_comparison.py -c build/src/hydrasdr_433.exe \
    -t ../rtl_433_tests/tests -I time -o TEST_HYDRASDR_433.md

Found 1873 reference JSON files in ../rtl_433_tests/tests/
  [100/1873] 1s elapsed...
  ...
  [1800/1873] 21s elapsed...

Report written to TEST_HYDRASDR_433.md

=== RESULTS: 1809 exact pass, 18 extra decode, 1 mismatch, ... (21.8s) ===
=== Effective pass rate: 99.9% ===
```

The exit code equals the number of hard failures (mismatch + missing decode),
so it can be used in CI scripts.
