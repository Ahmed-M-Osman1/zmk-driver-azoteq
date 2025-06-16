# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

if(CONFIG_IQS5XX)
    zephyr_library()

    zephyr_library_include_directories(include)

    # Include ZMK headers for proper event integration
    if(EXISTS ${ZEPHYR_BASE}/../zmk/app/include)
        zephyr_library_include_directories(${ZEPHYR_BASE}/../zmk/app/include)
    endif()

    zephyr_library_sources(
      src/iqs5xx.c
      src/iqs5xx_regdump.c
      src/trackpad.c
      src/single_finger.c
      src/two_finger.c
      src/three_finger.c
      src/trackpad_keyboard_events.c
    )

    # Link against ZMK if available
    if(TARGET zmk)
        zephyr_library_link_libraries(zmk)
    endif()
endif()
