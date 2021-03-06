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

#include "my_log_backend.h"
#include "mysettings.h"
#include "mygpio.h"
#include "web_shell.h"
#include "websocket.h"
#include "cJSON.h"

#define HTTP_PORT	80
//#define HTTPS_PORT	4443

#define CIVETWEB_MAIN_THREAD_STACK_SIZE		CONFIG_MAIN_STACK_SIZE

/* Use samllest possible value of 1024 (see the line 18619 of civetweb.c) */
#define MAX_REQUEST_SIZE_BYTES			1024

// generated from html files
extern int button_handler(struct mg_connection *conn, void *cbdata);
extern int switches_handler(struct mg_connection *conn, void *cbdata);
extern int input_handler(struct mg_connection *conn, void *cbdata);

// pseude file system for static html files
struct file_def {
	const char path[128];
	const char mime[32];
	const char* data;
	size_t len;
};

static const char index_htm[] = {
	#include "../htm/index.htm.gz.inc"
};

static const char webshell_htm[] = {
	#include "../htm/webshell.htm.gz.inc"
};

static const char live_log_htm[] = {
	#include "../htm/live_log.htm.gz.inc"
};

static const char bootstrap_js[] = {
	#include "../htm/bootstrap.bundle.min.js.gz.inc"
};

static const char bootstrap_css[] = {
	#include "../htm/bootstrap.min.css.gz.inc"
};

static const char jquery_js[] = {
	#include "../htm/jquery-3.3.1.min.js.gz.inc"
};

struct file_def file_system[] = {
	{.path = "/", .mime = "text/html", .data = index_htm, .len = sizeof(index_htm)},
	{.path = "/webshell.htm", .mime = "text/html", .data = webshell_htm, .len = sizeof(webshell_htm)},
	{.path = "/live_log.htm", .mime = "text/html", .data = live_log_htm, .len = sizeof(live_log_htm)},
	{.path = "/bootstrap.bundle.min.js", .mime = "text/javascript", .data = bootstrap_js, .len = sizeof(bootstrap_js)},
	{.path = "/bootstrap.min.css", .mime = "text/css", .data = bootstrap_css, .len = sizeof(bootstrap_css)},
	{.path = "/jquery-3.3.1.min.js", .mime = "text/javascript", .data = jquery_js, .len = sizeof(jquery_js)},
};

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

void send_ok(struct mg_connection *conn, const char* mime_type) {
	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: %s\r\n"
		  "Connection: close\r\n\r\n", mime_type);
}


void send_error(struct mg_connection *conn, const char *msg) {
	mg_printf(conn,
		  "HTTP/1.1 400 Bad Request\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
	mg_printf(conn, "%s\n", msg);
	LOG_INF("HTTP error: %s", msg);
}

static int set_output_handler(struct mg_connection *conn, void *cbdata) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char buffer[1024];
	int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
	cJSON *obj, *elem;

	if (0 != strcmp(ri->request_method, "POST")) {
		send_error(conn, "Only POST requests are allowed");
		return 400;
	}

	if ((dlen < 1) || (dlen >= sizeof(buffer))) {
		send_error(conn, "Invalid data size (no or exceeded maximum length)");
		return 400;
	}

	buffer[dlen] = 0;	
	LOG_INF("HTTP params: %s", buffer);

	obj = cJSON_Parse(buffer);
	if (obj == NULL) {
		send_error(conn, "Json parse error");
		return 400;
	}

	int64_t timer = 0;
	char *current_key = NULL;

	bool schema_ok = true;
	cJSON_ArrayForEach(elem, obj) {
		current_key = elem->string;
		if (current_key != NULL) {
			if (elem->type != cJSON_Number) {
				schema_ok = false;
				LOG_INF("error: element type for %s is not a number", current_key);	
			}
			if (0 == strcmp(current_key, "delay")) {
				if (elem->valueint >= 100 && elem->valueint <= 5000) {
					timer = elem->valueint;
				} else {
					schema_ok = false;
					LOG_INF("error: delay has to be in the range of [100..5000]");
				}
			} else if (get_output_by_name(current_key) >= 0) {
				if (elem->valueint != 0 && elem->valueint != 1) {
					schema_ok = false;
					LOG_INF("error: output value for %s is not 0 or 1", current_key);
				}
			} else {
				schema_ok = false;
				LOG_INF("error: output '%s' does not exist", current_key);
			}
		} else {
			schema_ok = false;
			LOG_INF("error: unexpected element in json object");	
		}
	}

	if (!schema_ok) {
		send_error(conn, "error: json object deviates from schema");
		return 400;
	}

	// schema is ok, so set outputs now
	cJSON_ArrayForEach(elem, obj) {
		current_key = elem->string;
		if (0 != strcmp(current_key, "delay")) {
			(void)set_output_by_name(current_key, elem->valueint, timer);
		}
	}

	cJSON_Delete(obj);
	send_ok(conn, "text/plain");
	return 200;
}

static int set_output_default_handler(struct mg_connection *conn, void *cbdata) {
	const struct mg_request_info *ri = mg_get_request_info(conn);

	if (0 != strcmp(ri->request_method, "POST")) {
		send_error(conn, "Only POST requests are allowed");
		return 400;
	}

	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		set_output(i, get_output(i)->default_value, 0);
	}

	send_ok(conn, "text/plain");
	return 200;
}

static int get_output_handler(struct mg_connection *conn, void *cbdata)
{
	send_ok(conn, "text/plain");

	mg_printf(conn, "{");
	for (uint32_t i = 0; i < NUM_OUTPUTS; i++) {
		mg_printf(conn, "%s \"%s\":%d", i == 0 ? "" : ",", get_output(i)->json_name, get_output(i)->value);		
	}
	mg_printf(conn, "}");

	return 200;
}

static int get_input_handler(struct mg_connection *conn, void *cbdata)
{
	send_ok(conn, "text/plain");

	mg_printf(conn, "{");
	for (uint32_t i = 0; i < NUM_INPUTS; i++) {
		mg_printf(conn, "%s \"%s\":%d", i == 0 ? "" : ",", get_input(i)->json_name, get_input_state(i));		
	}
	mg_printf(conn, "}");

	return 200;
}

static int webshell_cmd_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);
	char buffer[256];
	int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);

	if (0 != strcmp(ri->request_method, "POST")) {
		send_error(conn, "Only POST requests are allowed");
		return 400;
	}

	if ((dlen < 1) || (dlen >= sizeof(buffer))) {
		send_error(conn, "Invalid data size (no or exceeded maximum length)");
		return 400;
	}

	buffer[dlen] = 0;	
	LOG_INF("HTTP params: %s", buffer);

	send_ok(conn, "text/plain");

	shell_backend_dummy_clear_output(shell_backend_dummy_get_ptr());
	shell_execute_cmd(shell_backend_dummy_get_ptr(), buffer);

	size_t out_len;
	const char* out_buf;
	out_buf = shell_backend_dummy_get_output(shell_backend_dummy_get_ptr(), &out_len);

	mg_write(conn, out_buf, out_len);

	mg_printf(conn, "\n");

	return 200;
}

static int get_log_handler(struct mg_connection *conn, void *cbdata)
{
	char line[MY_LOG_BACKEND_RB_SLOT_SIZE];

	send_ok(conn, "text/plain");

	for (bool end = log_get_next_line(true, line); !end; end = log_get_next_line(false, line)) {
		mg_printf(conn, "%s", line);	
	}
	return 200;
}

static int file_system_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *ri = mg_get_request_info(conn);

	for (uint32_t i = 0; i < sizeof(file_system)/sizeof(file_system)[0]; i++) {
		if (0 == strcmp(ri->request_uri, file_system[i].path)) {
			mg_printf(conn,
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: %s\r\n"
				"Content-Encoding: gzip\r\n"
				"Connection: close\r\n\r\n", file_system[i].mime);
			mg_write(conn, file_system[i].data, file_system[i].len);
			return 200;
		}
	}

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
		"3",
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
	mg_set_request_handler(ctx, "/shell$", webshell_cmd_handler, 0);
	mg_set_request_handler(ctx, "/get_outputs$", get_output_handler, 0);
	mg_set_request_handler(ctx, "/get_inputs$", get_input_handler, 0);
	mg_set_request_handler(ctx, "/set_outputs$", set_output_handler, 0);
	mg_set_request_handler(ctx, "/set_default$", set_output_default_handler, 0);
	mg_set_request_handler(ctx, "/buttons$", button_handler, 0);
	mg_set_request_handler(ctx, "/switches$", switches_handler, 0);
	mg_set_request_handler(ctx, "/inputs$", input_handler, 0);
	mg_set_request_handler(ctx, "/", file_system_handler, 0);

	init_websocket_server_handlers(ctx); // this call will not return
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

	init_gpios();

	pthread_attr_t civetweb_attr;
	pthread_t civetweb_thread;

	LOG_INF("Run http server");
	(void)pthread_attr_init(&civetweb_attr);
	(void)pthread_attr_setstack(&civetweb_attr, &civetweb_stack,
				    CIVETWEB_MAIN_THREAD_STACK_SIZE);

	(void)pthread_create(&civetweb_thread, &civetweb_attr,
			     &main_pthread, 0);

	// now check output timeouts
	while (true) {
		check_output_timer();
		k_msleep(100);
	}
}
