/*
 * Copyright (c) 2019 Antmicro Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */


extern void log_proxy(const char* fmt, ...);

static void log_access(const struct mg_connection *conn)
{
	const struct mg_request_info *ri;

	if (!conn || !conn->dom_ctx) {
		return;
	}

	ri = &conn->request_info;

	log_proxy("%s - %s from %s HTTP/%s %d",
	       ri->request_method ? ri->request_method : "-",
	       ri->request_uri ? ri->request_uri : "-",
	       ri->remote_addr ? ri->remote_addr : "-",
	       ri->http_version,
	       conn->status_code);
}
