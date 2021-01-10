#ifndef LOG_BACKEND_RB_H_
#define LOG_BACKEND_RB_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MY_LOG_BACKEND_RB_MEM_BASE 0x24070000
#define MY_LOG_BACKEND_RB_MEM_SIZE 65536U
#define MY_LOG_BACKEND_RB_SLOT_SIZE 64U

extern bool log_get_next_line(bool begin, char line[MY_LOG_BACKEND_RB_SLOT_SIZE]);
extern void log_buffer_clear();
extern void log_register_listener(void(*fun_ptr)(const char*));

#ifdef __cplusplus
}
#endif

#endif /* LOG_BACKEND_RB_H_ */