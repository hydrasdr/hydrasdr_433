########################################################################
# Build libraries and executables
########################################################################
# commodity libraries
# consider -fvisibility=hidden
# Proper object library type was only introduced with CMake 2.8.8
add_library(r_433 STATIC
    abuf.c
    am_analyze.c
    baseband.c
    bit_util.c
    bitbuffer.c
    cf32_resampler.c
    channelizer.c
    channelizer_sse2.c
    channelizer_avx2.c
    channelizer_avx512.c
    channelizer_neon.c
    channelizer_sve.c
    compat_paths.c
    compat_time.c
    confparse.c
    data.c
    data_tag.c
    decoder_util.c
    fileformat.c
    http_server.c
    jsmn.c
    list.c
    logger.c
    mongoose.c
    optparse.c
    output_file.c
    output_influx.c
    output_log.c
    output_mqtt.c
    output_rtltcp.c
    output_trigger.c
    output_udp.c
    pulse_analyzer.c
    pulse_data.c
    pulse_detect.c
    pulse_detect_fsk.c
    pulse_slicer.c
    r_api.c
    r_util.c
    raw_output.c
    rfraw.c
    samp_grab.c
    sdr.c
    term_ctl.c
    wb_dedup.c
    write_sigrok.c
    devices/abmt.c
    devices/acurite.c
    devices/acurite_01185m.c
    devices/akhan_100F14.c
    devices/alecto.c
    devices/ambient_weather.c
    devices/ambientweather_tx8300.c
    devices/ambientweather_wh31e.c
    devices/ant_antplus.c
    devices/apator_metra_erm30.c
    devices/arad_ms_meter.c
    devices/archos_tbh.c
    devices/arexx_ml.c
    devices/atech_ws308.c
    devices/auriol_4ld5661.c
    devices/auriol_aft77b2.c
    devices/auriol_afw2a1.c
    devices/auriol_ahfl.c
    devices/auriol_hg02832.c
    devices/badger_orion_endpoint.c
    devices/badger_water.c
    devices/baldr_rain.c
    devices/baldr_therm.c
    devices/blueline.c
    devices/blyss.c
    devices/bm5.c
    devices/brennenstuhl_rcs_2044.c
    devices/bresser_3ch.c
    devices/bresser_5in1.c
    devices/bresser_6in1.c
    devices/bresser_7in1.c
    devices/bresser_leakage.c
    devices/bresser_lightning.c
    devices/bresser_st1005h.c
    devices/bt_rain.c
    devices/burnhardbbq.c
    devices/calibeur.c
    devices/cardin.c
    devices/cavius.c
    devices/ced7000.c
    devices/celsia_czc1.c
    devices/chamberlain_cwpirc.c
    devices/chuango.c
    devices/cmr113.c
    devices/companion_wtr001.c
    devices/cotech_36_7959.c
    devices/current_cost.c
    devices/danfoss.c
    devices/deltadore_x3d.c
    devices/digitech_xc0324.c
    devices/directv.c
    devices/dish_remote_6_3.c
    devices/dsc.c
    devices/ec3k.c
    devices/ecodhome.c
    devices/ecowitt.c
    devices/efergy_e2_classic.c
    devices/efergy_optical.c
    devices/efth800.c
    devices/elro_db286a.c
    devices/elv.c
    devices/emax.c
    devices/emontx.c
    devices/emos_e6016.c
    devices/emos_e6016_rain.c
    devices/enocean_erp1.c
    devices/ert_idm.c
    devices/ert_scm.c
    devices/esa.c
    devices/esic_emt7110.c
    devices/esperanza_ews.c
    devices/eurochron.c
    devices/fineoffset.c
    devices/fineoffset_wh1050.c
    devices/fineoffset_wh1080.c
    devices/fineoffset_wh31l.c
    devices/fineoffset_wh43.c
    devices/fineoffset_wh45.c
    devices/fineoffset_wh46.c
    devices/fineoffset_wh55.c
    devices/fineoffset_wn34.c
    devices/fineoffset_ws80.c
    devices/fineoffset_ws85.c
    devices/fineoffset_ws90.c
    devices/flex.c
    devices/flowis.c
    devices/fordremote.c
    devices/fs20.c
    devices/ft004b.c
    devices/funkbus.c
    devices/gasmate_ba1008.c
    devices/ge_coloreffects.c
    devices/geevon.c
    devices/geevon_tx19.c
    devices/generic_motion.c
    devices/generic_remote.c
    devices/generic_temperature_sensor.c
    devices/geo_minim.c
    devices/govee.c
    devices/gridstream.c
    devices/gt_tmbbq05.c
    devices/gt_wt_02.c
    devices/gt_wt_03.c
    devices/hcs200.c
    devices/hideki.c
    devices/holman_ws5029.c
    devices/homelead_hg9901.c
    devices/hondaremote.c
    devices/honeywell.c
    devices/honeywell_cm921.c
    devices/honeywell_wdb.c
    devices/ht680.c
    devices/ibis_beacon.c
    devices/ikea_sparsnas.c
    devices/infactory.c
    devices/inkbird_ith20r.c
    devices/inovalley-kw9015b.c
    devices/insteon.c
    devices/interlogix.c
    devices/intertechno.c
    devices/jasco.c
    devices/kedsum.c
    devices/kerui.c
    devices/klimalogg.c
    devices/lacrosse.c
    devices/lacrosse_breezepro.c
    devices/lacrosse_r1.c
    devices/lacrosse_th3.c
    devices/lacrosse_tx141x.c
    devices/lacrosse_tx31u.c
    devices/lacrosse_tx34.c
    devices/lacrosse_tx35.c
    devices/lacrosse_wr1.c
    devices/lacrosse_ws7000.c
    devices/lacrossews.c
    devices/lightwave_rf.c
    devices/m_bus.c
    devices/markisol.c
    devices/marlec_solar.c
    devices/maverick_et73.c
    devices/maverick_et73x.c
    devices/maverick_xr30.c
    devices/maverick_xr50.c
    devices/mebus.c
    devices/megacode.c
    devices/missil_ml0757.c
    devices/mueller_hotrod.c
    devices/neptune_r900.c
    devices/new_template.c
    devices/newkaku.c
    devices/nexa.c
    devices/nexus.c
    devices/nice_flor_s.c
    devices/norgo.c
    devices/oil_smart.c
    devices/oil_standard.c
    devices/oil_watchman.c
    devices/oil_watchman_advanced.c
    devices/opus_xt300.c
    devices/oregon_scientific.c
    devices/oregon_scientific_sl109h.c
    devices/oregon_scientific_v1.c
    devices/oria_wa150km.c
    devices/philips_aj3650.c
    devices/philips_aj7010.c
    devices/proflame2.c
    devices/prologue.c
    devices/proove.c
    devices/quhwa.c
    devices/quinetic.c
    devices/radiohead_ask.c
    devices/rainpoint.c
    devices/rainpoint_hcs012arf.c
    devices/regency_fan.c
    devices/revolt_nc5462.c
    devices/revolt_zx7717.c
    devices/rftech.c
    devices/risco_agility.c
    devices/rojaflex.c
    devices/rosstech_dcu706.c
    devices/rubicson.c
    devices/rubicson_48659.c
    devices/rubicson_pool_48942.c
    devices/s3318p.c
    devices/schou_72543_rain.c
    devices/schraeder.c
    devices/scmplus.c
    devices/secplus_v1.c
    devices/secplus_v2.c
    devices/sharp_spc775.c
    devices/silvercrest.c
    devices/simplisafe.c
    devices/simplisafe_gen3.c
    devices/smoke_gs558.c
    devices/solight_te44.c
    devices/somfy_iohc.c
    devices/somfy_rts.c
    devices/springfield.c
    devices/srsmith_pool_srs_2c_tx.c
    devices/steelmate.c
    devices/telldus_ft0385r.c
    devices/tfa_14_1504_v2.c
    devices/tfa_30_3196.c
    devices/tfa_30_3221.c
    devices/tfa_drop_30.3233.c
    devices/tfa_marbella.c
    devices/tfa_pool_thermometer.c
    devices/tfa_twin_plus_30.3049.c
    devices/thermopro_tp11.c
    devices/thermopro_tp12.c
    devices/thermopro_tp28b.c
    devices/thermopro_tp82xb.c
    devices/thermopro_tx2.c
    devices/thermopro_tx2c.c
    devices/thermopro_tx7b.c
    devices/thermor.c
    devices/tpms_abarth124.c
    devices/tpms_ave.c
    devices/tpms_bmw.c
    devices/tpms_bmw_g3.c
    devices/tpms_citroen.c
    devices/tpms_eezrv.c
    devices/tpms_elantra2012.c
    devices/tpms_ford.c
    devices/tpms_gm.c
    devices/tpms_hyundai_vdo.c
    devices/tpms_jansite.c
    devices/tpms_jansite_solar.c
    devices/tpms_kia.c
    devices/tpms_nissan.c
    devices/tpms_pmv107j.c
    devices/tpms_porsche.c
    devices/tpms_renault.c
    devices/tpms_renault_0435r.c
    devices/tpms_toyota.c
    devices/tpms_truck.c
    devices/tpms_tyreguard400.c
    devices/ts_ft002.c
    devices/ttx201.c
    devices/universalfanctrl.c
    devices/vaillant_vrt340f.c
    devices/vauno_en8822c.c
    devices/vevor_7in1.c
    devices/visonic_powercode.c
    devices/watts_thermostat.c
    devices/waveman.c
    devices/wec2103.c
    devices/wg_pb12v1.c
    devices/ws2032.c
    devices/wssensor.c
    devices/wt0124.c
    devices/wt450.c
    devices/x10_rf.c
    devices/x10_sec.c
    devices/yale_hsa.c
)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    # untouched upstream code, disable all warnings
    set_source_files_properties(mongoose.c PROPERTIES COMPILE_FLAGS "-w")
endif()

# ISA-specific flags for channelizer hot-path variants
# Runtime dispatch selects the best variant for the detected CPU ISA.
if(ENABLE_NATIVE_OPTIMIZATIONS)
    # All variants use native ISA (fastest, not portable)
    set_source_files_properties(channelizer_sse2.c channelizer_avx2.c channelizer_avx512.c
        channelizer_neon.c channelizer_sve.c
        PROPERTIES COMPILE_FLAGS "${DSP_OPTIMIZE_FLAGS}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    # x86: Runtime dispatch — each variant compiled for its target ISA
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_files_properties(channelizer_sse2.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
        set_source_files_properties(channelizer_avx2.c
            PROPERTIES COMPILE_FLAGS "-mavx2 -mfma -ffast-math")
        set_source_files_properties(channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl -mfma -ffast-math")
        # ARM variant files: compile as baseline (linked but never called on x86)
        set_source_files_properties(channelizer_neon.c channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    elseif(MSVC)
        set_source_files_properties(channelizer_sse2.c
            PROPERTIES COMPILE_FLAGS "/fp:fast")
        set_source_files_properties(channelizer_avx2.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX2 /fp:fast")
        set_source_files_properties(channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "/arch:AVX512 /fp:fast")
        # ARM variant files: compile as baseline (linked but never called on x86)
        set_source_files_properties(channelizer_neon.c channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "/fp:fast")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    # ARM AArch64: NEON is mandatory, SVE is optional
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        # NEON variant: -ffast-math only (NEON auto-vectorized by default on AArch64)
        set_source_files_properties(channelizer_neon.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
        # SVE variant: explicit SVE enable for scalable vector auto-vectorization
        # Apple Clang does not support -march=armv8-a+sve (no SVE on Apple Silicon)
        if(NOT APPLE)
            set_source_files_properties(channelizer_sve.c
                PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve -ffast-math")
        else()
            set_source_files_properties(channelizer_sve.c
                PROPERTIES COMPILE_FLAGS "-ffast-math")
        endif()
        # x86 variant files: compile as baseline (linked but never called on ARM)
        set_source_files_properties(channelizer_sse2.c channelizer_avx2.c channelizer_avx512.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    endif()
else()
    # Non-x86, non-ARM (RISC-V, etc.): all variants get baseline fast-math only
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_files_properties(channelizer_sse2.c channelizer_avx2.c channelizer_avx512.c
            channelizer_neon.c channelizer_sve.c
            PROPERTIES COMPILE_FLAGS "-ffast-math")
    endif()
endif()
# channelizer.c itself: NO -ffast-math (init code has float comparisons)

# cf32_resampler.c: keep DSP optimization flags when native optimizations enabled
if(DSP_OPTIMIZE_FLAGS)
    set_source_files_properties(cf32_resampler.c
        PROPERTIES COMPILE_FLAGS "${DSP_OPTIMIZE_FLAGS}")
endif()

# Link r_433 with hydrasdr_lfft for channelizer FFT operations
target_link_libraries(r_433 hydrasdr_lfft)
target_include_directories(r_433 PUBLIC ${PROJECT_SOURCE_DIR}/external/hydrasdr-lfft)

add_executable(hydrasdr_433 hydrasdr_433.c)
target_link_libraries(hydrasdr_433 r_433)

# target_compile_definitions was only added with CMake 2.8.11
if(THREADS_HAVE_PTHREAD_ARG)
    set_target_properties(r_433 PROPERTIES COMPILE_OPTIONS "-pthread")
    set_target_properties(r_433 PROPERTIES INTERFACE_COMPILE_OPTIONS "-pthread")
endif()
if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(hydrasdr_433 "${CMAKE_THREAD_LIBS_INIT}")
endif()

if(MSVC)
    # needs CMake 3.1 but Windows builds should have that
    target_sources(hydrasdr_433 PRIVATE getopt/getopt.c)
endif()

add_library(data STATIC data.c abuf.c)
target_link_libraries(data ${NET_LIBRARIES})

target_link_libraries(hydrasdr_433
    ${SDR_LIBRARIES}
    ${NET_LIBRARIES}
    hydrasdr_lfft
)

set(INSTALL_TARGETS hydrasdr_433)
if(UNIX)
target_link_libraries(hydrasdr_433 m)
endif()

# Explicitly say that we want C99
set_target_properties(hydrasdr_433 r_433 PROPERTIES C_STANDARD 99)

########################################################################
# Copy runtime DLLs on Windows
########################################################################
if(WIN32)
    # Copy OpenSSL DLLs if using OpenSSL
    if(OPENSSL_FOUND)
        get_filename_component(OPENSSL_BIN_DIR "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
        # Try common DLL names
        foreach(SSL_DLL libcrypto-3-x64.dll libssl-3-x64.dll libcrypto-1_1-x64.dll libssl-1_1-x64.dll)
            if(EXISTS "${OPENSSL_BIN_DIR}/../bin/${SSL_DLL}")
                add_custom_command(TARGET hydrasdr_433 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENSSL_BIN_DIR}/../bin/${SSL_DLL}"
                    "$<TARGET_FILE_DIR:hydrasdr_433>"
                    COMMENT "Copying ${SSL_DLL} to output directory")
            endif()
        endforeach()
    endif()
endif()

########################################################################
# Copy HydraSDR DLLs on Windows
########################################################################
if(WIN32 AND HydraSDR_FOUND)
    if(TARGET hydrasdr)
        # HydraSDR built as subproject via FetchContent - copy DLL from target
        add_custom_command(TARGET hydrasdr_433 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:hydrasdr>"
            "$<TARGET_FILE_DIR:hydrasdr_433>"
            COMMENT "Copying hydrasdr DLL to output directory")
    elseif(HydraSDR_DLL_DIR)
        # Using pre-built HydraSDR - copy from DLL directory
        if(EXISTS "${HydraSDR_DLL_DIR}/libhydrasdr.dll")
            add_custom_command(TARGET hydrasdr_433 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HydraSDR_DLL_DIR}/libhydrasdr.dll"
                "$<TARGET_FILE_DIR:hydrasdr_433>"
                COMMENT "Copying libhydrasdr.dll to output directory")
        elseif(EXISTS "${HydraSDR_DLL_DIR}/hydrasdr.dll")
            add_custom_command(TARGET hydrasdr_433 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HydraSDR_DLL_DIR}/hydrasdr.dll"
                "$<TARGET_FILE_DIR:hydrasdr_433>"
                COMMENT "Copying hydrasdr.dll to output directory")
        endif()
        # Copy libusb-1.0.dll if present
        if(EXISTS "${HydraSDR_DLL_DIR}/libusb-1.0.dll")
            add_custom_command(TARGET hydrasdr_433 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HydraSDR_DLL_DIR}/libusb-1.0.dll"
                "$<TARGET_FILE_DIR:hydrasdr_433>"
                COMMENT "Copying libusb-1.0.dll to output directory")
        endif()
    endif()
endif()

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin              # .dll file
)
