/*
 * Copyright (c) 2020 Alexander Kozhinov <AlexanderKozhinov@yandex.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#include <kernel.h>
LOG_MODULE_REGISTER(websocket_server, LOG_LEVEL_INF);

#include "websocket.h"
#include "mygpio.h"

#define FIN_SHIFT		   7u
#define RSV1_SHIFT		6u
#define RSV2_SHIFT		5u
#define RSV3_SHIFT		4u
#define OPCODE_SHIFT		0u

#define BOOL_MASK		0x1  /* boolean value mask */
#define HALF_BYTE_MASK		0xF  /* half byte value mask */

struct ws_connection {
   struct mg_connection * conn;
   bool ready;
   struct k_mutex mutex;
} connection = {0};

#define WS_MSG_LEN 128
K_MSGQ_DEFINE(ws_msgq, WS_MSG_LEN, 10, 4);

char gpio_listener_buf[WS_MSG_LEN];
K_MUTEX_DEFINE(gpio_listener_mutex);

static int ws_connect_handler(const struct mg_connection *conn, void *cbdata)
{
	int ret = -1;

   k_mutex_lock(&connection.mutex, K_FOREVER);
	if (!connection.conn) {
		connection.conn = (struct mg_connection*)conn;
		connection.ready = false;
		ret = 0;
	}

   k_mutex_unlock(&connection.mutex);

	const struct mg_request_info *ri = mg_get_request_info(conn);

	if (ret < 0) {
	   LOG_INF("Websocket busy - declined connection from %s:%d", ri->remote_addr, ri->remote_port);
	} else {
	   LOG_INF("Websocket connected from %s:%d", ri->remote_addr, ri->remote_port);
	}
	return ret;
}

static void ws_ready_handler(struct mg_connection *conn, void *cbdata)
{
   k_mutex_lock(&connection.mutex, K_FOREVER);
   connection.ready = true;
   k_mutex_unlock(&connection.mutex);
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
   k_mutex_lock(&connection.mutex, K_FOREVER);
   connection.conn = NULL;
   connection.ready = false;
   k_mutex_unlock(&connection.mutex);
 	const struct mg_request_info *ri = mg_get_request_info(conn);
   LOG_INF("Websocket close (%s:%d)", ri->remote_addr, ri->remote_port);
}

static void gpio_listener(const char* json_name, uint8_t value) {
   k_mutex_lock(&gpio_listener_mutex, K_FOREVER);
   snprintf(gpio_listener_buf, WS_MSG_LEN, "{\"%s\":%d}", json_name, value);
   if (k_msgq_put(&ws_msgq, gpio_listener_buf, K_NO_WAIT) != 0) {
      LOG_INF("failed to add gpio change to msg queue");
   }
   k_mutex_unlock(&gpio_listener_mutex);
}

void init_websocket_server_handlers(struct mg_context *ctx)
{
   k_mutex_init(&connection.mutex);
   register_listener(gpio_listener);

	mg_set_websocket_handler(ctx, "/ws_gpio_status",
				ws_connect_handler,
				ws_ready_handler,
				ws_data_handler,
				ws_close_handler,
				NULL);

   char msg[WS_MSG_LEN];

   k_thread_name_set(k_current_get(), "websocket_sender");

   while (true) {
      k_msgq_get(&ws_msgq, msg, K_FOREVER);
      k_mutex_lock(&connection.mutex, K_FOREVER);
      if (connection.conn != NULL && connection.ready) {
         (void)mg_websocket_write(connection.conn, MG_WEBSOCKET_OPCODE_TEXT, msg, strlen(msg));
      }
      k_mutex_unlock(&connection.mutex);
   }
}
