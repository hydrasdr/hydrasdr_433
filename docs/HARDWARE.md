# Hardware tested with hydrasdr_433

hydrasdr_433 is a dedicated HydraSDR fork.
For RTL-SDR and SoapySDR support, see the upstream [rtl_433](https://github.com/merbanan/rtl_433/).

## HydraSDR

Native support for HydraSDR family devices using the HydraSDR API v1.1.0.

Supported devices:
- HydraSDR RFOne

Device selection:
- `-d hydrasdr` - Use first available HydraSDR device
- `-d hydrasdr:0` - Use HydraSDR device by index
- `-d hydrasdr:serial=XXXX` - Use HydraSDR device by serial number (hex)

Settings (via `-t`):
- `biastee=1` - Enable bias tee (if supported)
- `decimation=1` - Use high definition decimation mode
- `bandwidth=2500000` - Set RF bandwidth in Hz (if supported)

The HydraSDR backend uses capability discovery to adapt to different hardware variants.
Sample rates are automatically selected from available rates, with decimation used to achieve
the closest match to requested rates.

See also [HydraSDR](https://hydrasdr.com/).

## Other input sources

- **rtl_tcp**: Remote SDR data via TCP network (`-d rtl_tcp:host:port`)
- **Files**: CU8, CS16, CF32 I/Q data, AM data (`-r filename`)

## Not supported

- Ultra cheap 1-bit (OOK) receivers, and antenna-on-a-raspi-pin
- CC1101, and alike special purpose / non general SDR chips
- RTL-SDR (use upstream [rtl_433](https://github.com/merbanan/rtl_433/))
- SoapySDR (use upstream [rtl_433](https://github.com/merbanan/rtl_433/))
