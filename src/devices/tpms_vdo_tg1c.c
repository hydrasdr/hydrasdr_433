/** @file
    Continental/VDO TG1C TPMS decoder -- robust unified variant (10-byte protocol).

    Copyright (C) 2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Continental/VDO TG1C TPMS sensor -- 10-byte FSK Manchester protocol.

Robust unified decoder for the Continental/VDO TG1C sensor family.
Supersedes tpms_citroen (XOR only, inverted only) and tpms_hyundai_vdo
(CRC only, inverted only, 4-byte preamble) with:
  - CRC-8 primary validation + XOR checksum fallback
  - Both preamble polarities (inverted and non-inverted)
  - Shorter 2-byte preamble for better noise tolerance

Note: The TG1C physical sensor family also includes a 9-byte variant
(see tpms_abarth124) with a different packet layout.  This decoder
handles only the 10-byte variant described below.

FCC IDs: KR5S180052092, KR5TIS-01 (Continental Automotive GmbH)

Sensor part numbers (confirmed 10-byte protocol):
  S180052092, S180052094A  -- Hyundai/Kia older gen, Citroen, Mitsubishi
  S180052062, A2C98607702  -- Hyundai/Kia newer gen (i30 PD, Kona, Niro)
  A2C1446770080            -- Hyundai/Kia OE replacement

Confirmed compatible vehicles (433 MHz, 10-byte protocol):

  Hyundai:     Accent (RB 2011-2018, HCI 2017-2023),
               Elantra (AD 2015-2021), Equus (2009-2016),
               Genesis / Genesis Coupe (2008-2020),
               Grandeur (2014-2023),
               i20 (PB 2012-2015),
               i30 (GD 2012-2018, PD 2017-2022, PDE/N 2017+),
               i30 Fastback (2018-2023), Ioniq (2016-2021),
               ix20 (JC 2010-2018), ix55 (2007-2016),
               Kona (OS 2017-2023), Porter (2006-2017),
               Santa Fe III (DM 2012-2019), Solaris (2017-2023)

  Kia:         Cadenza (2011-2023), Carens IV (2013-2019),
               Cee'd / Pro Cee'd (JD 2012-2019),
               K5 (2021-2023), Mohave (2008-2019),
               Niro (2016-2021), Optima (2015-2021),
               Picanto (2011-2018), Rio (2012-2018),
               Seltos (2021-2023), Sorento II/III (2012-2021),
               Soul (2012-2015), Sportage (2016-2021),
               Venga (2009-2018)

  Genesis:     G80 (2017-2020), G90 (2017-2023),
               GV70 (2021+), GV80 (2021+)

  Citroen:     C-Zero (2014-2020), C4 Aircross (2014-2017)

  Peugeot:     4008 (2014-2017), iOn (2014-2020)

  Mitsubishi:  ASX (2014-2020), Attrage (2014-2022),
               Eclipse Cross (2018+), i-MiEV (2014-2021),
               L200 / Triton (2014-2024), Lancer (2014-2017),
               Outlander / PHEV (2014-2022),
               Pajero / Montero (2014-2022),
               Space Star / Mirage (2014+)

  Fiat:        Fullback / Cross (2016-2020)

Aftermarket programmable sensors (Schrader EZ-sensor, Hamaton U-Pro,
RIDEX, SKF, Bosch, ESEN SKV, Quinton Hazell, etc.) clone this same
protocol when configured for any of the above vehicles.

References:
  FCC test reports (KR5S180052092, 2013-03-08):
    https://fcc.report/FCC-ID/KR5S180052092
  Continental TPMS OE application list:
    https://www.continental-aftermarket.com/media/2443/continental-tpms-application-list.pdf
  VDO TG1C sensor datasheets (Tyresure):
    https://www.tyresure-tpms.com/pages/product_details.php?product=CON-S084
  Hyundai TPMS fitment (Bartec):
    https://www.hyundaitpms.com/hyundai-tpms-types-fitment
  Kia TPMS fitment (Bartec):
    https://www.kiatpms.com/kia-tpms-types-fitment
  Kia Cee'd TPMS service manual:
    https://www.kceed.com/tpms_sensor_description_and_operation-1172.html
  rtl_433 Hyundai VDO discussion:
    https://github.com/merbanan/rtl_433/issues/3097

RF parameters (from FCC test report 13008318, 2013-03-08):
- Carrier:       433.920 MHz (26 MHz crystal)
- Modulation:    FSK, ITU emission class 115K F1D
- Deviation:     +/- 38 kHz (measured 20 dB BW: 114.79 kHz)
- Encoding:      Manchester (IEEE 802.3)
- Data rate:     ~9615 baud (52 us half-bit), Manchester -> 19230 baud symbol
- Occupied BW:   117.6 kHz (99%)
- Antenna:       Metal strip loop (part of PCB)

Burst structure (from FCC periodic operation test):
- Wake-up:       ASK, 46 ms, 12 pulses of 1.594 ms
- Data frames:   4 x ~10.3 ms FSK, ~100 ms apart
- Burst total:   521.74 ms
- Burst interval: ~16 seconds (silent >= 15.65 s)

Packet nibbles (10 bytes after Manchester decode):

    SS IIIIIIII FR PP TT BB CC

- S:  State/status (8 bits), not covered by checksum
- I:  Sensor ID (32 bits, big-endian)
- F:  Flags (4 bits, upper nibble)
- R:  Repeat counter (4 bits, lower nibble, cycles 0-3)
- P:  Pressure (8 bits), kPa = raw * 1.364
- T:  Temperature (8 bits), deg C = raw - 50
- B:  Battery indicator (8 bits)
- C:  CRC-8 (poly=0x07, init=0xAA) or XOR checksum (bytes 1-9 = 0)
*/

#include "decoder.h"

static int tpms_vdo_tg1c_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int state;
    unsigned id;
    int flags;
    int repeat;
    int pressure;
    int temperature;
    int battery;
    char const *mic_type;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 88);

    if (packet_bits.bits_per_row[0] < 80)
        return DECODE_FAIL_SANITY;

    b = packet_bits.bb[0];

    /* Sanity: pressure and temperature should be non-zero */
    if (b[6] == 0 || b[7] == 0)
        return DECODE_ABORT_EARLY;

    /* CRC-8 check (poly=0x07, init=0xAA, bytes 0-8) */
    uint8_t crc = crc8(b, 9, 0x07, 0xAA);
    if (crc == b[9]) {
        mic_type = "CRC";
    } else {
        /* XOR checksum fallback (bytes 1-9 XOR = 0) */
        uint8_t xor_val = xor_bytes(&b[1], 9);
        if (xor_val != 0)
            return DECODE_FAIL_MIC;
        mic_type = "CHECKSUM";
    }

    state       = b[0];
    id          = (unsigned)b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4];
    flags       = b[5] >> 4;
    repeat      = b[5] & 0x0f;
    pressure    = b[6];
    temperature = b[7];
    battery     = b[8];

    char state_str[3];
    snprintf(state_str, sizeof(state_str), "%02x", state);
    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "VDO-TG1C",
            "type",          "",            DATA_STRING, "TPMS",
            "id",            "",            DATA_STRING, id_str,
            "state",         "",            DATA_STRING, state_str,
            "flags",         "",            DATA_INT,    flags,
            "repeat",        "",            DATA_INT,    repeat,
            "pressure_kPa",  "Pressure",    DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.364,
            "temperature_C", "Temperature", DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
            "battery_ok",    "Battery",     DATA_INT,    battery > 5 ? 1 : 0,
            "mic",           "Integrity",   DATA_STRING, mic_type,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_vdo_tg1c_decode() */
static int tpms_vdo_tg1c_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /*
     * Preamble: alternating 010101...0110 sync.
     * Appears as {0x55, 0x56} or {0xaa, 0xa9} depending on
     * FSK demodulator polarity assignment (F1/F2 mapping).
     *
     * Manchester decode outputs bit2 of each pair: "01"->1, "10"->0.
     * Inverting the bitstream swaps all pairs, producing the bitwise
     * NOT of the decoded data.  The CRC/XOR values in this protocol
     * were established with the inverted-decode convention (same as
     * tpms_citroen), so we always search for {0xaa, 0xa9} and let
     * the inversion state determine the Manchester decode output.
     *
     * Pass 1: invert bitbuffer, search {0xaa, 0xa9} -> handles the
     *         standard case (original has {0x55, 0x56}).
     * Pass 2: restore original, search {0xaa, 0xa9} -> handles the
     *         reversed FSK polarity case (original has {0xaa, 0xa9}).
     *         Reversed polarity + no invert gives the same Manchester
     *         decoded bytes as standard polarity + invert.
     */
    uint8_t const preamble[2] = {0xaa, 0xa9};

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    /* Pass 1: invert, then search (standard FSK polarity) */
    bitbuffer_invert(bitbuffer);
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble, 16)) + 178 <=
            bitbuffer->bits_per_row[0]) {
        ret = tpms_vdo_tg1c_decode(decoder, bitbuffer, 0, bitpos + 16);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    /* Pass 2: restore original, search again (reversed FSK polarity) */
    if (!events) {
        bitbuffer_invert(bitbuffer); /* undo the first invert */
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble, 16)) + 178 <=
                bitbuffer->bits_per_row[0]) {
            ret = tpms_vdo_tg1c_decode(decoder, bitbuffer, 0, bitpos + 16);
            if (ret > 0)
                events += ret;
            bitpos += 2;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "state",
        "flags",
        "repeat",
        "pressure_kPa",
        "temperature_C",
        "battery_ok",
        "mic",
        NULL,
};

r_device const tpms_vdo_tg1c = {
        .name        = "Continental VDO TG1C TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 52 us half-bit, ~19230 baud Manchester
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us]
        .decode_fn   = &tpms_vdo_tg1c_callback,
        .fields      = output_fields,
};
