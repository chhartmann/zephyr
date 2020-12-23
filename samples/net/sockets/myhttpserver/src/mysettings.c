#include <shell/shell.h>
#include <drivers/flash.h>
#include <stdlib.h>
#include "mysettings.h"

#define FLASH_START_ADDR 0x8000000 // TODO read from devicetree
#define MY_SETTINGS_FLASH_OFFSET 0x1F0000u
#define MY_SETTINGS_MAGIC_ID 0x487F934Au


// this is not a random mac address, but is used to "overload" setting of the mac address
void gen_random_mac(uint8_t *mac_addr, uint8_t b0, uint8_t b1, uint8_t b2)
{
	struct mysettings const * const cfg = get_settings();
	for (uint32_t i = 0; i < 6; i++) {
		mac_addr[i] = cfg->mac_address[i];
	}
}

static struct mysettings default_settings = {
   .magic = MY_SETTINGS_MAGIC_ID,
   .flash_counter = 1,
   .sntp_server = "192.168.0.1",
   .syslog_server = "",
   .mac_address = {0x00,0x80,0xE1,0x04,0x05,0x06}
};


struct mysettings const * const get_settings() {
   struct mysettings const * const cfg = (struct mysettings*)(FLASH_START_ADDR + MY_SETTINGS_FLASH_OFFSET);
   return cfg->magic == MY_SETTINGS_MAGIC_ID ? cfg : &default_settings;
}

static int store_settings(const struct shell *shell, struct mysettings const * const p_cfg) {
	const struct device *flash_dev;
   const struct mysettings cfg = *p_cfg;

	flash_dev = device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
	if (!flash_dev) {
		shell_error(shell, "Flash driver was not found!");
		return -ENODEV;
	}

	flash_write_protection_set(flash_dev, false);

	if (flash_erase(flash_dev, MY_SETTINGS_FLASH_OFFSET, sizeof(cfg))) {
		shell_error(shell, "Erase internal ERROR!");
      return -EIO;
	}

	if (flash_write(flash_dev, MY_SETTINGS_FLASH_OFFSET, &cfg, sizeof(cfg)) != 0) {
		shell_error(shell, "Write internal ERROR!");
		return -EIO;
	}

	flash_write_protection_set(flash_dev, true);

	return 0;
}

static int cmd_show(const struct shell *shell, size_t argc, char **argv) {
   struct mysettings const * const cfg = get_settings();

	shell_fprintf(shell, SHELL_NORMAL, "SNTP server: %s\n", cfg->sntp_server);
	shell_fprintf(shell, SHELL_NORMAL, "Syslog server: %s\n", cfg->syslog_server);
	shell_fprintf(shell, SHELL_NORMAL, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
      cfg->mac_address[0], cfg->mac_address[1], cfg->mac_address[2], cfg->mac_address[3], cfg->mac_address[4], cfg->mac_address[5]);
	return 0;
}

static int cmd_sntp(const struct shell *shell, size_t argc, char **argv) {
	int ret_val = 0;
   if ((argc == 2) && (strlen(argv[1]) < (sizeof(default_settings.sntp_server) - 1))) {
      struct mysettings cfg = *get_settings();
      strcpy(cfg.sntp_server, argv[1]);
      ret_val = store_settings(shell, &cfg);
   } else {
   	shell_error(shell, "invalid syntax: settings sntp <servername>\n");
   }
   return ret_val;
}

static int cmd_syslog(const struct shell *shell, size_t argc, char **argv) {
	int ret_val = 0;
   if ((argc == 2) && (strlen(argv[1]) < (sizeof(default_settings.syslog_server) - 1))) {
      struct mysettings cfg = *get_settings();
      strcpy(cfg.syslog_server, argv[1]);
      ret_val = store_settings(shell, &cfg);
   } else {
   	shell_error(shell, "invalid syntax: settings syslog <servername>\n");
   }
   return ret_val;
}

static int cmd_mac(const struct shell *shell, size_t argc, char **argv) {
	int ret_val = 0;
   if ((argc == 2) && (strlen(argv[1]) == (6*2+5) 
			&& argv[1][2] == ':' && argv[1][5] == ':' && argv[1][8] == ':' && argv[1][11] == ':' && argv[1][14] == ':' )) {
      struct mysettings cfg = *get_settings();
		char* startptr = argv[1], *endptr;
		for (uint32_t i = 0; i < 6; i++) {
			cfg.mac_address[i] = (uint8_t)strtoul(startptr, &endptr, 16);
			startptr = endptr + 1;
		}
      ret_val = store_settings(shell, &cfg);
   } else {
   	shell_error(shell, "invalid syntax: settings mac <xx:xx:xx:xx:xx:xx>\n");
   }
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_settings,
	SHELL_CMD(show, NULL, "Show all settings", cmd_show),
	SHELL_CMD(sntp, NULL, "Set SNTP server", cmd_sntp),
	SHELL_CMD(syslog, NULL, "Set SNTP server", cmd_syslog),
	SHELL_CMD(mac, NULL, "Set SNTP server", cmd_mac),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(settings, &sub_settings, "Setting commands", NULL);
