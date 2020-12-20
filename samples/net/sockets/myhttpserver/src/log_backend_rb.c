/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log_backend.h>
#include <logging/log_core.h>
#include <logging/log_msg.h>
#include <logging/log_output.h>
#include <sys/byteorder.h>
#include <sys/ring_buffer.h>
#include <shell/shell.h>

#include "log_backend_rb.h"

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
BUILD_ASSERT(CONFIG_LOG_BACKEND_RB_MEM_SIZE %
	     CONFIG_LOG_BACKEND_RB_SLOT_SIZE == 0);

static void init(void)
{
	ring_buf_init(&ringbuf, CONFIG_LOG_BACKEND_RB_MEM_SIZE,
		      (void *)CONFIG_LOG_BACKEND_RB_MEM_BASE);
}

static void trace(const uint8_t *data, size_t length)
{
	const uint16_t magic = 0x55aa;
	static uint16_t log_id;
	volatile uint8_t *t, *region;
	int space;

	space = ring_buf_space_get(&ringbuf);
	if (space < CONFIG_LOG_BACKEND_RB_SLOT_SIZE) {
		uint8_t *dummy;

		/* Remove oldest entry */
		ring_buf_get_claim(&ringbuf, &dummy,
				   CONFIG_LOG_BACKEND_RB_SLOT_SIZE);
		ring_buf_get_finish(&ringbuf, CONFIG_LOG_BACKEND_RB_SLOT_SIZE);
	}

	ring_buf_put_claim(&ringbuf, (uint8_t **)&t,
			   CONFIG_LOG_BACKEND_RB_SLOT_SIZE);
	region = t;

	/* Add magic number at the beginning of the slot */
	sys_put_le16(magic, (uint8_t *)t);
	t += 2;

	/* Add log id */
	sys_put_le16(log_id++, (uint8_t *)t);
	t += 2;

	length = MIN(length, CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4);

	memcpy((void *)t, data, length);
	t += length;

	memset((void *)t, 0,
		       CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4 - length);

	ring_buf_put_finish(&ringbuf, CONFIG_LOG_BACKEND_RB_SLOT_SIZE);
}

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	static uint32_t index = 0;
	static uint8_t immediate_buffer[CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4];

	if (length == 0) {
		trace(immediate_buffer, index);
		index = 0;
	} else if (length == 1) {
		immediate_buffer[index++] = *data;
		if (index == sizeof(immediate_buffer) - 1) {
			trace(immediate_buffer, index);
			index = 0;
		}
	} else {
		trace(data, length);
		index = 0;		
	}

	return length;
}

/* magic and log id takes space */
static uint8_t rb_log_buf[CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4];

LOG_OUTPUT_DEFINE(log_output_rb, char_out, rb_log_buf, sizeof(rb_log_buf));

static void put(const struct log_backend *const backend,
		struct log_msg *msg)
{
	uint32_t flags = LOG_OUTPUT_FLAG_LEVEL;

	log_msg_get(msg);

	flags |= LOG_OUTPUT_FLAG_TIMESTAMP;
	flags |= LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP;

	log_output_msg_process(&log_output_rb, msg, flags);

	log_msg_put(msg);
}

static void panic(struct log_backend const *const backend)
{
	log_output_flush(&log_output_rb);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	log_output_dropped_process(&log_output_rb, cnt);
}

static void sync_string(const struct log_backend *const backend,
			struct log_msg_ids src_level, uint32_t timestamp,
			const char *fmt, va_list ap)
{
	uint32_t flags = LOG_OUTPUT_FLAG_LEVEL;
	uint32_t key;

	flags |= LOG_OUTPUT_FLAG_TIMESTAMP;
	flags |= LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP;

	key = irq_lock();
	log_output_string(&log_output_rb, src_level,
			  timestamp, fmt, ap, flags);
	irq_unlock(key);
}

static void sync_hexdump(const struct log_backend *const backend,
			 struct log_msg_ids src_level, uint32_t timestamp,
			 const char *metadata, const uint8_t *data, uint32_t length)
{
	uint32_t flags = LOG_OUTPUT_FLAG_LEVEL;
	uint32_t key;

	flags |= LOG_OUTPUT_FLAG_TIMESTAMP;
	flags |= LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP;

	key = irq_lock();
	log_output_hexdump(&log_output_rb, src_level, timestamp,
			   metadata, data, length, flags);
	irq_unlock(key);
}

const struct log_backend_api log_backend_rb_api = {
	.put = IS_ENABLED(CONFIG_LOG_IMMEDIATE) ? NULL : put,
	.put_sync_string = IS_ENABLED(CONFIG_LOG_IMMEDIATE) ?
			sync_string : NULL,
	.put_sync_hexdump = IS_ENABLED(CONFIG_LOG_IMMEDIATE) ?
			sync_hexdump : NULL,
	.panic = panic,
	.init = init,
	.dropped = IS_ENABLED(CONFIG_LOG_IMMEDIATE) ? NULL : dropped,
};

LOG_BACKEND_DEFINE(log_backend_rb, log_backend_rb_api, true);

// interface
bool log_get_next_line(bool begin, char line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE]) {
	static uint32_t index = 0; // not thread safe - when used in parallel, not all lines will be shown
	bool end = false;
	if (begin) {
		index = ringbuf.head;
	}
	if (index != ringbuf.tail) {
		memcpy(line, &ringbuf.buf.buf8[(index & ringbuf.mask) + 4], CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4);
		line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE - 4] = '\0';
		index += CONFIG_LOG_BACKEND_RB_SLOT_SIZE;
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
	char line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE];

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
	SHELL_CMD(show, NULL, "Show whole log\n", cmd_show),
	SHELL_CMD(clear, NULL, "Clear whole log\n", cmd_clear),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ramlog, &sub_ramlog, "RAMlog commands", NULL);
