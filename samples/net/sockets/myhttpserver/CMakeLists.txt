# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(civetweb)

set(HTML_FILES buttons.htm switches.htm index.htm)

foreach(HTML_FILE ${HTML_FILES})
add_custom_command(
   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/htm
   COMMAND ./htm2c.sh ${HTML_FILE} > ${HTML_FILE}.c
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}
   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/htm2c.sh
   OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}.c
   COMMENT "Generating code for ${HTML_FILE}."
)
endforeach()

target_sources(app PRIVATE src/main.c src/libc_extensions.c src/cJSON.c src/cJSON_Utils.c src/log_backend_rb.c src/mysettings.c src/mygpio.c 
               htm/buttons.htm.c htm/switches.htm.c htm/index.htm.c)
