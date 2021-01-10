/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log_backend.h>
#include <logging/log_ctrl.h>
#include <logging/log_msg.h>
#include <sys/byteorder.h>
#include <sys/ring_buffer.h>
#include <shell/shell.h>
#include <time.h>
#include <stdio.h>

#include "my_log_backend.h"

static struct ring_buf ringbuf;

/*
 * Log message format:
 * Logging started with magic number 0x55aa followed by log message id.
 * Log message ended with null terminator and takes
 * CONFIG_LOG_BACKEND_RB_SLOT_SIZE slot. The long log message can occupy
 * several logging slots.
 */

/*
 * All log messages are split to similar sized logging slots. Since ring
 * buffer slots get rewritten we need to check that all slots are fit to
 * the ring buffer
 */
BUILD_ASSERT(MY_LOG_BACKEND_RB_MEM_SIZE % MY_LOG_BACKEND_RB_SLOT_SIZE == 0);

static void(*listener_callback)(const char*) = NULL;

void log_register_listener(void(*fun_ptr)(const char*)) {
	listener_callback = fun_ptr;
}

static void init(void)
{
	ring_buf_init(&ringbuf, MY_LOG_BACKEND_RB_MEM_SIZE,
		      (void *)MY_LOG_BACKEND_RB_MEM_BASE);
}

static void trace(const uint8_t *data, size_t length)
{
	const uint16_t magic = 0x55aa;
	static uint16_t log_id;
	volatile uint8_t *t, *region;
	int space;

	space = ring_buf_space_get(&ringbuf);
	if (space < MY_LOG_BACKEND_RB_SLOT_SIZE) {
		uint8_t *dummy;

		/* Remove oldest entry */
		ring_buf_get_claim(&ringbuf, &dummy,
				   MY_LOG_BACKEND_RB_SLOT_SIZE);
		ring_buf_get_finish(&ringbuf, MY_LOG_BACKEND_RB_SLOT_SIZE);
	}

	ring_buf_put_claim(&ringbuf, (uint8_t **)&t,
			   MY_LOG_BACKEND_RB_SLOT_SIZE);
	region = t;

	/* Add magic number at the beginning of the slot */
	sys_put_le16(magic, (uint8_t *)t);
	t += 2;

	/* Add log id */
	sys_put_le16(log_id++, (uint8_t *)t);
	t += 2;

	length = MIN(length, MY_LOG_BACKEND_RB_SLOT_SIZE - 4);

	memcpy((void *)t, data, length);
	t += length;

	memset((void *)t, 0, MY_LOG_BACKEND_RB_SLOT_SIZE - 4 - length);

	ring_buf_put_finish(&ringbuf, MY_LOG_BACKEND_RB_SLOT_SIZE);
}

static const char *const severity[] = {
	NULL,
	"err",
	"wrn",
	"inf",
	"dbg"
};

static int insert_timestamp(char* buffer, uint32_t timestamp)
{
	int length = 0;

	struct timespec tp;
	int res = clock_gettime(CLOCK_REALTIME, &tp);
	if (res == 0) {
		uint32_t act_timestamp;
		uint32_t act_freq;
		uint32_t timestamp_diff;
		uint32_t ns_delta;
		uint32_t s_delta;
		struct tm tm;

		// correct difference of timestamp from log and current time
		if (sys_clock_hw_cycles_per_sec() > 1000000) {
			act_timestamp = k_uptime_get_32();
			act_freq = 1000;
		} else {
			act_timestamp = k_cycle_get_32();
			act_freq = sys_clock_hw_cycles_per_sec();
		}

		// remainder = timestamp % freq;
		// ms = (remainder * 1000U) / freq;
		// us = (1000 * (remainder * 1000U - (ms * freq))) / freq;

		timestamp_diff = (act_timestamp - timestamp);
		ns_delta = (((timestamp_diff % act_freq) * 1000U) / act_freq) * 1000000U;
		s_delta = timestamp_diff / act_freq;
		tp.tv_sec -= s_delta;

		if (tp.tv_nsec + 1000000000U - ns_delta >= 1000000000U) {
			tp.tv_nsec -= ns_delta;
		} else {
			tp.tv_nsec += (1000000000U - ns_delta);
			tp.tv_sec -= 1U;
		}

		gmtime_r(&tp.tv_sec, &tm);
		length = sprintf(buffer,
			"%d-%02u-%02u %02u:%02u:%02u:%03u ",
		    tm.tm_year + 1900,
		    tm.tm_mon + 1,
		    tm.tm_mday,
		    tm.tm_hour,
		    tm.tm_min,
		    tm.tm_sec,
			 (unsigned int)(tp.tv_nsec / 1000000U));

	}
	return length;
}

static void sync_string(const struct log_backend *const backend,
			struct log_msg_ids src_level, uint32_t timestamp,
			const char *fmt, va_list ap)
{
	char buf[256];
	int index = 0;

	index += insert_timestamp(buf, timestamp);
	index += snprintf(buf + index, sizeof(buf) - index, "<%s> %s: ", 
		severity[src_level.level], log_source_name_get(src_level.domain_id, src_level.source_id));
	index += vsnprintf(buf + index, sizeof(buf) - index, fmt, ap);

	index = MIN(sizeof(buf) - 2, index);
	buf[index]='\n';
	buf[index+1]='\0';

	// add line to ramlog
	for (uint32_t i = 0; i < strlen(buf) + 1; i += (MY_LOG_BACKEND_RB_SLOT_SIZE - 4)) {
		trace(buf + i,  MIN((strlen(buf) + 1 - i), (MY_LOG_BACKEND_RB_SLOT_SIZE - 4)));
	}

	// send line to websocket 
	if (listener_callback) {
		listener_callback(buf);
	}
}

const struct log_backend_api my_log_backend_api = {
	.put = NULL,
	.put_sync_string = sync_string,
	.put_sync_hexdump = NULL,
	.panic = NULL,
	.init = init,
	.dropped =  NULL,
};

LOG_BACKEND_DEFINE(my_log_backend, my_log_backend_api, true);

// interface
bool log_get_next_line(bool begin, char line[MY_LOG_BACKEND_RB_SLOT_SIZE]) {
	static uint32_t index = 0; // not thread safe - when used in parallel, not all lines will be shown
	bool end = false;
	if (begin) {
		index = ringbuf.head;
	}
	if (index != ringbuf.tail) {
		memcpy(line, &ringbuf.buf.buf8[(index & ringbuf.mask) + 4], MY_LOG_BACKEND_RB_SLOT_SIZE - 4);
		line[MY_LOG_BACKEND_RB_SLOT_SIZE - 4] = '\0';
		index += MY_LOG_BACKEND_RB_SLOT_SIZE;
	} else {
		end = true;
	};
	return end;
}

void log_buffer_clear() {
	ring_buf_reset(&ringbuf);
}

// shell commands

static int cmd_show(const struct shell *shell, size_t argc, char **argv) {
	char line[MY_LOG_BACKEND_RB_SLOT_SIZE];

	for (bool end = log_get_next_line(true, line); !end; end = log_get_next_line(false, line)) {
		shell_fprintf(shell, SHELL_NORMAL, "%s", line);
	}

	return 0;
}

static int cmd_clear(const struct shell *shell, size_t argc, char **argv) {
	log_buffer_clear();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ramlog,
	SHELL_CMD(show, NULL, "Show whole log", cmd_show),
	SHELL_CMD(clear, NULL, "Clear whole log", cmd_clear),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ramlog, &sub_ramlog, "RAMlog commands", NULL);
