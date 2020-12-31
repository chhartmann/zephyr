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
   uint8_t default_value;
	int64_t timer;
};

#define NUM_OUTPUTS 3


extern void init_outputs();
extern void check_output_timer();
extern void set_output(uint32_t index, uint8_t value, int64_t timer);
extern bool set_output_by_name(const char * const json_name, const uint8_t value, int64_t timer);
extern int32_t get_output_by_name(const char * const json_name);
extern struct output_struct const * const get_output(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* MY_GPIO_H_ */