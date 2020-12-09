HTTP server for nucleo_h743zi
#############################

Overview
********
This is an extention of the existing webserver example for nucleo_h743zi board:

* Telnet and serial shell are enabled with GPIO shell and date shell configured.
* The ringbuffer log, which was part in earlier zephyr version, has been adapted.
* It uses tha last 64Kbyte of the RAM. It can be printed with a shell command similar to dmesg.
* The logging prefix is extended with an absolut timestamp.
* DHCP is enabled. MAC address is currently read from flash. This will change in the future. By default only hardcoded or random MAC address is supported.
* SNTP is used to get the current time. The default code for doing this is executed too early. Therefore a hook has been added where the SNTP routine is called.

This is more a proof of concept than a real project.
Generally it is clearly not a good idea to base the example on a fork of zephyr instead of using a separate repository the example.


Requirements
************
The MAC address cat be set with STM32CubeProgrammer at 0x8100000.
The software can be flashed with STM32CubeProgrammer or directly with the virtual file system of ST-LINK V3 attached to the nucleo_h743zi2 board.

Building and Running
********************
Getting started is easiest with gitpod: https://gitpod.io/#https://github.com/chhartmann/zephyr
This will install all libraries with west and start the build. A build task is configured for compilation from within the IDE.
When developing locally, build and flash can be done with "west flash --runner stm32cubeprogrammer"
