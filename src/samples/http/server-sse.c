/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file
 * @brief HTTP Server sse
 *
 * Sample server that return a response and keep it alive.  It
 * implements server sent events. The server sends the data typed in
 * stdin and the data is broadcasted to all the clients connected.
 *
 * To test it:
 *
 * run ./server-sse -p 8080
 * open a browser in http://127.0.0.1:8080 or curl http://127.0.0.1:8080
 * type something
 *
 * To see the usage help, -h or --help.
 */


#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "soletta.h"
#include "sol-http.h"
#include "sol-http-server.h"
#include "sol-util.h"
#include "sol-util-file.h"

#define HTML_FILE \
    "<!DOCTYPE html>" \
    "<html>" \
    "<body>" \
    "<h1>Getting server updates</h1>" \
    "<div id=\"result\"></div>" \
    "<script>" \
    "if(typeof(EventSource) !== \"undefined\") {" \
    "var source = new EventSource(\"http://%.*s:%d/events\");" \
    "source.onmessage = function(event) {" \
    "document.getElementById(\"result\").innerHTML += event.data + \"<br>\";" \
    "};" \
    "} else {" \
    "document.getElementById(\"result\").innerHTML = \"Sorry, your browser does not support server-sent events...\";" \
    "}" \
    "</script>" \
    "</body>" \
    "</html>"


static struct sol_http_server *server;
static struct sol_fd *stdin_watch;
static struct sol_ptr_vector responses = SOL_PTR_VECTOR_INIT;
static int port = 8080;
static bool should_quit = false;

static bool
on_stdin(void *data, int fd, uint32_t flags)
{
    uint16_t i;
    struct sol_http_progressive_response *sse;
    struct sol_buffer value = SOL_BUFFER_INIT_EMPTY;

    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_HUP)) {
        fprintf(stderr, "ERROR: Something wrong happened with file descriptor: %d\n", fd);
        goto err;
    }

    if (flags & SOL_FD_FLAGS_IN) {
        int err;
        struct sol_blob *blob;

        /* this will loop trying to read as much data as possible to buffer. */
        err = sol_util_load_file_fd_buffer(fd, &value);
        if (err < 0) {
            fprintf(stderr, "ERROR: failed to read from stdin: %s\n",
                sol_util_strerrora(-err));
            goto err;
        }

        if (value.used == 0) {
            /* no data usually means ^D on the terminal, quit the application */
            printf("no data on stdin, quitting.\n");
            should_quit = true;
            SOL_PTR_VECTOR_FOREACH_IDX (&responses, sse, i)
                sol_http_progressive_response_del(sse, true);
            goto end;
        }

        blob = sol_buffer_to_blob(&value);
        if (!blob) {
            fprintf(stderr, "Could not alloc the blob data\n");
            goto err;
        }
        SOL_PTR_VECTOR_FOREACH_IDX (&responses, sse, i)
            sol_http_progressive_response_sse_feed(sse, blob);
        sol_blob_unref(blob);
    }

    sol_buffer_fini(&value);
    return true;

err:
    sol_quit_with_code(EXIT_FAILURE);
end:
    stdin_watch = NULL;
    sol_buffer_fini(&value);
    return false;
}

static void
delete_cb(void *data, const struct sol_http_progressive_response *sse)
{
    sol_ptr_vector_remove(&responses, sse);

    if (should_quit && !sol_ptr_vector_get_len(&responses))
        sol_quit();
}

static void
on_feed_done_cb(void *data, struct sol_http_progressive_response *sse, struct sol_blob *blob, int status)
{
    struct sol_str_slice slice = sol_str_slice_from_blob(blob);

    if (sol_str_slice_str_eq(slice, "data: ") || sol_str_slice_str_eq(slice, "\n\n"))
        return;
    if (isspace(slice.data[slice.len - 1]))
        slice.len--;
    printf("Blob data *%.*s* sent\n", SOL_STR_SLICE_PRINT(slice));
}

static int
request_events_cb(void *data, struct sol_http_request *request)
{
    int ret;
    struct sol_http_progressive_response *sse;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK,
        .content = SOL_BUFFER_INIT_EMPTY
    };
    struct sol_http_server_progressive_config config = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_PROGRESSIVE_CONFIG_API_VERSION, )
        .on_close = delete_cb,
        .on_feed_done = on_feed_done_cb
    };

    ret = sol_http_response_set_sse_headers(&response);
    if (ret < 0)
        return ret;

    sse = sol_http_server_send_progressive_response(request, &response, &config);
    sol_http_params_clear(&response.param);
    if (!sse)
        return -1;

    ret = sol_ptr_vector_append(&responses, sse);
    if (ret < 0) {
        sol_http_progressive_response_del(sse, false);
        return ret;
    }

    return 0;
}

static int
request_cb(void *data, struct sol_http_request *request)
{
    int r;

    SOL_BUFFER_DECLARE_STATIC(buf, SOL_NETWORK_INET_ADDR_STR_LEN);
    struct sol_network_link_addr addr;
    struct sol_http_response response = {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_RESPONSE_API_VERSION, )
        .param = SOL_HTTP_REQUEST_PARAMS_INIT,
        .response_code = SOL_HTTP_STATUS_OK,
    };

    r = sol_http_request_get_interface_address(request, &addr);
    SOL_INT_CHECK(r, < 0, r);

    SOL_NULL_CHECK(sol_network_link_addr_to_str(&addr, &buf), -1);

    /* SSE needs that URL matches to work */
    sol_buffer_init(&response.content);
    r = sol_buffer_append_printf(&response.content, HTML_FILE,
        SOL_STR_SLICE_PRINT(sol_buffer_get_slice(&buf)), addr.port);
    SOL_INT_CHECK(r, < 0, r);

    r = sol_http_server_send_response(request, &response);
    sol_buffer_fini(&response.content);

    return r;
}

static void
startup_server(void)
{
    char **argv = sol_argv();
    int c, opt_idx,  argc = sol_argc();
    static const struct option opts[] = {
        { "port", required_argument, NULL, 'p' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(argc, argv, "p:h", opts,
            &opt_idx)) != -1) {
        switch (c) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'h':
        default:
            fprintf(stderr,
                "Usage:\n\t%s [-p <port >]\n\n"
                "Then everything that is typed will be sent using SSE technique\n"
                "Test it opening a browser in http//127.0.0.1:<port>\n",
                argv[0]);
            sol_quit_with_code(EXIT_SUCCESS);
            return;
        }
    }

    /* always set stdin to non-block before we use sol_fd_add() on it,
     * otherwise we may block reading and it would impact the main
     * loop dispatching other events.
     */
    if (sol_util_fd_set_flag(STDIN_FILENO, O_NONBLOCK) < 0) {
        fprintf(stderr, "ERROR: cannot set stdin to non-block.\n");
        goto err;
    }

    stdin_watch = sol_fd_add(STDIN_FILENO,
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_ERR, on_stdin, NULL);
    if (!stdin_watch) {
        fprintf(stderr, "ERROR: Failed to watch stdin\n");
        goto err;
    }

    server = sol_http_server_new(&(struct sol_http_server_config) {
        SOL_SET_API_VERSION(.api_version = SOL_HTTP_SERVER_CONFIG_API_VERSION, )
        .port = port,
    });
    if (!server) {
        fprintf(stderr, "ERROR: Failed to create the server\n");
        goto err;
    }

    if (sol_http_server_register_handler(server, "/",
        request_cb, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to register the handler\n");
        goto err;
    }

    if (sol_http_server_register_handler(server, "/events",
        request_events_cb, NULL) < 0) {
        fprintf(stderr, "ERROR: Failed to register the handler\n");
        goto err;
    }

    printf("HTTP server at port %d.\n"
        "Start typing to send data\n", port);

    return;

err:
    sol_quit_with_code(EXIT_FAILURE);
}

static void
shutdown_server(void)
{
    uint16_t i;
    struct sol_http_progressive_response *sse;

    if (stdin_watch)
        sol_fd_del(stdin_watch);
    if (server)
        sol_http_server_del(server);

    SOL_PTR_VECTOR_FOREACH_IDX (&responses, sse, i)
        sol_http_progressive_response_del(sse, false);

    sol_ptr_vector_clear(&responses);
}


SOL_MAIN_DEFAULT(startup_server, shutdown_server);
