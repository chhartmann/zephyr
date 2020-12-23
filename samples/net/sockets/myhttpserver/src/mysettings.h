#ifndef MY_SETTINGS_H_
#define MY_SETTINGS_H_

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mysettings {
   uint32_t magic;
   uint32_t flash_counter;
   char sntp_server[64];
   char syslog_server[64];
   uint8_t mac_address[6];
};

struct mysettings const * const get_settings();

#ifdef __cplusplus
}
#endif

#endif /* MY_SETTINGS_H_ */