cmake_minimum_required(VERSION 3.10)
project(CoreNG)

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>")

set(SRCS
    "src/SAME5x_C21/Flash.cpp"
    "src/SAME5x_C21/AnalogOut.cpp"
    "src/SAME5x_C21/Interrupts.cpp"
    "src/SAME5x_C21/Serial.cpp"
    "src/SAME5x_C21/AsyncSerial.cpp"
    "src/SAME5x_C21/DmacManager.cpp"
    "src/SAME5x_C21/SAME5x/hpl/nvmctrl/hpl_nvmctrl.c"
    "src/SAME5x_C21/SAME5x/hpl/gmac/hpl_gmac.c"
    "src/SAME5x_C21/SAME5x/hpl/wdt/hpl_wdt.c"
    "src/SAME5x_C21/SAME5x/hpl/core/hpl_core_m4.c"
    "src/SAME5x_C21/SAME5x/hpl/usb/hpl_usb.c"
    "src/SAME5x_C21/SAME5x/Vectors.c"
    "src/SAME5x_C21/SAME5x/hal/utils/src/utils_event.c"
    "src/SAME5x_C21/SAME5x/hal/utils/src/utils_list.c"
    "src/SAME5x_C21/SAME5x/hal/utils/src/utils_assert.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_mac_async.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_atomic.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_gpio.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_sleep.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_flash.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_rand_sync.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_usb_device.c"
    "src/SAME5x_C21/SAME5x/hal/src/hal_wdt.c"
    "src/SAME5x_C21/SAME5x/Sdhc.cpp"
    "src/SAME5x_C21/SAME5x/AnalogIn.cpp"
    "src/SAME5x_C21/SAME5x/usb/device/usbdc.c"
    "src/SAME5x_C21/SAME5x/usb/usb_protocol.c"
    "src/SAME5x_C21/SAME5x/usb/class/cdc/device/cdcdf_acm.c"
    "src/SAME5x_C21/SAME5x/SerialCDC.cpp"
    "src/SAME5x_C21/SAME5x/Startup.cpp"
    "src/Cache.cpp"
    "src/Print.cpp"
    "src/CoreIO.cpp"
    "src/Stream.cpp"
    "src/CanDevice.cpp"
)

set(INCLUDE_DIRS
    "src"
    "src/SAME5x_C21/SAME5x/hal/utils/include"
    "src/arm/CMSIS/5.4.0/CMSIS/Core/Include"
    "src/SAME5x_C21"
    "src/SAME5x_C21/SAME5x"
    "src/SAME5x_C21/SAME5x/hri"
    "src/SAME5x_C21/SAME5x/hal/include"
    "src/SAME5x_C21/SAME5x/Config"
    "${CANlib_DIR}/src"
    "src/arm/CMSIS/5.4.0/CMSIS/Core_A/Include"
)

if (BOARD STREQUAL "Duet3Mini")
    list(APPEND SRCS 
        "src/atmel/SAME54_DFP/1.1.134/gcc/gcc/startup_same54.c"
        "src/atmel/SAME54_DFP/1.1.134/gcc/system_same54.c"
    )
    list(APPEND INCLUDE_DIRS
        "src/atmel/SAME54_DFP/1.1.134/include"
    )
elseif (BOARD STREQUAL "LunaboardMCU")
    list(APPEND SRCS 
        "src/atmel/SAME51_DFP/1.1.139/gcc/gcc/startup_same51.c"
        "src/atmel/SAME51_DFP/1.1.139/gcc/system_same51.c"
    )
    list(APPEND INCLUDE_DIRS
        "src/atmel/SAME51_DFP/1.1.139/include"
    )
endif()

add_library(CoreNG ${SRCS})
target_include_directories(CoreNG PUBLIC ${INCLUDE_DIRS})

target_link_libraries(CoreNG INTERFACE "-T ${CMAKE_SOURCE_DIR}/src/Hardware/SAME5x/same54p20a_flash_16k_bootloader.ld")

target_compile_definitions(CoreNG PRIVATE "-DUDD_ENABLE" "-DUDD_NO_SLEEP_MGR")
target_link_libraries(CoreNG PUBLIC RRFLibraries FreeRTOS)