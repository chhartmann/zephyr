#ifndef MY_GPIO_H_
#define MY_GPIO_H_

#include <zephyr.h>
#include <posix/time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct output_struct {
	const char* json_name;
	const struct device *dev;
	uint8_t index;
	uint8_t value;
	time_t time;
};

#define NUM_OUTPUTS 3


extern void init_outputs();
extern bool set_output_by_name(const char * const json_name, const uint8_t value);
extern struct output_struct const * const get_output(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* MY_GPIO_H_ */