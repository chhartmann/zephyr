# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(civetweb)

set(HTML_DYNAMIC_FILES buttons.htm switches.htm inputs.htm)
set(HTML_STATIC_FILES webshell.htm index.htm bootstrap.bundle.min.js bootstrap.min.css jquery-3.3.1.min.js)

target_sources(app PRIVATE src/main.c src/log_backend_rb.c src/mysettings.c src/mygpio.c src/web_shell.c
               src/libc_extensions.c src/cJSON.c src/cJSON_Utils.c)

foreach(HTML_FILE ${HTML_STATIC_FILES})
	generate_inc_file_for_target(app
		${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}
		${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}.gz.inc
      --gzip)
endforeach()

foreach(HTML_FILE ${HTML_DYNAMIC_FILES})
   add_custom_command(
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/htm
      COMMAND ./htm2c.sh ${HTML_FILE} > ${HTML_FILE}.c
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/htm/htm2c.sh
      OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/htm/${HTML_FILE}.c
      COMMENT "Generating code for ${HTML_FILE}."
   )
   target_sources(app PRIVATE htm/${HTML_FILE}.c)
endforeach()
