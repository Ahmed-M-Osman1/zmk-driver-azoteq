# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources(src/iqs5xx.c)
zephyr_library_sources(src/iqs5xx_regdump.c)
target_include_directories(zmk-driver-azoteq PRIVATE include)
