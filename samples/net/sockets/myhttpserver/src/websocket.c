/*
 * Copyright (c) 2020 Alexander Kozhinov <AlexanderKozhinov@yandex.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#include <logging/log_backend.h>
#include <logging/log_output.h>
#include <kernel.h>

LOG_MODULE_REGISTER(ws_server, LOG_LEVEL_INF);

#include "websocket.h"
#include "mygpio.h"

#define FIN_SHIFT		   7u
#define RSV1_SHIFT		6u
#define RSV2_SHIFT		5u
#define RSV3_SHIFT		4u
#define OPCODE_SHIFT		0u

#define BOOL_MASK		0x1  /* boolean value mask */
#define HALF_BYTE_MASK		0xF  /* half byte value mask */

#define MAX_NUM_WS_CONN 2 // with currently 3 threads one should be reserved for http connections

#define WS_URI_GPIO "/ws_gpio_status"
#define WS_URI_LOG "/ws_log"

#define WS_LOG_LINE_LEN 512

static int ws_char_out(uint8_t *data, size_t length, void *ctx);
static uint8_t ws_log_buf[WS_LOG_LINE_LEN];
LOG_OUTPUT_DEFINE(log_output_ws, ws_char_out, ws_log_buf, sizeof(ws_log_buf));

enum ws_endpoint {
	WS_ENDPOINT_NONE,
	WS_ENDPOINT_GPIO,
	WS_ENDPOINT_LOG
};

struct ws_connection {
   struct mg_connection * conn;
	enum ws_endpoint endpoint;
   bool ready;
};

K_MUTEX_DEFINE(ws_conn_mutex);

struct ws_connection ws_conn[MAX_NUM_WS_CONN] = {0};

#define WS_GPIO_MSG_LEN 128
K_MSGQ_DEFINE(ws_gpio_msgq, WS_GPIO_MSG_LEN, 10, 4);

K_MUTEX_DEFINE(gpio_listener_mutex);

static int ws_connect_handler(const struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	int ret = -1;
	enum ws_endpoint ep = WS_ENDPOINT_NONE; 

	if (0 == strcmp(ri->local_uri, WS_URI_GPIO)) {
		ep = WS_ENDPOINT_GPIO;
	} else if (0 == strcmp(ri->local_uri, WS_URI_LOG)) {
		ep = WS_ENDPOINT_LOG;
	} else {
		LOG_INF("Endpoint %s not supported", ri->local_uri);
		return ret;
	}

   k_mutex_lock(&ws_conn_mutex, K_FOREVER);
	for (uint32_t i = 0; i < MAX_NUM_WS_CONN; i++) {
		if (ws_conn[i].conn == NULL) {
			ws_conn[i].conn = (struct mg_connection*)conn;
			ws_conn[i].ready = false;
			ws_conn[i].endpoint = ep;
			ret = 0;
			break;
		}
	}
	k_mutex_unlock(&ws_conn_mutex);

	if (ret < 0) {
	   LOG_INF("No free websocket - declined connection from %s:%d", ri->remote_addr, ri->remote_port);
	} else {
	   LOG_INF("Websocket %s connected from %s:%d", ri->local_uri, ri->remote_addr, ri->remote_port);
	}
	return ret;
}

static void ws_ready_handler(struct mg_connection *conn, void *cbdata)
{
   k_mutex_lock(&ws_conn_mutex, K_FOREVER);
	for (uint32_t i = 0; i < MAX_NUM_WS_CONN; i++) {
		if (ws_conn[i].conn == conn) {
			ws_conn[i].ready = true;
			break;
		}
	}
	k_mutex_unlock(&ws_conn_mutex);
}

static int ws_data_handler(struct mg_connection *conn, int bits,
				  char *data, size_t data_len, void *cbdata)
{
	int ret_state = 1;

	/* Encode bits as by https://tools.ietf.org/html/rfc6455#section-5.2: */
	const bool FIN = (bits >> FIN_SHIFT) & BOOL_MASK;
	const bool RSV1 = (bits >> RSV1_SHIFT) & BOOL_MASK;
	const bool RSV2 = (bits >> RSV2_SHIFT) & BOOL_MASK;
	const bool RSV3 = (bits >> RSV2_SHIFT) & BOOL_MASK;

	uint8_t OPCODE = (bits >> OPCODE_SHIFT) & HALF_BYTE_MASK;

	(void)FIN;
	(void)RSV1;
	(void)RSV2;
	(void)RSV3;

	LOG_DBG("got bits: %d", bits);
	LOG_DBG("\t\twith OPCODE: %d", OPCODE);
   char mystring[256];
   uint32_t len = data_len < sizeof(mystring) ? data_len : sizeof(mystring) - 1;
   memcpy(mystring, data, len);
   mystring[len] = '\0';
   LOG_INF("ws received: %s", mystring);

	/* Process depending of opcode: */
	switch (OPCODE) {
	case MG_WEBSOCKET_OPCODE_CONTINUATION:
		break;
	case MG_WEBSOCKET_OPCODE_TEXT:
		break;
	case MG_WEBSOCKET_OPCODE_BINARY:
		ret_state = 0;
		mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, NULL, 0);
		LOG_INF("Binary data not supported currently: close connection");
		break;
	case MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE:
		ret_state = 0;
		mg_websocket_write(conn, OPCODE, NULL, 0);
		break;
	case MG_WEBSOCKET_OPCODE_PING:
		break;
	case MG_WEBSOCKET_OPCODE_PONG:
		break;
	default:
		ret_state = 0;
		mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, NULL, 0);
		LOG_ERR("Unknown OPCODE: close connection");
		break;
	}

	return ret_state;
}

static void ws_close_handler(const struct mg_connection *conn,
				    void *cbdata)
{
   k_mutex_lock(&ws_conn_mutex, K_FOREVER);
	for (uint32_t i = 0; i < MAX_NUM_WS_CONN; i++) {
		if (ws_conn[i].conn == conn) {
			ws_conn[i].conn = NULL;
			ws_conn[i].ready = false;
			ws_conn[i].endpoint = WS_ENDPOINT_NONE;
			break;
		}
	}
	k_mutex_unlock(&ws_conn_mutex);

 	const struct mg_request_info *ri = mg_get_request_info(conn);
   LOG_INF("Websocket close (%s:%d)", ri->remote_addr, ri->remote_port);
}

static void gpio_listener(const char* json_name, uint8_t value) {
	static char gpio_listener_buf[WS_GPIO_MSG_LEN];
   k_mutex_lock(&gpio_listener_mutex, K_FOREVER);
   snprintf(gpio_listener_buf, WS_GPIO_MSG_LEN, "{\"%s\":%d}", json_name, value);
   if (k_msgq_put(&ws_gpio_msgq, gpio_listener_buf, K_NO_WAIT) != 0) {
      LOG_INF("failed to add gpio change to msg queue");
   }
   k_mutex_unlock(&gpio_listener_mutex);
}

void init_websocket_server_handlers(struct mg_context *ctx)
{
   mygpio_register_listener(gpio_listener);

	mg_set_websocket_handler(ctx, WS_URI_GPIO,
				ws_connect_handler,
				ws_ready_handler,
				ws_data_handler,
				ws_close_handler,
				NULL);

	mg_set_websocket_handler(ctx, WS_URI_LOG,
				ws_connect_handler,
				ws_ready_handler,
				ws_data_handler,
				ws_close_handler,
				NULL);

   k_thread_name_set(k_current_get(), "websocket_sender");
   char msg[WS_GPIO_MSG_LEN];

   while (true) {
      k_msgq_get(&ws_gpio_msgq, msg, K_FOREVER);
		k_mutex_lock(&ws_conn_mutex, K_FOREVER);
		for (uint32_t i = 0; i < MAX_NUM_WS_CONN; i++) {
			if (ws_conn[i].conn != NULL && ws_conn[i].ready && ws_conn[i].endpoint == WS_ENDPOINT_GPIO) {
				(void)mg_websocket_write(ws_conn[i].conn, MG_WEBSOCKET_OPCODE_TEXT, msg, strlen(msg));
			}
		}
		k_mutex_unlock(&ws_conn_mutex);
   }
}

static void ws_log(char* data, size_t length) {
	k_mutex_lock(&ws_conn_mutex, K_FOREVER);
	for (uint32_t i = 0; i < MAX_NUM_WS_CONN; i++) {
		if (ws_conn[i].conn != NULL && ws_conn[i].ready && ws_conn[i].endpoint == WS_ENDPOINT_LOG) {
			(void)mg_websocket_write(ws_conn[i].conn, MG_WEBSOCKET_OPCODE_TEXT, data, length);
		}
	}
	k_mutex_unlock(&ws_conn_mutex);
}

static int ws_char_out(uint8_t *data, size_t length, void *ctx)
{
	static uint32_t index = 0;
	static uint8_t immediate_buffer[WS_LOG_LINE_LEN + 1];

	if (length == 0) {
		ws_log(immediate_buffer, index);
		index = 0;
	} else if (length == 1) {
		immediate_buffer[index++] = *data;
		if (index == sizeof(immediate_buffer) - 1) {
			ws_log(immediate_buffer, index);
			index = 0;
		}
	} else {
		ws_log(data, length);
		index = 0;		
	}

	return length;
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
	log_output_string(&log_output_ws, src_level,
			  timestamp, fmt, ap, flags);
	irq_unlock(key);
}

const struct log_backend_api log_backend_ws_api = {
	.put = NULL,
	.put_sync_string = sync_string,
	.put_sync_hexdump = NULL,
	.panic = NULL,
	.init = NULL,
	.dropped = NULL
};

LOG_BACKEND_DEFINE(log_backend_ws, log_backend_ws_api, true);
