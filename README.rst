HTTP server for nucleo_h743zi


Overview
********
This is an extention of the existing webserver example customized for nucleo_h743zi2 board:

* Telnet and serial shell are enabled with GPIO shell and date shell configured.
* A ram-log-ringbuffer is used. It can be printed with a shell command similar to dmesg.
* DHCP is enabled. MAC address is currently read from flash. This will change in the future. By default only hardcoded or random MAC address is supported.
* SNTP is used to get the current time. The default code for doing this is executed too early. Therefore a hook has been added where the SNTP routine is called.

This is more a proof of concept than a real project.
Generally it is clearly not a good idea to base the example on a fork of zephyr instead of using a separate repository the example.
There werte some minor changes in some files of zephyr necessary to get this working as it is.

Settings
********
The settings can be changed with a shell command. They are stored in flash at 0x81F0000. The following parameters can be set:

* SNTP server
* MAC address

When settings have been changed, a reboot is required. This can be accomplished with the shell command 'reboot'.

Known Issues
************

* Telnet is not working with windows telnet. Only linux telnet is working at the moment.
* When a telnet connection is interrupted during logging the system crashes. When telnet connection is closed with '^]', everything is fine.

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

* /index - HTML start page with links to all other pages.
* /set_outputs - Send HTTP POST to control outputs with an optional delay value meaning the outputs are toggled back after the delay time.  e.g. curl -d '{ "delay":2000, "led1":0, "led3":1 }' http://192.168.0.133/set
* /set_default - Send HTTP POST to reset all outputs. Used by website /switches
* /get_outputs - Get json string with all outputs and their state
* /get_inputs - Get json string with all inputs and their state
* /ws_gpio_status - Receive changes of inputs or outputs over websocket
* /ws_log - Receive logging output
