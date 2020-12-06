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

#include <zephyr.h>
#include <posix/pthread.h>
#include <data/json.h>

#include "log_backend_rb.h"
#include "civetweb.h"

#define HTTP_PORT	8080
#define HTTPS_PORT	4443

#define CIVETWEB_MAIN_THREAD_STACK_SIZE		CONFIG_MAIN_STACK_SIZE

/* Use samllest possible value of 1024 (see the line 18619 of civetweb.c) */
#define MAX_REQUEST_SIZE_BYTES			1024

extern int net_init_clock_via_sntp(void);

K_THREAD_STACK_DEFINE(civetweb_stack, CIVETWEB_MAIN_THREAD_STACK_SIZE);

static struct net_mgmt_event_callback mgmt_cb;

void send_ok(struct mg_connection *conn)
{
	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
}

int get_log_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char line[CONFIG_LOG_BACKEND_RB_SLOT_SIZE];

	LOG_INF("Request from %s", log_strdup(ri->remote_addr));

	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n");

	uint32_t index = log_get_next_line(index, line);
	while (index != LOG_GET_FIRST) {
		mg_printf(conn, line);
		index = log_get_next_line(index, line);
	};

	return 200;
}


int hello_world_handler(struct mg_connection *conn, void *cbdata)
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

void *main_pthread(void *arg)
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


	mg_set_request_handler(ctx, "/log", get_log_handler, 0);
	mg_set_request_handler(ctx, "/$", hello_world_handler, 0);

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
	
	pthread_attr_t civetweb_attr;
	pthread_t civetweb_thread;

	LOG_INF("Run http server");
	(void)pthread_attr_init(&civetweb_attr);
	(void)pthread_attr_setstack(&civetweb_attr, &civetweb_stack,
				    CIVETWEB_MAIN_THREAD_STACK_SIZE);

	(void)pthread_create(&civetweb_thread, &civetweb_attr,
			     &main_pthread, 0);

}
