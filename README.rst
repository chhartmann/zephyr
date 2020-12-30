HTTP server for nucleo_h743zi
#############################

Overview
********
This is an extention of the existing webserver example customized for nucleo_h743zi2 board:

* Telnet and serial shell are enabled with GPIO shell and date shell configured.
* The ringbuffer log, which was part in earlier zephyr version, has been adapted. Immediate logging is activated.
* It uses tha last 64Kbyte of the RAM. It can be printed with a shell command similar to dmesg.
* The logging prefix is extended with an absolut timestamp.
* DHCP is enabled. MAC address is currently read from flash. This will change in the future. By default only hardcoded or random MAC address is supported.
* SNTP is used to get the current time. The default code for doing this is executed too early. Therefore a hook has been added where the SNTP routine is called.

This is more a proof of concept than a real project.
Generally it is clearly not a good idea to base the example on a fork of zephyr instead of using a separate repository the example.
There werte some minor changes in some files of zephyr necessary to get this working as it is.

Settings
********
The settings can be changed with a shell command. They are stored in flash at 0x81F0000. The following parameters can be set:

* SNTP server
* Syslogd server (syslogd is not yet activated)
* MAC address

Known Issues
************

* Telnet is not working with windows telnet. Only linux telnet is working at the moment.


Building and Running
********************
Getting started is easiest with gitpod: https://gitpod.io/#https://github.com/chhartmann/zephyr
The gitpod dockerfile was derived from the zephyr dockerfile.
This will install all libraries with west and start the build. A build task is configured for compilation from within the IDE.
The bin-file can be flashed with drag-and-drop with the nucleo_h743zi2 virtual file system of the STLINK V3 on this board.

When developing locally, development with zephyr and west works out-of-the-box

* Build with: west build -b nucleo_h743zi
* Flash with: west flash
* Debug with: west debug
* Debug with: west debugserver

HTTP Connections Points
***********************

* /log - Read current log as text.
* /set - Send HTTP POST to control outputs, e.g. curl -d '{ "led1": 0,"led2": 0,"led3": 1 }' http://192.168.0.133/set
* /get - Get json string with all outputs and their state
* /buttons - Show a html site with buttons to control the outputs. The website uses /set to control the outputs.
