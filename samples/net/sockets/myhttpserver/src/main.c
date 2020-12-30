/*
 * Copyright (c) 2019 Antmicro Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(myhttpserver, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <errno.h>
#include <stdio.h>

#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/ethernet_mgmt.h>
#include <net/sntp.h>

#include <posix/pthread.h>
#include <posix/time.h>
#include <civetweb.h>

#include "log_backend_rb.h"
#include "mysettings.h"
#include "mygpio.h"
#include "cJSON.h"

#define HTTP_PORT	80
//#define HTTPS_PORT	4443

#define CIVETWEB_MAIN_THREAD_STACK_SIZE		CONFIG_MAIN_STACK_SIZE

/* Use samllest possible value of 1024 (see the line 18619 of civetweb.c) */
#define MAX_REQUEST_SIZE_BYTES			1024

// generated from html files
extern int button_handler(struct mg_connection *conn, void *cbdata);

K_THREAD_STACK_DEFINE(civetweb_stack, CIVETWEB_MAIN_THREAD_STACK_SIZE);

void log_proxy(const char* fmt, ...) {
	struct log_msg_ids src_level = {
		.domain_id = CONFIG_LOG_DOMAIN_ID,
		.level = LOG_LEVEL_INF,
		.source_id = (uint16_t)LOG_CURRENT_MODULE_ID()
	};

	va_list myargs;
	va_start(myargs, fmt);
	log_generic(src_level, fmt, myargs, LOG_STRDUP_CHECK_EXEC);
	va_end(myargs);
}

static struct net_mgmt_event_callback mgmt_cb;
static struct k_delayed_work sntp_timer;

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
	mg_printf(conn, "%s", msg);
	LOG_INF("HTTP error: %s", msg);
}

static int set_output_handler(struct mg_connection *conn, void *cbdata) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char buffer[1024];
	int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
	cJSON *obj, *elem;

	if (0 != strcmp(ri->request_method, "POST")) {
		send_error(conn, "Only POST requests are allowed\n");
		return 400;
	}

	if ((dlen < 1) || (dlen >= sizeof(buffer))) {
		send_error(conn, "Invalid data size (no or exceeded maximum length)\n");
		return 400;
	}

	buffer[dlen] = 0;	
	LOG_INF("HTTP params: %s", buffer);

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

static int get_output_handler(struct mg_connection *conn, void *cbdata)
{
	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n");

	mg_printf(conn, "{");
	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		mg_printf(conn, "%s \"%s\":%d", i == 0 ? "" : ",", get_output(i)->json_name, get_output(i)->value);		
	}
	mg_printf(conn, "}");

	return 200;
}

static int get_log_handler(struct mg_connection *conn, void *cbdata)
{
	char line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE];

	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n");

	for (bool end = log_get_next_line(true, line); !end; end = log_get_next_line(false, line)) {
		mg_printf(conn, "%s", line);	
	}
	return 200;
}


static int hello_world_handler(struct mg_connection *conn, void *cbdata)
{
	send_ok(conn);
	mg_printf(conn, "<html><body>");
	mg_printf(conn, "<h3>Hello World from myhttpserver!</h3>");
	mg_printf(conn, "</body></html>\n");

	return 200;
}

static int file_not_found_handler(struct mg_connection *conn, void *cbdata)
{
	mg_printf(conn,
		  "HTTP/1.1 404 Not Found\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
	LOG_INF("Requested page not available");

	return 404;
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
	mg_set_request_handler(ctx, "/get$", get_output_handler, 0);
	mg_set_request_handler(ctx, "/set$", set_output_handler, 0);
	mg_set_request_handler(ctx, "/buttons$", button_handler, 0);
	mg_set_request_handler(ctx, "/$", hello_world_handler, 0);
	mg_set_request_handler(ctx, "/", file_not_found_handler, 0);

	LOG_INF("main_pthread end");
	return 0;
}

static void get_time_from_sntp(struct k_work *work) {
	struct sntp_time ts;
	struct timespec tspec;
	int res = sntp_simple(get_settings()->sntp_server, 3000, &ts);

	if (res < 0) {
		LOG_ERR("Cannot set time using SNTP");
		return;
	}

	tspec.tv_sec = ts.seconds;
	tspec.tv_nsec = ((uint64_t)ts.fraction * (1000 * 1000 * 1000)) >> 32;
	res = clock_settime(CLOCK_REALTIME, &tspec);

	LOG_INF("Setup clock via sntp");
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event,
		    struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	// run it from work queue because task of network management thread is very small
	k_delayed_work_init(&sntp_timer, get_time_from_sntp);
	k_delayed_work_submit(&sntp_timer, K_NO_WAIT);
}

void main(void)
{
	net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler,
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
