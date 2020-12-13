/*
 * Copyright (c) 2019 Antmicro Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(myhttpserver, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <linker/sections.h>
#include <errno.h>
#include <stdio.h>

#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/ethernet_mgmt.h>

#include <drivers/gpio.h>

#include <zephyr.h>
#include <posix/pthread.h>
#include <data/json.h>

#include "log_backend_rb.h"
#include "civetweb.h"
#include "cJSON.h"

#define HTTP_PORT	80
//#define HTTPS_PORT	4443

#define CIVETWEB_MAIN_THREAD_STACK_SIZE		CONFIG_MAIN_STACK_SIZE

/* Use samllest possible value of 1024 (see the line 18619 of civetweb.c) */
#define MAX_REQUEST_SIZE_BYTES			1024

extern int net_init_clock_via_sntp(void);

K_THREAD_STACK_DEFINE(civetweb_stack, CIVETWEB_MAIN_THREAD_STACK_SIZE);

static struct net_mgmt_event_callback mgmt_cb;

struct output_struct {
	const char* json_name;
	const struct device *dev;
	uint8_t index;
	uint8_t value;
	time_t time;
};

#define NUM_OUTPUTS 3

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

void send_ok(struct mg_connection *conn) {
	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
}


void send_error(struct mg_connection *conn, const char *msg) {
	mg_printf(conn,
		  "HTTP/1.1 400 Bad Request\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
	mg_printf(conn, msg);
}

static int set_output_handler(struct mg_connection *conn, void *cbdata) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char buffer[1024];
	int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
	cJSON *obj, *elem;

	LOG_INF("Request from %s", log_strdup(ri->remote_addr));

	if (0 != strcmp(ri->request_method, "POST")) {
		send_error(conn, "Only POST requests are allowed\n");
		return 400;
	}

	if ((dlen < 1) || (dlen >= sizeof(buffer))) {
		send_error(conn, "Invalid data size (no or exceeded maximum length)\n");
		return 400;
	}

	buffer[dlen] = 0;	

	obj = cJSON_Parse(buffer);
	if (obj == NULL) {
		send_error(conn, "Json parse error\n");
		return 400;
	}

	char *current_key = NULL;
	cJSON_ArrayForEach(elem, obj)
	{
		current_key = elem->string;
		if (current_key != NULL)
		{
			// TODO: check type of value
			// TODO: check return value (output exists)
			set_output_by_name(current_key, elem->valueint);
		}
	}

	cJSON_Delete(obj);
	send_ok(conn);
	return 200;
}

static int get_log_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE];

	LOG_INF("Request from %s", log_strdup(ri->remote_addr));

	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n");

	uint32_t index = log_get_next_line(LOG_GET_FIRST, line);
	while (index != LOG_GET_FIRST) {
		mg_printf(conn, line);
		index = log_get_next_line(index, line);
	};

	return 200;
}


static int hello_world_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	LOG_INF("Request from %s\n", log_strdup(ri->remote_addr));

	send_ok(conn);
	mg_printf(conn, "<html><body>");
	mg_printf(conn, "<h3>Hello World from Zephyr!</h3>");
	mg_printf(conn, "See also:\n");
	mg_printf(conn, "<ul>\n");
	mg_printf(conn, "<li><a href=/info>system info</a></li>\n");
	mg_printf(conn, "<li><a href=/history>cookie demo</a></li>\n");
	mg_printf(conn, "</ul>\n");
	mg_printf(conn, "</body></html>\n");

	return 200;
}

static void *main_pthread(void *arg)
{
	LOG_INF("main_pthread");

	static const char * const options[] = {
		"listening_ports",
		STRINGIFY(HTTP_PORT),
		"num_threads",
		"1",
		"max_request_size",
		STRINGIFY(MAX_REQUEST_SIZE_BYTES),
		NULL
	};

	struct mg_callbacks callbacks;
	struct mg_context *ctx;

	(void)arg;

	memset(&callbacks, 0, sizeof(callbacks));
	ctx = mg_start(&callbacks, 0, (const char **)options);

	if (ctx == NULL) {
		printf("Unable to start the server.");
		return 0;
	}

	mg_set_request_handler(ctx, "/log$", get_log_handler, 0);
	mg_set_request_handler(ctx, "/set$", set_output_handler, 0);
	mg_set_request_handler(ctx, "/", hello_world_handler, 0);

	LOG_INF("main_pthread end");
	return 0;
}

static void handler(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event,
		    struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	LOG_INF("Setup clock via sntp");
	net_init_clock_via_sntp();
}


void main(void)
{
	net_mgmt_init_event_callback(&mgmt_cb, handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	init_outputs();

	pthread_attr_t civetweb_attr;
	pthread_t civetweb_thread;

	LOG_INF("Run http server");
	(void)pthread_attr_init(&civetweb_attr);
	(void)pthread_attr_setstack(&civetweb_attr, &civetweb_stack,
				    CIVETWEB_MAIN_THREAD_STACK_SIZE);

	(void)pthread_create(&civetweb_thread, &civetweb_attr,
			     &main_pthread, 0);

}
