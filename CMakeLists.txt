# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

if(CONFIG_IQS5XX)
    zephyr_library()

    zephyr_library_include_directories(include)

    zephyr_library_sources(
      src/iqs5xx.c
      src/iqs5xx_regdump.c
      src/trackpad.c  # Add this line
    )
endif()
