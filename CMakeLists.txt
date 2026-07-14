cmake_minimum_required(VERSION 3.13)


# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")



# Detect if the active kit is an ARM cross-compiler
if(CMAKE_CXX_COMPILER MATCHES "arm-none-eabi")
    message(STATUS "Detected that the current kit is a cross-compiler.")
    set(CrossCompiling 1)
else()
    message(STATUS "Detected that the current kit is a host compiler. Building the test harness.")
endif()

# Detect if the active kit is an ARM cross-compiler
if(CrossCompiling)
    # Yes, build for the RP2040

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    # Specify an increased oscillator startup delay to work around a potential hardware issue where the 
    # clock is slow to boot.
    add_compile_definitions(PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

    # Pull in Raspberry Pi Pico SDK (must be before project)
    include(pico_sdk_import.cmake)

    project(CC3501-A2-Connor-Mitchell-Liam C CXX ASM)
    
    # Initialise the Raspberry Pi Pico SDK
    pico_sdk_init()

    add_executable(CC3501_A2_CML)
    target_sources(CC3501_A2_CML 
        PUBLIC
        src/main.cpp
        src/drivers/logging/logging.cpp
        
    )
    target_include_directories(CC3501_A2_CML
        PUBLIC 
        src/
    )

    pico_set_program_name(CC3501_A2_CML "CC3501-A2-Connor-Mitchell-Liam")
    pico_set_program_version(CC3501_A2_CML "0.2")

    # select UART for standard IO
    pico_enable_stdio_uart(CC3501_A2_CML 1)
    pico_enable_stdio_usb(CC3501_A2_CML 0)

    # compile the PIO file
    pico_generate_pio_header(CC3501_A2_CML ${CMAKE_CURRENT_LIST_DIR}/src/drivers/WS2812/WS2812.pio)

    # Add the standard library to the build
    target_link_libraries(CC3501_A2_CML
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_timer
        hardware_watchdog
        hardware_clocks
        hardware_pwm
    )

    pico_add_extra_outputs(CC3501_A2_CML)

else()

    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 20)

    # We are building natively, so create the test harness instead
    project(CC3501-A2-Connor-Mitchell-Liam CXX)

    add_executable(CC3501_A2_CML)
    target_sources(CC3501_A2_CML 
        PUBLIC
        src/main.cpp
        src/drivers/logging/logging.cpp
        src/drivers/HTU21D/HTU21D.cpp
        src/drivers/BH1750FVI-TR/BH1750FVI-TR.cpp
        src/drivers/TF-015/TF-015.cpp
        src/drivers/RN4871/RN4871.cpp
        src/drivers/soil_moisture/soil_moisture.cpp
        tests/mocks/pico/stdlib.cpp
        tests/mocks/pico/time.cpp
        tests/mocks/hardware/gpio.cpp
        tests/mocks/hardware/pio.cpp
        tests/mocks/ws2812.cpp
    )
    target_include_directories(CC3501_A2_CML
        PUBLIC 
        src/
        tests/
        tests/mocks/
    )
    target_compile_definitions(CC3501_A2_CML 
        PUBLIC
        TEST_HARNESS=1
    )

endif()

target_compile_definitions(CC3501_A2_CML 
    PUBLIC
    LOG_DRIVER_STYLE=${LogDriverImplementation}
)