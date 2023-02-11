#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set(OVERLAY_CONFIG "secret.conf")

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C VERSION 1.0.0)

configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)
target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/app/include src)

target_sources(app PRIVATE src/mqtt.c)
target_sources(app PRIVATE src/dns_resolve.c)
target_sources(app PRIVATE src/openthread.c)
target_sources(app PRIVATE src/main.c)

zephyr_include_directories(
  configuration/${BOARD}
  )
