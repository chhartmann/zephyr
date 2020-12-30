#include "mygpio.h"
#include <drivers/gpio.h>
#include <logging/log.h>
LOG_MODULE_DECLARE(myhttpserver, LOG_LEVEL_DBG);

struct output_struct outputs[NUM_OUTPUTS] = {0};

void set_output(uint32_t index, uint8_t value) {
	__ASSERT(index < NUM_OUTPUTS, "invalid index for set_output");
	if (gpio_pin_set(outputs[index].dev, outputs[index].index, value)) {
		LOG_ERR("Failed to set output\n");
	}
	outputs[index].value = value;
}

void setup_output(const uint32_t index, 
                  const char * const json_name, 
						const char * const port, 
						const uint8_t pin_index, 
						const uint8_t value) {
	if (index < NUM_OUTPUTS) {
		outputs[index].json_name = json_name;
		outputs[index].dev = device_get_binding(port);
		outputs[index].index = pin_index;
		outputs[index].value = value;
		outputs[index].value = value;

		if (outputs[index].dev == NULL) {
			LOG_ERR("Failed to get device for %s\n", outputs[index].json_name);
		} else if (0 != gpio_pin_configure(outputs[index].dev, outputs[index].index, GPIO_OUTPUT)) {
			LOG_ERR("Failed to configure output\n");
		} else {
			set_output(index, value);
		}
		
		 if (gpio_pin_set(outputs[index].dev, outputs[index].index, outputs[index].value)) {
			LOG_ERR("Failed to set output\n");
		}
	} else {
		LOG_ERR("Invalid index for output: %s\n", json_name);
	}
}

void init_outputs() {
	LOG_INF("Initializing GPIOs");
	uint32_t i = 0;
	setup_output(i++, "led1", "GPIOB", 0, 0);
	setup_output(i++, "led2", "GPIOE", 1, 0);
	setup_output(i++, "led3", "GPIOB", 14, 0);
	__ASSERT(i == NUM_OUTPUTS, "Setup %d outputs instead of %d\n", i, NUM_OUTPUTS);
}

struct output_struct const * const get_output(uint32_t index) {
   return &outputs[index];
}

bool set_output_by_name(const char * const json_name, const uint8_t value) {
	bool found = false;
	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		if (0 == strcmp(outputs[i].json_name, json_name)) {
			set_output(i, value);
			break;
		}
	}
	return found;
}
