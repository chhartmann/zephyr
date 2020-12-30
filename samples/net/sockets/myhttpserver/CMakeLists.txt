# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(civetweb)

add_custom_command(
   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/htm
   COMMAND ./htm2c.sh buttons.htm > buttons.c
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/buttons.htm
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/htm2c.sh
   OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/htm/buttons.c
   COMMENT "Generating code for buttons.htm."
)

target_sources(app PRIVATE src/main.c src/libc_extensions.c src/cJSON.c src/cJSON_Utils.c src/log_backend_rb.c src/mysettings.c src/mygpio.c htm/buttons.c)
