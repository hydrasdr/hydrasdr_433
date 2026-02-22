# Supported Protocols

hydrasdr_433 supports **291 protocol decoders** (285 active + 6 reserved slots),
inherited from [rtl_433](https://github.com/merbanan/rtl_433) and fully compatible
with the HydraSDR wideband channelizer pipeline.

Protocol numbers are assigned at compile time from the `DEVICES` macro in
`include/rtl_433_devices.h`. Enable/disable individual protocols with `-R <num>`
on the command line or `protocol <num>` in the config file.

## Frequency Bands

| Band | Region | Typical Use |
|------|--------|-------------|
| 27 MHz | Worldwide | Legacy garage remotes |
| 303-310 MHz | US | Ceiling fans, X10 |
| 315 MHz | US/Japan | TPMS, automotive RKE, garage doors |
| 318-319.5 MHz | US | Security systems (Interlogix, Jasco, Megacode) |
| 345 MHz | US | Honeywell/Ademco security |
| 390 MHz | US | Garage door openers (Security+) |
| 433.92 MHz | Worldwide (EU ISM) | Weather, remotes, TPMS (EU), security |
| 868 MHz | EU SRD | Weather (Bresser, LaCrosse IT+), M-Bus, thermostats |
| 915 MHz | US ISM | Utility meters, LaCrosse View, Fine Offset US, Insteon |
| 2.4 GHz | Worldwide | ANT+ (requires wideband SDR) |

## Modulation Types

| Abbreviation | Full Name |
|--------------|-----------|
| OOK_PWM | On-Off Keying, Pulse Width Modulation |
| OOK_PPM | On-Off Keying, Pulse Position Modulation |
| OOK_PCM | On-Off Keying, Pulse Code Modulation (NRZ) |
| OOK_MANCHESTER | On-Off Keying, Manchester encoding |
| OOK_DMC | On-Off Keying, Differential Manchester |
| OOK_RZ | On-Off Keying, Return-to-Zero |
| OOK_NRZS | On-Off Keying, Non-Return-to-Zero Space |
| OOK_PIWM | On-Off Keying, Pulse Interval Width Modulation |
| OOK_PWM_OSV1 | On-Off Keying, PWM variant for Oregon Scientific v1 |
| FSK_PCM | Frequency Shift Keying, PCM (NRZ) |
| FSK_PWM | Frequency Shift Keying, Pulse Width Modulation |
| FSK_MANCHESTER | Frequency Shift Keying, Manchester encoding |

## Protocol List

The **Default** column reflects the example config file (`conf/hydrasdr_433.example.conf`).
The software itself enables all protocols by default when no config is specified.

| # | Name | Modulation | Freq (MHz) | Data Fields | Default | Category |
|----:|------|------------|-----------|-------------|:-------:|----------|
| 1 | Silvercrest Remote Control | OOK_PWM | 433.92 | code, buttons | on | Remote |
| 2 | Rubicson, TFA 30.3197, InFactory PT-310 Temperature Sensor | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 3 | Prologue, FreeTec NC-7104, NC-7159-675 temperature sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 4 | Waveman Switch Transmitter | OOK_PWM | 433.92 | id, channel, switch_state | on | Remote |
| 5 | *(reserved)* | — | — | — | — | — |
| 6 | ELV EM 1000 | OOK_PPM | **868.35** | power_W, energy_kWh | off | Energy |
| 7 | ELV WS 2000 | OOK_PWM | **868.35** | temperature_C, humidity, wind, rain, pressure_hPa | off | Weather Station |
| 8 | LaCrosse TX Temperature / Humidity Sensor | OOK_PWM | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 9 | *(reserved)* | — | — | — | — | — |
| 10 | Acurite 896 Rain Gauge | OOK_PPM | 433.92 | rain_mm, battery_ok | on | Weather/Rain |
| 11 | Acurite 609TXC Temperature and Humidity Sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 12 | Oregon Scientific Weather Sensor (v1/v2/v3) | OOK_MANCHESTER | 433.92 | temperature_C, humidity, rain_mm, wind, pressure_hPa, battery_ok | on | Weather Station |
| 13 | Mebus 433 | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | off | Weather/Temp+Hum |
| 14 | Intertechno 433 | OOK_PPM | 433.92 | id, unit, command | off | Remote |
| 15 | KlikAanKlikUit Wireless Switch | OOK_PPM | 433.92 | id, unit, command, dim_level | on | Remote |
| 16 | AlectoV1 Weather Sensor (WS3500, Ventus W155/W044) | OOK_PPM | 433.92 | temperature_C, humidity, rain_mm, wind_avg_km_h, wind_dir_deg, battery_ok | on | Weather Station |
| 17 | Cardin S466-TX2 | OOK_PWM | **27.195** | id, button | on | Remote (27 MHz) |
| 18 | Fine Offset WH2, WH5, Telldus Temp/Humidity/Rain | OOK_PWM | 433.92 | temperature_C, humidity, rain_mm, battery_ok | on | Weather/Temp+Hum |
| 19 | Nexus, FreeTec NC-7345, Solight TE82S, TFA 30.3209 | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 20 | Ambient Weather F007TH, TFA 30.3208.02, SwitchDocLabs F016TH | OOK_MANCHESTER | 433.92, 868 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 21 | Calibeur RF-104 Sensor | OOK_PWM | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 22 | X10 RF | OOK_PPM | **310** (US), 433 (EU) | house_code, unit, command | on | Remote |
| 23 | DSC Security Contact | OOK_RZ | 433.92 | id, battery_ok, tamper, alarm | on | Security |
| 24 | Brennenstuhl RCS 2044 | OOK_PWM | 433.92 | id, command | off | Remote |
| 25 | Globaltronics GT-WT-02 Sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 26 | Danfoss CFR Thermostat | FSK_PCM | 433.92 | temperature_C, setpoint_C | on | Thermostat |
| 27 | *(reserved)* | — | — | — | — | — |
| 28 | *(reserved)* | — | — | — | — | — |
| 29 | Chuango Security Technology | OOK_PWM | 433.92 | id, command | on | Security |
| 30 | Generic Remote SC226x EV1527 | OOK_PWM | 433.92 | id, command | on | Remote |
| 31 | TFA-Twin-Plus-30.3049, Conrad KW9010, Ea2 BL999 | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 32 | Fine Offset WH1080/WH3080 Weather Station | OOK_PWM | 433.92, **868.3**, 915 | temperature_C, humidity, rain_mm, wind, lux, uv_index, battery_ok | on | Weather Station |
| 33 | WT450, WT260H, WT405H | OOK_DMC | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 34 | LaCrosse WS-2310 / WS-3600 Weather Station | OOK_PWM | 433.92 | temperature_C, humidity, rain_mm, wind_dir_deg | on | Weather Station |
| 35 | Esperanza EWS | OOK_PPM | 433.92 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 36 | Efergy e2 classic | FSK_PWM | 433.55 | id, battery_ok, current, interval, learn | on | Energy |
| 37 | Inovalley kw9015b, TFA 30.3161 (Rain + Temperature) | OOK_PPM | 433.92 | temperature_C, rain_mm, battery_ok | off | Weather/Rain+Temp |
| 38 | Generic temperature sensor 1 | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 39 | WG-PB12V1 Temperature Sensor | OOK_PWM | 433.92 | temperature_C | on | Weather/Temp |
| 40 | Acurite 592TXR, 5n1, 3n1, Atlas, 515, 6045, 899, 1190/1192 | OOK_PWM | 433.92 | temperature_C, humidity, rain_mm, wind, lux, battery_ok | on | Weather Station |
| 41 | Acurite 986 Refrigerator / Freezer Thermometer | OOK_PPM | 433.92 | temperature_F, battery_ok, channel | on | Fridge/Freezer |
| 42 | HIDEKI TS04 Temperature, Humidity, Wind and Rain | OOK_DMC | 433.92 | temperature_C, humidity, rain_mm, wind_dir_deg, battery_ok | on | Weather Station |
| 43 | Watchman Sonic / Apollo Ultrasonic / Beckett Rocket oil tank | FSK_PCM | 433.92 | temperature_C, depth_cm | on | Tank Level |
| 44 | CurrentCost Current Sensor | FSK_PCM | 433.92 | current_A, battery_ok | on | Energy |
| 45 | emonTx OpenEnergyMonitor | FSK_PCM | 433.92 | power1..4_W, vrms | on | Energy |
| 46 | HT680 Remote control | OOK_PWM | 433.92 | id, command | on | Remote |
| 47 | Conrad S3318P, FreeTec NC-5849-913, ORIA WA50 ST389 | OOK_PPM | 433.92 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 48 | Akhan 100F14 remote keyless entry | OOK_PWM | 433.92 | id, command | off | Automotive/RKE |
| 49 | Quhwa wireless doorbell | OOK_PWM | 433.92 | id, button | on | Remote/Doorbell |
| 50 | OSv1 Temperature Sensor | OOK_PWM_OSV1 | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 51 | Proove / Nexa / KlikAanKlikUit Wireless Switch | OOK_PPM | 433.92 | id, unit, command, dim | on | Remote |
| 52 | Bresser Thermo-/Hygro-Sensor 3CH | OOK_PWM | 433.92 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 53 | Springfield Temperature and Soil Moisture | OOK_PPM | 433.92 | temperature_C, moisture | on | Soil |
| 54 | Oregon Scientific SL109H Remote Thermal Hygro Sensor | OOK_PPM | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 55 | Acurite 606TX / Technoline TX960 Temperature Sensor | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 56 | TFA pool temperature sensor | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Pool Thermometer |
| 57 | Kedsum Temperature & Humidity Sensor, Pearl NC-7415 | OOK_PPM | 433.71 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 58 | Blyss DC5-UK-WH | OOK_PWM | 433.92 | id, channel, command | on | Remote |
| 59 | Steelmate TPMS | FSK_MANCHESTER | 433.92 | pressure_kPa, temperature_C, alarm | on | TPMS |
| 60 | Schrader TPMS | OOK_MANCHESTER | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 61 | LightwaveRF | OOK_PPM | 433.92 | id, channel, level, command | off | Smart Home |
| 62 | Elro DB286A Doorbell | OOK_PWM | 433.92 | id, button | off | Remote/Doorbell |
| 63 | Efergy Optical | FSK_PWM | 433.55 | energy_kWh, battery_ok | on | Energy |
| 64 | Honda Car Key | FSK_PWM | 433.92 (EU), **315** (US/JP) | id, code | off | Automotive/RKE |
| 65 | *(reserved)* | — | — | — | — | — |
| 66 | *(reserved)* | — | — | — | — | — |
| 67 | Radiohead ASK | OOK_PCM | 433.92 | raw payload | on | Generic/IoT |
| 68 | Kerui PIR / Contact Sensor | OOK_PWM | 433.92 | id, motion, battery_ok | on | Security/PIR |
| 69 | Fine Offset WH1050 Weather Station | OOK_PWM | 433.92 | temperature_C, humidity, rain_mm, wind_avg_km_h, battery_ok | on | Weather Station |
| 70 | Honeywell Door/Window Sensor, 2Gig DW10/DW11, RE208 | OOK_MANCHESTER | **345** | id, channel, event, state, contact_open, reed_open, alarm, tamper, battery_ok, heartbeat | on | Security |
| 71 | Maverick ET-732/733 BBQ Sensor | OOK_MANCHESTER | 433.92 | temperature1_C, temperature2_C | on | BBQ Thermometer |
| 72 | RF-tech | OOK_PPM | 433.92 | temperature_C, battery_ok | off | Weather/Temp |
| 73 | LaCrosse TX141-Bv2/Bv3, TX141W, TX145wsdth, (TFA, ORIA) | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok, wind | on | Weather/Temp+Hum |
| 74 | Acurite 00275rm, 00276rm Temp/Humidity with optional probe | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 75 | LaCrosse TX35DTH-IT, TFA 30.3155 Temperature/Humidity | FSK_PCM | **868.24** (EU), 915 (US) | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 76 | LaCrosse TX29IT, TFA 30.3159.IT Temperature | FSK_PCM | **868.24** | temperature_C, battery_ok | on | Weather/Temp |
| 77 | Vaillant calorMatic VRT340f Central Heating Control | OOK_DMC | **868** | heating_mode, setpoint, battery_ok | on | Thermostat |
| 78 | Fine Offset WH25, WH32, WH24, WH65B, HP1000, Misol WS2320 | FSK_PCM | 433.92, **868.2** | temperature_C, humidity, pressure_hPa, battery_ok | on | Weather Station |
| 79 | Fine Offset WH0530 Temperature/Rain Sensor | OOK_PWM | 433.92 | temperature_C, rain_mm, battery_ok | on | Weather/Rain+Temp |
| 80 | IBIS beacon | OOK_MANCHESTER | 433.92 | company_id, vehicle_id, counter | on | Transport/Beacon |
| 81 | Oil Ultrasonic STANDARD FSK | FSK_PCM | 433.92 | level_mm, alarm | on | Tank Level |
| 82 | Citroen TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 83 | Oil Ultrasonic STANDARD ASK (Beckett Rocket TEK377A) | OOK_PCM | **915** | level_mm, alarm | on | Tank Level |
| 84 | Thermopro TP11 Thermometer | OOK_PPM | 433.92 | temperature_C | on | BBQ Thermometer |
| 85 | Solight TE44/TE66, EMOS E0107T, NX-6876-917 | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 86 | Wireless Smoke and Heat Detector GS 558 | OOK_PWM | 433.88 | id, code | off | Security/Smoke |
| 87 | Generic wireless motion sensor | OOK_PWM | **433.3** | id, code | on | Security/PIR |
| 88 | Toyota TPMS | FSK_PCM | **315** | id, type, status, pressure_PSI, temperature_C | on | TPMS |
| 89 | Ford TPMS | FSK_PCM | **315** (US), 433.92 (EU) | id, temperature_C, code | on | TPMS |
| 90 | Renault TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 91 | inFactory, nor-tec, FreeTec NC-3982-913 | OOK_PPM | 434.05 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 92 | FT-004-B Temperature Sensor | OOK_PPM | 433.92 | temperature_C | on | Weather/Temp |
| 93 | Ford Car Key | OOK_DMC | 433.92 (EU), **315** (US) | id, code | on | Automotive/RKE |
| 94 | Philips outdoor temperature sensor (AJ3650) | OOK_PWM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 95 | Schrader TPMS EG53MA4 (Saab, Opel, Vauxhall, Chevrolet) | OOK_MANCHESTER | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 96 | Nexa | OOK_PPM | 433.92 | id, unit, command | on | Remote |
| 97 | ThermoPro TP08/TP12/TP20 thermometer | OOK_PPM | 433.92 | temperature1_C, temperature2_C | on | BBQ Thermometer |
| 98 | GE Color Effects (G35 Christmas lights) | FSK_PCM | 433.92 | id, channel, intensity, command | on | Remote/Lighting |
| 99 | X10 Security | OOK_PPM | **310** (US), 433 (EU) | address, function, battery_ok | on | Security |
| 100 | Interlogix GE UTC Security Devices | OOK_PPM | **319.5** | subtype, id, raw_message, battery_ok, switch1..5 | on | Security |
| 101 | Dish remote 6.3 | OOK_PPM | 433.92 | id, button | off | Remote |
| 102 | SimpliSafe Home Security System | OOK_PIWM | 433.92 | id, command | on | Security |
| 103 | Sensible Living Mini-Plant Moisture Sensor | OOK_PCM | 433.92 | moisture | on | Soil |
| 104 | Wireless M-Bus, Mode C&T, 100kbps | FSK_PCM | **868.95** | meter data | on | Utility/M-Bus |
| 105 | Wireless M-Bus, Mode S, 32.768kbps | FSK_PCM | **868.3** | meter data | on | Utility/M-Bus |
| 106 | Wireless M-Bus, Mode R, 4.8kbps | FSK_MANCHESTER | **868.33** | meter data | off | Utility/M-Bus |
| 107 | Wireless M-Bus, Mode F, 2.4kbps | FSK_PCM | **433.82** | meter data | off | Utility/M-Bus |
| 108 | Hyundai WS SENZOR Remote Temperature Sensor | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 109 | WT0124 Pool Thermometer | OOK_PWM | 433.92 | temperature_C, channel | on | Pool Thermometer |
| 110 | PMV-107J (Toyota) TPMS | FSK_PCM | **315** | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 111 | Emos TTX201 Temperature Sensor | OOK_MANCHESTER | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 112 | Ambient Weather TX-8300, TFA 30.3211.02 | OOK_PPM | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 113 | Ambient Weather WH31E, EcoWitt WH40, WS68 | FSK_PCM | **915** | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 114 | Maverick ET73 | OOK_PPM | 433.92 | temperature1_C, temperature2_C | on | BBQ Thermometer |
| 115 | Honeywell ActivLink Wireless Doorbell | OOK_PWM | 433.92 | id, battery_ok | on | Remote/Doorbell |
| 116 | Honeywell ActivLink Wireless Doorbell (FSK) | FSK_PWM | 433.92 | id, battery_ok | on | Remote/Doorbell |
| 117 | ESA1000 / ESA2000 Energy Monitor | OOK_MANCHESTER | **868.35** | energy_kWh, power_W | off | Energy |
| 118 | Biltema rain gauge | OOK_PPM | 433.92 | temperature_C, rain_mm, battery_ok | off | Weather/Rain+Temp |
| 119 | Bresser Weather Center 5-in-1 | FSK_PCM | **868.3** | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 120 | Digitech XC-0324 / AmbientWeather FT005TH | OOK_PPM | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 121 | Opus/Imagintronix XT300 Soil Moisture | OOK_PWM | 433.92 | temperature_C, moisture_percent | on | Soil |
| 122 | FS20 / FHT | OOK_PWM | **868.35** | house_code, address, command | on | Smart Home |
| 123 | Jansite TPMS Model TY02S | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | off | TPMS |
| 124 | LaCrosse/ELV/Conrad WS7000/WS2500 | OOK_PWM | **868.35** | temperature_C, humidity, pressure_hPa, wind, rain | on | Weather Station |
| 125 | TS-FT002 Ultrasonic Tank Liquid Level Meter | OOK_PPM | 433.92 | temperature_C, level_cm, battery_ok | on | Tank Level |
| 126 | Companion WTR001 Temperature Sensor | OOK_PWM | 433.92 | temperature_C | on | Weather/Temp |
| 127 | Ecowitt WH53/WH0280/WH0281A | OOK_PWM | 433.92 | temperature_C | on | Weather/Temp |
| 128 | DirecTV RC66RX Remote Control | FSK_PCM | 433.92 | id, button | on | Remote |
| 129 | Eurochron temperature and humidity sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | off | Weather/Temp+Hum |
| 130 | IKEA Sparsnas Energy Meter Monitor | FSK_PCM | **868** | energy_kWh, power_W, battery_ok | on | Energy |
| 131 | Microchip HCS200/HCS300 KeeLoq (OOK) | OOK_PWM | 433.92 | id, counter (rolling code) | on | Remote/Rolling Code |
| 132 | TFA Dostmann 30.3196 T/H outdoor sensor | FSK_MANCHESTER | **868.33** | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 133 | Rubicson 48659 Thermometer | OOK_PPM | 433.92 | temperature_F | on | Weather/Temp |
| 134 | AOK/Holman WS5029, Conrad AOK-5056, Optex 990018 | FSK_PCM | **917** | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station (AU) |
| 135 | Philips outdoor temperature sensor (AJ7010) | OOK_PWM | 433.92 | temperature_C | on | Weather/Temp |
| 136 | ESIC EMT7110 power meter | FSK_PCM | **868.28** | power_W, current_A, voltage_V, energy_kWh | on | Energy |
| 137 | Globaltronics QUIGG GT-TMBBQ-05 | OOK_PPM | 433.92 | temperature_F | on | BBQ Thermometer |
| 138 | Globaltronics GT-WT-03 Sensor | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 139 | Norgo NGE101 | OOK_DMC | 433.92 | power_W, energy_kWh, channel, id | on | Energy |
| 140 | Elantra2012 TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 141 | Auriol HG02832, HG05124A-DCF, Rubicson 48957 | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 142 | Fine Offset/ECOWITT WH51, SwitchDoc Labs SM23 Soil Moisture | FSK_PCM | 433.92 | moisture_percent, battery_ok | on | Soil |
| 143 | Holman Industries iWeather WS5029 (older PWM) | FSK_PWM | 433.92 | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 144 | TBH weather sensor | FSK_PCM | 433.93 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 145 | WS2032 weather station | OOK_PWM | 433.92 | temperature_C, humidity, wind, rain_mm, battery_ok | on | Weather Station |
| 146 | Auriol AFW2A1 temperature/humidity sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 147 | TFA Drop Rain Gauge 30.3233.01 | OOK_PWM | 433.92 | rain_mm, battery_ok | on | Weather/Rain |
| 148 | DSC Security Contact (WS4945) | OOK_RZ | 433.92 | id, alarm, battery_ok | on | Security |
| 149 | ERT Standard Consumption Message (SCM) | OOK_MANCHESTER | **912.6** | id, consumption, ert_type | on | Utility/AMR |
| 150 | Klimalogg | OOK_NRZS | 433.92 | temperature_C, humidity, battery_ok | off | Weather/Temp+Hum |
| 151 | Visonic powercode | OOK_PWM | 433.92 | id, alarm, battery_ok | on | Security |
| 152 | Eurochron EFTH-800 temperature and humidity sensor | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 153 | Cotech 36-7959, SwitchDocLabs FT020T | OOK_MANCHESTER | 433.92 | temperature_F, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 154 | Standard Consumption Message Plus (SCMplus) | OOK_MANCHESTER | **912.6** | id, consumption, commodity_type | on | Utility/AMR |
| 155 | Fine Offset WH1080/WH3080 Weather Station (FSK) | FSK_PCM | **868.3** | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 156 | Abarth 124 Spider TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 157 | Missil ML0757 weather station | OOK_PPM | 433.92 | temperature_C, rain_mm, wind, battery_ok | on | Weather Station |
| 158 | Sharp SPC775 weather station | FSK_PWM | **917.2** | temperature_C, humidity, battery_ok | on | Weather Station (AU) |
| 159 | Insteon | FSK_PCM | **915** | id, command, payload | on | Smart Home |
| 160 | ERT Interval Data Message (IDM) | OOK_MANCHESTER | **912.6** | id, interval_data, consumption | on | Utility/AMR |
| 161 | ERT Interval Data Message (IDM) for Net Meters | OOK_MANCHESTER | **912.6** | id, interval_data | on | Utility/AMR |
| 162 | ThermoPro-TX2 temperature sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | off | Weather/Temp+Hum |
| 163 | Acurite 590TX Temperature with optional Humidity | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 164 | Security+ 2.0 (Keyfob) | OOK_PCM | **310, 315, 390** | id, rolling_code, command | on | Garage Door |
| 165 | TFA Dostmann 30.3221.02 T/H Outdoor Sensor | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 166 | LaCrosse LTV-WSDTH01 Breeze Pro Wind Sensor | FSK_PCM | **914.94** (US), 432.92 (EU) | temperature_C, humidity, wind, wind_dir_deg | on | Weather/Wind |
| 167 | Somfy RTS | OOK_PCM | **433.42** | id, control, counter, retransmission | on | Remote/Shutter |
| 168 | Schrader TPMS SMD3MA4 (Subaru, Infiniti, Nissan, Renault) | OOK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 169 | Nice Flor-s remote control for gates | OOK_PWM | 433.92 | id, rolling_code | off | Remote/Gate |
| 170 | LaCrosse LTV-WR1 Multi Sensor | FSK_PCM | **915** | wind_avg_km_h, wind_dir_deg | on | Weather/Wind |
| 171 | LaCrosse LTV-TH Thermo/Hygro Sensor | FSK_PCM | **915** | temperature_C, humidity | on | Weather/Temp+Hum |
| 172 | Bresser 6-in-1, 7-in-1 indoor, soil, Froggit WH6000, Ventus C8488A | FSK_PCM | **868** | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 173 | Bresser 7-in-1, Air Quality PM2.5/PM10, CO2, HCHO/VOC | FSK_PCM | **868** | temperature_C, humidity, rain_mm, wind, pm2_5, co2_ppm, battery_ok | on | Weather Station + AQ |
| 174 | EcoDHOME Smart Socket and MCEE Solar monitor | FSK_PCM | 433.92 | power_W | on | Energy |
| 175 | LaCrosse LTV-R1, R3 Rain Gauge, LTV-W1/W2 Wind | FSK_PCM | **915** (US), **868** (EU) | rain_mm, wind_avg_km_h, battery_ok | on | Weather/Rain+Wind |
| 176 | BlueLine Innovations Power Cost Monitor | OOK_PPM | 433.92 | pulse_ms, battery_ok | on | Energy |
| 177 | Burnhard BBQ thermometer | OOK_PWM | 433.92 | temperature_C, setpoint_C, timer, meat_type | on | BBQ Thermometer |
| 178 | Security+ (Keyfob) | OOK_PCM | **310, 315, 390** | id, fixed_code, rolling_code | on | Garage Door |
| 179 | Cavius smoke, heat and water detector | FSK_PCM | **869.67** | network_id, device_id, alarm, battery_ok | on | Security/Smoke |
| 180 | Jansite TPMS Model Solar | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 181 | Amazon Basics Meat Thermometer | OOK_PCM | 433.92 | temperature_C | on | BBQ Thermometer |
| 182 | TFA Marbella Pool Thermometer | FSK_PCM | **868** | temperature_C | on | Pool Thermometer |
| 183 | Auriol AHFL temperature/humidity sensor | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 184 | Auriol AFT 77 B2 temperature sensor | OOK_PPM | 433.92 | temperature_C | on | Weather/Temp |
| 185 | Honeywell CM921 Wireless Room Thermostat | FSK_PCM | **868** | setpoint_C, temperature_C, mode | on | Thermostat |
| 186 | Hyundai TPMS (VDO) | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 187 | RojaFlex shutter and remote devices | FSK_PCM | 433.92 | id, rolling_code, command | on | Remote/Shutter |
| 188 | Marlec Solar iBoost+ sensors | FSK_PCM | **868.3** | power_W, id | on | Energy/Solar |
| 189 | Somfy io-homecontrol | FSK_PCM | **868.89** | id, dst_id, msg_type, mode, counter, mac | on | Smart Home/Shutter |
| 190 | Ambient Weather WH31L (FineOffset WH57) Lightning | FSK_PCM | **915** | id, lightning_count, battery_ok | on | Weather/Lightning |
| 191 | Markisol, E-Motion, BOFU, Rollerhouse, BF-30x curtain remote | OOK_PWM | 433.92 | id, command | on | Remote/Shutter |
| 192 | Govee Water Leak Detector H5054, Door Contact B5023 | OOK_PWM | 433.92 | id, code, battery_ok | on | Security/Leak |
| 193 | Clipsal CMR113 Cent-a-meter power meter | OOK_PIWM | 433.92 | current1..3_A | on | Energy |
| 194 | Inkbird ITH-20R temperature humidity sensor | FSK_PCM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 195 | RainPoint soil temperature and moisture sensor | OOK_PCM | 433.9 | temperature_C, moisture, battery_ok | on | Soil |
| 196 | Atech-WS308 temperature sensor | OOK_RZ | 433.92 | temperature_C | on | Weather/Temp |
| 197 | Acurite Grill/Meat Thermometer 01185M | OOK_PWM | 433.92 | temperature1_F, temperature2_F, battery_ok | on | BBQ Thermometer |
| 198 | EnOcean ERP1 | OOK_PCM | **868.3** | id, data (varies by EEP) | off | Smart Home/Building |
| 199 | Linear Megacode Garage/Gate Remotes | OOK_PCM | **318** | id, button | on | Garage/Gate |
| 200 | Auriol 4-LD5661/4-LD5972/4-LD6313 temperature/rain | OOK_PPM | 433.92 | temperature_C, rain_mm, battery_ok | off | Weather/Rain+Temp |
| 201 | Unbranded SolarTPMS for trucks | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS (Truck) |
| 202 | Funkbus / Instafunk (Berker, Gira, Jung) | OOK_DMC | **433.42** | id, button, battery_ok | on | Smart Home |
| 203 | Porsche Boxster/Cayman TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 204 | Jasco/GE Choice Alert Security Devices | OOK_PCM | **318** | id, state | on | Security |
| 205 | Telldus weather station FT0385R sensors | OOK_MANCHESTER | 433.92 | temperature_F, humidity, rain_mm, pressure_hPa, wind, battery_ok | on | Weather Station |
| 206 | LaCrosse TX34-IT rain gauge | FSK_PCM | **868.3** | rain_mm, battery_ok | on | Weather/Rain |
| 207 | SmartFire Proflame 2 remote (gas fireplace) | OOK_PCM | **315** | command | on | Remote/Fireplace |
| 208 | AVE TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 209 | SimpliSafe Gen 3 Home Security System | FSK_PCM | 433.9 | id, type, command | on | Security |
| 210 | Yale HSA (Home Security Alarm), YES-Alarmkit | OOK_PWM | 433.92 | id, type, state, event | on | Security |
| 211 | Regency Ceiling Fan Remote | OOK_PWM | **303.75-303.96** | id, speed, direction | on | Remote/Fan |
| 212 | Renault 0435R TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 213 | Fine Offset WS80 weather station | FSK_PCM | 433.92 | temperature_C, humidity, wind, battery_ok | on | Weather Station |
| 214 | EMOS E6016 weatherstation with DCF77 | OOK_PWM | 433.92 | temperature_C, humidity, wind_dir_deg, battery_ok | on | Weather Station |
| 215 | Emax W6 / Altronics / Infactory FWS-1200 / Newentor Q9 / TechniSat IMETEO X6 | FSK_PCM | 433.92 | temperature_F, humidity, wind, rain_mm, battery_ok | on | Weather Station |
| 216 | ANT and ANT+ devices | FSK_PCM | **2457** (2.4 GHz) | raw ANT payload | off | Sports/Fitness |
| 217 | EMOS E6016 rain gauge | OOK_PWM | 433.92 | rain_mm, battery_ok | on | Weather/Rain |
| 218 | Microchip HCS200/HCS300 KeeLoq (FSK) | FSK_PWM | 433.92 | id, counter (rolling code) | on | Remote/Rolling Code |
| 219 | Fine Offset WH45 air quality sensor | FSK_PCM | 433.92 | temperature_C, humidity, pm2_5, co2_ppm, battery_ok | on | Air Quality |
| 220 | Maverick XR-30 BBQ Sensor | FSK_PCM | 433.92 | temperature1_C, temperature2_C | on | BBQ Thermometer |
| 221 | Fine Offset WN34S/L/D, Froggit DP150/D35 | FSK_PCM | 433.92 | temperature_C, battery_ok | on | Weather/Temp |
| 222 | Rubicson Pool Thermometer 48942 | OOK_PWM | 433.92 | temperature_C, battery_ok | on | Pool Thermometer |
| 223 | Badger ORION water meter, 100kbps | FSK_PCM | **916.45** | id, reading_gallons, flags | on | Utility/Water |
| 224 | GEO minim+ energy monitor | FSK_PCM | **868.29** | power_W, energy_kWh | on | Energy |
| 225 | TyreGuard 400 TPMS | OOK_MANCHESTER | **434.1** | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 226 | Kia TPMS (-s 1000k) | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C | on | TPMS |
| 227 | SRSmith Pool Light Remote SRS-2C-TX | FSK_PCM | **915** | id, command | on | Remote/Pool (915 MHz) |
| 228 | Neptune R900 flow meters | OOK_PCM | **915** | id, consumption, leak, backflow | on | Utility/Water |
| 229 | WEC-2103 temperature/humidity sensor | OOK_PPM | 433.92 | temperature_F, humidity, battery_ok | on | Weather/Temp+Hum |
| 230 | Vauno EN8822C | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 231 | Govee Water Leak Detector H5054 | OOK_PWM | 433.92 | id, code, battery_ok | on | Security/Leak |
| 232 | TFA 14.1504.V2 grill and meat thermometer | FSK_PCM | 433.92 | temperature_C, battery_ok | on | BBQ Thermometer |
| 233 | CED7000 Shot Timer | FSK_PCM | 433.92 | rfid, shot_count, final_time, split_time | off | Sport/Shot Timer |
| 234 | Watchman Sonic Advanced / Plus, Tekelek | FSK_PCM | 433.92 | temperature_C, level_cm | on | Tank Level |
| 235 | Oil Ultrasonic SMART FSK | FSK_PCM | 433.92 | level_mm | on | Tank Level |
| 236 | Gasmate BA1008 meat thermometer | OOK_PPM | 433.92 | temperature_C | on | BBQ Thermometer |
| 237 | Flowis flow meters | FSK_PCM | 433.92 | id, flow_volume, alarm | on | Utility/Water |
| 238 | Wireless M-Bus, Mode T Downlink, 32.768kbps | FSK_PCM | **868.3** | meter command | on | Utility/M-Bus |
| 239 | Revolt NC-5642 Energy Meter | OOK_PWM | 433.92 | power_W, energy_kWh, frequency_Hz | on | Energy |
| 240 | LaCrosse TX31U-IT, Weather Channel WS-1910TWC-IT | FSK_PCM | **915** | temperature_C, humidity, wind, rain, battery_ok | on | Weather Station |
| 241 | EezTire E618, Carchet TPMS, TST-507 TPMS | OOK_MANCHESTER | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 242 | Baldr / RainPoint rain gauge | OOK_PPM | 433.92 | id, rain_mm | off | Weather/Rain |
| 243 | Celsia CZC1 Thermostat | OOK_PCM | 433.92 | id, heating_level | on | Thermostat |
| 244 | Fine Offset WS90 weather station | FSK_PCM | 433.92 | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 245 | ThermoPro TX-2C Thermometer and Humidity | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | off | Weather/Temp+Hum |
| 246 | TFA 30.3151 Weather Station | FSK_PCM | 433.92 | temperature_C, humidity, rain_mm, wind, battery_ok | on | Weather Station |
| 247 | Bresser water leakage | FSK_PCM | **868** | id, alarm, battery_ok | on | Security/Leak |
| 248 | Nissan TPMS | FSK_PCM | 433.92 | id, pressure_kPa | off | TPMS |
| 249 | Bresser lightning | FSK_PCM | **868** | id, lightning_count, distance_km, battery_ok | on | Weather/Lightning |
| 250 | Schou 72543, MarQuant, TFA 30.3252 Rain Gauge + Thermometer | OOK_PWM | 433.92 | temperature_F, rain_mm, battery_ok | on | Weather/Rain+Temp |
| 251 | Fine Offset / Ecowitt WH55 water leak sensor | FSK_PCM | 433.92 | id, alarm, battery_ok | on | Security/Leak |
| 252 | BMW Gen4-Gen5 TPMS, Audi, HUF/Beru, Continental, Schrader | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 253 | Watts WFHT-RF Thermostat | OOK_PWM | 433.92 | temperature_C, setpoint_C, id | on | Thermostat |
| 254 | Thermor DG950 weather station | OOK_PWM | 433.92 | temperature_C, wind, wind_dir_deg | on | Weather Station |
| 255 | Mueller Hot Rod water meter | FSK_PCM | 433.92 | id, reading | on | Utility/Water |
| 256 | ThermoPro TP28b Super Long Range Meat Thermometer | FSK_PCM | **915** | temperature1..4_C | on | BBQ Thermometer |
| 257 | BMW Gen2 and Gen3 TPMS | FSK_PCM | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 258 | Chamberlain CWPIRC PIR Sensor | FSK_PCM | **915** | id, motion | on | Security/PIR |
| 259 | ThermoPro TP829B 4-probe Meat Thermometer | FSK_PCM | 433.92 | temperature1..4_C | on | BBQ Thermometer |
| 260 | Arad/Master Meter Dialog3G water meter | FSK_MANCHESTER | 433.92 | id, reading, meter_type | off | Utility/Water |
| 261 | Geevon TX16-3 outdoor sensor | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 262 | Fine Offset WH46 air quality sensor | FSK_PCM | 433.92 | temperature_C, humidity, pm2_5, co2_ppm, battery_ok | on | Air Quality |
| 263 | Vevor Wireless Weather Station 7-in-1 | FSK_PCM | 433.92 | temperature_C, humidity, wind, rain_mm, battery_ok | on | Weather Station |
| 264 | Arexx Multilogger IP-HA90, IP-TH78EXT, TSN-70E | FSK_MANCHESTER | 433.92 | temperature_C, humidity | on | Weather/Temp+Hum |
| 265 | Rosstech DCU-706/Sundance/Jacuzzi spa controller | OOK_PCM | 433.92 | temperature_F, id | on | Pool/Spa |
| 266 | Risco 2 Way Agility, PIR/PET Sensor RWX95P | OOK_PCM | 433.92 | id, motion, battery_ok | on | Security/PIR |
| 267 | ThermoPro TP828B 2-probe Meat Thermometer | FSK_PCM | 433.92 | temperature1_C, temperature2_C, setpoint | on | BBQ Thermometer |
| 268 | Bresser Thermo-/Hygro-Sensor Explore Scientific ST1005H | OOK_PPM | **868** | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 269 | DeltaDore X3D devices | FSK_PCM | **868.95** | temperature_C, command | on | Smart Home/Heating |
| 270 | Quinetic | FSK_PCM | **433.3** | id, command | off | Smart Home |
| 271 | Landis & Gyr Gridstream Power Meters 9.6k | FSK_PCM | **915** (FHSS) | id, energy, demand | on | Utility/Smart Meter |
| 272 | Landis & Gyr Gridstream Power Meters 19.2k | FSK_PCM | **915** (FHSS) | id, energy, demand | on | Utility/Smart Meter |
| 273 | Landis & Gyr Gridstream Power Meters 38.4k | FSK_PCM | **915** (FHSS) | id, energy, demand | on | Utility/Smart Meter |
| 274 | Revolt ZX-7717 power meter | OOK_MANCHESTER | 433.92 | power_W, energy_kWh | on | Energy |
| 275 | GM-Aftermarket TPMS | OOK_MANCHESTER | 433.92 | id, pressure_kPa, temperature_C, battery_ok | on | TPMS |
| 276 | RainPoint HCS012ARF Rain Gauge | OOK_PCM | 433.92 | rain_mm, battery_ok | on | Weather/Rain |
| 277 | Apator Metra E-RM 30 water meter | FSK_PCM | **868** | id, volume_L, date | on | Utility/Water |
| 278 | ThermoPro TX-7B Outdoor Thermometer Hygrometer | FSK_PCM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 279 | Nexus, CRX, Prego sauna temperature sensor | OOK_PPM | 433.92 | temperature_C, battery_ok | on | Sauna/Temp |
| 280 | Homelead HG9901 / Geevon / Dr.Meter soil sensor | OOK_PWM | 433.92 | temperature_C, moisture, battery_ok | on | Soil |
| 281 | Maverick XR-50 BBQ Sensor | FSK_PCM | 433.92 | temperature1..4_C | on | BBQ Thermometer |
| 282 | Orion Endpoint Badger Meter GIF2014W-OSE, water meter FHSS | FSK_PCM | **904.4-924.6** (FHSS) | id, reading, status | on | Utility/Water |
| 283 | Fine Offset WH43 air quality sensor | FSK_PCM | 433.92 | pm2_5, battery_ok | on | Air Quality |
| 284 | Baldr E0666TH Thermo-Hygrometer | OOK_PPM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |
| 285 | bm5-v2 12V Battery Monitor | OOK_PWM | 433.92 | temperature_C, soh_percent, soc_percent, id | on | Automotive/Battery |
| 286 | Universal (Reverseable) 24V Fan Controller | OOK_PWM | 433.92 | id, speed, direction | on | Remote/Fan |
| 287 | Fine Offset WS85 weather station | FSK_PCM | 433.92 | rain_mm, wind, wind_dir_deg, battery_ok | on | Weather Station |
| 288 | Oria WA150KM freezer and fridge thermometer | OOK_PCM | 433.92 | temperature_C, channel | on | Fridge/Freezer |
| 289 | Voltcraft EnergyCount 3000 (ec3k) | FSK_PCM | **868.3** (BFSK) | power_W, energy_kWh | on | Energy |
| 290 | Orion Endpoint Badger Meter GIF2020OCECNA, water meter FHSS | FSK_PCM | **904.4-924.6** (FHSS) | id, reading, status | on | Utility/Water |
| 291 | Geevon TX19-1 outdoor sensor | OOK_PWM | 433.92 | temperature_C, humidity, battery_ok | on | Weather/Temp+Hum |

## Protocols by Frequency Band

### 27 MHz

| # | Name |
|----:|------|
| 17 | Cardin S466-TX2 |

### 303-310 MHz (US)

| # | Name | Freq |
|----:|------|------|
| 22 | X10 RF (US variant) | 310 |
| 99 | X10 Security (US variant) | 310 |
| 164 | Security+ 2.0 (Keyfob) | 310, 315, 390 |
| 178 | Security+ (Keyfob) | 310, 315, 390 |
| 211 | Regency Ceiling Fan Remote | 303.75-303.96 |

### 315 MHz (US/Japan)

| # | Name | Notes |
|----:|------|-------|
| 64 | Honda Car Key (US/JP) | Also 433.92 EU |
| 88 | Toyota TPMS | Japan/US |
| 89 | Ford TPMS (US) | Also 433.92 EU |
| 93 | Ford Car Key (US) | Also 433.92 EU |
| 110 | PMV-107J (Toyota) TPMS | Japan/US |
| 207 | SmartFire Proflame 2 | 314.97 MHz |

### 318-319.5 MHz (US)

| # | Name | Freq |
|----:|------|------|
| 100 | Interlogix GE UTC Security | 319.5 |
| 199 | Linear Megacode Garage/Gate | 318 |
| 204 | Jasco/GE Choice Alert Security | 318 |

### 345 MHz (US — Honeywell/Ademco)

| # | Name |
|----:|------|
| 70 | Honeywell Door/Window Sensor, 2Gig DW10/DW11, RE208 |

### 433 MHz ISM (Worldwide)

The majority of protocols (230+) operate at 433.92 MHz. Notable variants:

| # | Name | Exact Freq |
|----:|------|------------|
| 36 | Efergy e2 classic | 433.55 |
| 57 | Kedsum | 433.71 |
| 63 | Efergy Optical | 433.55 |
| 86 | Smoke Detector GS 558 | 433.88 |
| 87 | Generic motion sensor | 433.3 |
| 91 | inFactory | 434.05 |
| 107 | M-Bus Mode F | 433.82 |
| 167 | Somfy RTS | 433.42 |
| 202 | Funkbus / Instafunk | 433.42 |
| 225 | TyreGuard 400 TPMS | 434.1 |
| 270 | Quinetic | 433.3 |

### 868 MHz EU SRD

| # | Name | Exact Freq | Notes |
|----:|------|------------|-------|
| 6 | ELV EM 1000 | 868.35 | Energy monitor |
| 7 | ELV WS 2000 | 868.35 | Weather station |
| 75 | LaCrosse TX35DTH-IT | 868.24 | Temp+Hum |
| 76 | LaCrosse TX29IT | 868.24 | Temp |
| 77 | Vaillant VRT340f | 868 | Thermostat |
| 78 | Fine Offset WH25 (FSK) | 868.2 | Weather station |
| 104 | M-Bus Mode C&T | 868.95 | 100 kbps, needs -s 1200k |
| 105 | M-Bus Mode S | 868.3 | 32.768 kbps, needs -s 1000k |
| 106 | M-Bus Mode R | 868.33 | 4.8 kbps |
| 117 | ESA1000/ESA2000 | 868.35 | Energy monitor |
| 119 | Bresser 5-in-1 | 868.3 | Weather station |
| 122 | FS20 / FHT | 868.35 | Home automation |
| 124 | LaCrosse WS7000/WS2500 | 868.35 | Weather station |
| 130 | IKEA Sparsnas | 868 | Energy monitor |
| 132 | TFA 30.3196 | 868.33 | Temp+Hum |
| 136 | ESIC EMT7110 | 868.28 | Power meter |
| 155 | Fine Offset WH1080 (FSK) | 868.3 | Weather station |
| 172 | Bresser 6-in-1 | 868 | Weather station |
| 173 | Bresser 7-in-1 | 868 | Weather + Air Quality |
| 179 | Cavius | 869.67 | Smoke/heat/water |
| 182 | TFA Marbella | 868 | Pool thermometer |
| 185 | Honeywell CM921 | 868 | Thermostat |
| 188 | Marlec Solar iBoost+ | 868.3 | Solar energy |
| 189 | Somfy io-homecontrol | 868.89 | Shutters |
| 198 | EnOcean ERP1 | 868.3 | Building automation |
| 206 | LaCrosse TX34-IT | 868.3 | Rain gauge |
| 224 | GEO minim+ | 868.29 | Energy monitor |
| 238 | M-Bus Mode T Downlink | 868.3 | Utility meters |
| 247 | Bresser water leakage | 868 | Leak detector |
| 249 | Bresser lightning | 868 | Lightning detector |
| 268 | Bresser ST1005H | 868 | Temp+Hum |
| 269 | DeltaDore X3D | 868.95 | Home automation |
| 277 | Apator Metra E-RM 30 | 868 | Water meter |
| 289 | Voltcraft ec3k | 868.3 | Energy, BFSK dual-tone |

### 915 MHz US ISM / AU

| # | Name | Exact Freq | Notes |
|----:|------|------------|-------|
| 83 | Oil Ultrasonic STANDARD ASK | 915 | Tank level |
| 113 | Ambient Weather WH31E | 915 | Weather station |
| 134 | Holman WS5029 (FSK) | 917.0 | AU market |
| 149 | ERT SCM | 912.6 | US utility meters |
| 154 | ERT SCMplus | 912.6 | US utility meters |
| 158 | Sharp SPC775 | 917.2 | AU market |
| 159 | Insteon | 915 | Home automation |
| 160 | ERT IDM | 912.6 | US utility meters |
| 161 | ERT NetIDM | 912.6 | US utility meters |
| 166 | LaCrosse Breeze Pro | 914.94 | Wind sensor |
| 170 | LaCrosse LTV-WR1 | 915 | Wind sensor |
| 171 | LaCrosse LTV-TH | 915 | Temp+Hum |
| 175 | LaCrosse LTV-R1/W1 | 915 | Rain + Wind |
| 190 | Fine Offset WH31L | 915 | Lightning |
| 223 | Badger ORION | 916.45 | Water meter, needs -s 1200k |
| 227 | SRSmith Pool Light | 915 | Remote |
| 228 | Neptune R900 | 915 | Water meter |
| 240 | LaCrosse TX31U-IT | 915 | Weather station |
| 256 | ThermoPro TP28b | 915 | BBQ thermometer |
| 258 | Chamberlain CWPIRC | 915 | PIR sensor |
| 271 | Gridstream 9.6k | 902-928 FHSS | Smart meter |
| 272 | Gridstream 19.2k | 902-928 FHSS | Smart meter |
| 273 | Gridstream 38.4k | 902-928 FHSS | Smart meter |
| 282 | Orion Endpoint (2014) | 904.4-924.6 FHSS | Water meter, needs -s 1600k |
| 290 | Orion Endpoint (2020) | 904.4-924.6 FHSS | Water meter, needs -s 1600k |

### 2.4 GHz

| # | Name | Notes |
|----:|------|-------|
| 216 | ANT and ANT+ devices | 2457.025 MHz, requires wideband SDR (PlutoSDR or similar) |

## Statistics

| Modulation | Count |
|------------|------:|
| FSK_PCM | 98 |
| OOK_PWM | 64 |
| OOK_PPM | 61 |
| OOK_PCM | 19 |
| OOK_MANCHESTER | 19 |
| FSK_PWM | 7 |
| OOK_DMC | 6 |
| FSK_MANCHESTER | 5 |
| OOK_RZ | 3 |
| OOK_PIWM | 2 |
| OOK_PWM_OSV1 | 1 |
| OOK_NRZS | 1 |
| Reserved slots | 6 |

Counts are from `r_device` structs in `src/devices/*.c`. Some files define
sub-variants (e.g., m\_bus.c defines 5 structs), so totals slightly exceed 291.

## Sample Rate Requirements

Most protocols work with the default 250 kHz sample rate. These require higher rates:

| Protocol | Min Sample Rate | Notes |
|----------|-----------------|-------|
| 104 (M-Bus C&T) | 1200 kHz | 100 kbps FSK |
| 105 (M-Bus S) | 1000 kHz | 32.768 kbps |
| 198 (EnOcean ERP1) | 1000 kHz | 125 kbps ASK |
| 223 (Badger ORION) | 1200 kHz | 100 kbps FSK |
| 226 (Kia TPMS) | 1000 kHz | Wide FSK deviation |
| 282, 290 (Orion Endpoint) | 1600 kHz | FHSS hopping |
| 271-273 (Gridstream) | 1000 kHz | FHSS |
