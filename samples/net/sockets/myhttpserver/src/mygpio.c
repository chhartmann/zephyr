#include "mygpio.h"
#include <drivers/gpio.h>
#include <logging/log.h>
LOG_MODULE_DECLARE(myhttpserver, LOG_LEVEL_DBG);

struct output_struct outputs[NUM_OUTPUTS] = {0};
struct input_struct inputs[NUM_INPUTS] = {0};

static struct gpio_callback input_cb_data;

static void(*listener_callback)(const char*, uint8_t) = NULL;

void mygpio_register_listener(void(*fun_ptr)(const char*, uint8_t)) {
	listener_callback = fun_ptr;
}

static void gpio_listener(const char* json_name, uint8_t val) {
	if (listener_callback) {
		listener_callback(json_name, val);
	}
}

void set_output(uint32_t index, uint8_t value, int64_t timer) {
	__ASSERT(index < NUM_OUTPUTS, "invalid index for set_output");
	if (gpio_pin_set(outputs[index].dev, outputs[index].index, value)) {
		LOG_ERR("Failed to set output\n");
	}
	outputs[index].value = value;
	outputs[index].timer = timer > 0 ? k_uptime_get() + timer : 0;
	gpio_listener(outputs[index].json_name, value);
}

bool get_input_state(uint32_t index) {
	__ASSERT(index < NUM_INPUTS, "invalid index for get_input_state");
	return gpio_pin_get(get_input(index)->dev, get_input(index)->index);
}

void input_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	for (uint32_t i = 0; i < NUM_INPUTS; i++) {
		if (get_input(i)->dev == dev && (pins & BIT(get_input(i)->index))) {
			gpio_listener(get_input(i)->json_name, get_input_state(i));
		}
	}
}

void setup_input(uint32_t index, const char * const json_name, const char * const port, const uint8_t pin_index) {
	if (index < NUM_INPUTS) {
		inputs[index].json_name = json_name;
		inputs[index].dev = device_get_binding(port);
		inputs[index].index = pin_index;

		if (inputs[index].dev == NULL) {
			LOG_ERR("Failed to get device for %s\n", inputs[index].json_name);
		} else if (0 != gpio_pin_configure(inputs[index].dev, inputs[index].index, GPIO_INPUT)) {
			LOG_ERR("Failed to configure input\n");
		} else if (0 != gpio_pin_interrupt_configure(inputs[index].dev, pin_index, GPIO_INT_EDGE_BOTH))
			LOG_ERR("Failed to enable interrupt for input\n");	
		} else {
		LOG_ERR("Invalid index for input: %s\n", json_name);
	}
}

void setup_output(const uint32_t index, 
                  const char * const json_name, 
						const char * const port, 
						const uint8_t pin_index, 
						const uint8_t value,
						const int64_t timer) {
	if (index < NUM_OUTPUTS) {
		outputs[index].json_name = json_name;
		outputs[index].dev = device_get_binding(port);
		outputs[index].index = pin_index;
		outputs[index].default_value = value; // the value for setup should be also the default value

		if (outputs[index].dev == NULL) {
			LOG_ERR("Failed to get device for %s\n", outputs[index].json_name);
		} else if (0 != gpio_pin_configure(outputs[index].dev, outputs[index].index, GPIO_OUTPUT)) {
			LOG_ERR("Failed to configure output\n");
		} else {
			set_output(index, value, timer);
		}
		
		 if (gpio_pin_set(outputs[index].dev, outputs[index].index, outputs[index].value)) {
			LOG_ERR("Failed to set output\n");
		}
	} else {
		LOG_ERR("Invalid index for output: %s\n", json_name);
	}
}

void check_output_timer() {
	int64_t now = k_uptime_get();
	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		if ((outputs[i].timer > 0) && (now >= outputs[i].timer)) {
			set_output(i, !outputs[i].value, 0);
		}
	}
}

void init_gpios() {
	LOG_INF("Initializing GPIOs");
	uint32_t i = 0;
	setup_output(i++, "green led", "GPIOB", 0, 0, 0);
	setup_output(i++, "orange led", "GPIOE", 1, 0, 0);
	setup_output(i++, "red led", "GPIOB", 14, 0, 0);
	__ASSERT(i == NUM_OUTPUTS, "Setup %d outputs instead of %d\n", i, NUM_OUTPUTS);

	i = 0;
	setup_input(i++, "button", "GPIOC", 13);
	__ASSERT(i == NUM_OUTPUTS, "Setup %d inputs instead of %d\n", i, NUM_INPUTS);

	//TODO make this more generic - not sure, if one input_cb_data per port is needed
	// foreach port get relevant inputs and generate mask and init callback
	gpio_init_callback(&input_cb_data, input_callback, -1);
	gpio_add_callback(inputs[0].dev, &input_cb_data);
}

struct output_struct const * const get_output(uint32_t index) {
   return &outputs[index];
}

struct input_struct const * const get_input(uint32_t index) {
   return &inputs[index];
}

int32_t get_output_by_name(const char * const json_name) {
	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		if (0 == strcmp(outputs[i].json_name, json_name)) {
			return i;
		}
	}
	return -1;
}

bool set_output_by_name(const char * const json_name, const uint8_t value, int64_t timer) {
	int32_t index = get_output_by_name(json_name);
	if (index >= 0) {
		set_output(index, value, timer);
	}
	return index >= 0;
}
