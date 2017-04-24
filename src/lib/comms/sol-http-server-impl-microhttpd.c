/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include "sol-buffer.h"
#include "sol-http-server.h"
#include "sol-log.h"
#include "sol-mainloop.h"
#include "sol-network-util.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util-internal.h"
#include "sol-vector.h"
#include "sol-arena.h"
#include "sol-file-reader.h"

#ifdef HAVE_LIBMAGIC
#include <magic.h>
#endif

#define SOL_HTTP_MULTIPART_HEADER "multipart/form-data"
#define SOL_HTTP_PARAM_IF_SINCE_MODIFIED "If-Since-Modified"
#define SOL_HTTP_PARAM_LAST_MODIFIED "Last-Modified"
#define READABLE_BY_EVERYONE (S_IRUSR | S_IRGRP | S_IROTH)

#define SOL_HTTP_REQUEST_BUFFER_SIZE 4096

#ifndef SOL_NO_API_VERSION
#define SOL_HTTP_SERVER_CONFIG_CHECK_API_VERSION(config, ...) \
    if ((config)->api_version != SOL_HTTP_SERVER_CONFIG_API_VERSION) { \
        SOL_WRN("" # config \
            "(%p)->api_version(%hu) != " \
            "SOL_HTTP_SERVER_CONFIG_API_VERSION(%hu)", \
            (config), (config)->api_version, \
            SOL_HTTP_SERVER_CONFIG_API_VERSION); \
        return __VA_ARGS__; \
    }
#else
#define SOL_HTTP_SERVER_CONFIG_CHECK_API_VERSION(config, ...)
#endif

static struct sol_blob sse_prefix = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"data: ",
    .size = sizeof("data: ") - 1,
    .refcnt = 1
};

static struct sol_blob sse_suffix = {
    .type = &SOL_BLOB_TYPE_NO_FREE,
    .parent = NULL,
    .mem = (void *)"\n\n",
    .size = sizeof("\n\n") - 1,
    .refcnt = 1
};

struct http_handler {
    time_t last_modified;
    struct sol_str_slice path;
    int (*request_cb)(void *data, struct sol_http_request *request);
    const void *user_data;
};

struct sol_http_request {
    struct MHD_Connection *connection;
    struct MHD_PostProcessor *pp;
    const char *url;
    struct sol_http_params params;
    struct sol_buffer buffer;
    struct sol_http_param_value param;
    size_t len;
    enum sol_http_method method;
    time_t if_since_modified;
    time_t last_modified;
    bool is_multipart;
    bool suspended;
};

struct sol_http_progressive_response {
    struct sol_http_request *request;
    void (*on_close)(void *data, const struct sol_http_progressive_response *progressive);
    void (*on_feed_done)(void *data, struct sol_http_progressive_response *progressive, struct sol_blob *blob, int status);
    const void *cb_data;
    struct sol_ptr_vector pending_blobs;
    size_t written;
    size_t feed_size;
    size_t accumulated_bytes;
    bool delete_me;
    bool graceful_del;
};

struct static_dir {
    size_t basename_len;
    char *basename;
    char *root;
};

struct default_page {
    char *page;
    enum sol_http_status_code error;
};

struct sol_http_server {
    struct MHD_Daemon *daemon;
    struct sol_vector dirs;
    struct sol_vector handlers;
    struct sol_vector fds;
    struct sol_vector defaults;
    struct sol_ptr_vector requests;
    size_t buf_size;
};

struct http_connection {
    struct sol_fd *watch;
    int fd;
};

#ifdef HAVE_LIBMAGIC
static magic_t magic;
#endif

static struct {
    struct sol_arena *arena;
    struct sol_vector table;
} ext_map = {
    .arena = NULL,
    .table = SOL_VECTOR_INIT(struct sol_str_table_ptr),
};

int sol_http_server_init(void);
void sol_http_server_shutdown(void);

int
sol_http_server_init(void)
{
    return 0;
}

void
sol_http_server_shutdown(void)
{
    sol_vector_clear(&ext_map.table);

    if (ext_map.arena) {
        sol_arena_del(ext_map.arena);
        ext_map.arena = NULL;
    }

#ifdef HAVE_LIBMAGIC
    if (magic) {
        magic_close(magic);
        magic = NULL;
    }
#endif
}

static void
load_ext_map(void)
{
    struct sol_file_reader *reader;
    struct sol_str_slice data;
    struct sol_str_table_ptr *sentinel;
    const char *itr, *itr_end, *last;
    char *mime = NULL;

    reader = sol_file_reader_open("/etc/mime.types");
    if (!reader) {
        SOL_DBG("no /etc/mime.types to map extensions to mime-types.");
        return;
    }

    data = sol_file_reader_get_all(reader);
    if (data.len == 0)
        goto end;

    ext_map.arena = sol_arena_new();
    SOL_NULL_CHECK_GOTO(ext_map.arena, end);

    itr = data.data;
    itr_end = data.data + data.len;
    last = itr;

    for (; itr < itr_end; itr++) {
        struct sol_str_table_ptr *entry;
        size_t i, len;
        char *s;

        if (!isspace(*itr))
            continue;

        if (last == itr) {
            last++;
            continue;
        } else if (last > itr)
            continue;

        len = itr - last;
        s = sol_arena_str_dup_n(ext_map.arena, last, len);
        SOL_NULL_CHECK_GOTO(s, end);
        last = itr + 1;

        /* mime.types format is weird, it's not one-map per line, the
         * only meaning is that mime-types have a slash. They may be
         * followed by extensions or not
         */
        if (strchr(s, '/')) {
            mime = s;
            continue;
        }

        for (i = 0; i < len; i++)
            s[i] = tolower(s[i]);

        entry = sol_vector_append(&ext_map.table);
        SOL_NULL_CHECK_GOTO(entry, end);

        entry->key = s;
        entry->len = len;
        entry->val = mime;
    }

    sentinel = sol_vector_append(&ext_map.table);
    if (sentinel)
        memset(sentinel, 0, ext_map.table.elem_size);

end:
    sol_file_reader_close(reader);
}

static const char *
get_file_mime_type(struct sol_http_server *server, const char *path)
{
    const char *mime = "application/octet-stream";

#ifdef HAVE_LIBMAGIC
    if (!magic) {
        magic = magic_open(MAGIC_MIME | MAGIC_SYMLINK);
        SOL_NULL_CHECK_GOTO(magic, exit);
        if (magic_load(magic, NULL) < 0) {
            magic_close(magic);
            magic = NULL;
            SOL_WRN("Could not load the magic database!");
            goto exit;
        }
    }

    mime = magic_file(magic, path);

    if (!mime)
        mime = "application/octet-stream";

exit:
#endif

    if (strstartswith(mime, "application/octet-stream") ||
        strstartswith(mime, "text/plain")) {
        const char *ext = strrchr(path, '.');

        if (ext) {
            size_t i, len = strlen(ext) - 1;
            char *lowext = alloca(len + 1);
            static const struct sol_str_table_ptr fallback_ext_map[] = {
                SOL_STR_TABLE_PTR_ITEM("js", "text/javascript"),
                SOL_STR_TABLE_PTR_ITEM("css", "text/css"),
                SOL_STR_TABLE_PTR_ITEM("png", "image/png"),
                SOL_STR_TABLE_PTR_ITEM("jpg", "image/jpeg"),
                SOL_STR_TABLE_PTR_ITEM("jpeg", "image/jpeg"),
                { }
            };

            ext++;
            for (i = 0; i < len; i++)
                lowext[i] = tolower(ext[i]);
            lowext[i] = '\0';

            if (!ext_map.table.data)
                load_ext_map();

            if (ext_map.table.data) {
                const char *map;

                map = sol_str_table_ptr_lookup_fallback(ext_map.table.data, SOL_STR_SLICE_STR(lowext, len), NULL);
                if (map)
                    return map;
            }

            return sol_str_table_ptr_lookup_fallback(fallback_ext_map, SOL_STR_SLICE_STR(lowext, len), mime);
        }
    }

    return mime;
}

static char *
sanitize_path(const char *str)
{
    char *handler, *aux;

    /*
     * Ensure that will have at least space for the first /
     * and nul char at end.
     */
    handler = calloc(1, strlen(str) + 2);
    SOL_NULL_CHECK(handler, NULL);

    aux = handler;
    *aux = '/';
    while (str && (*str != '\0')) {
        if ((*str == '/') && (*aux == '/')) {
            str++;
            continue;
        }

        aux++;
        *aux = *str;
        str++;
    }
    *(++aux) = '\0';

    return handler;
}

SOL_API const char *
sol_http_request_get_url(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, NULL);

    return request->url;
}

SOL_API const struct sol_http_params *
sol_http_request_get_params(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, NULL);

    return &request->params;
}

SOL_API enum sol_http_method
sol_http_request_get_method(const struct sol_http_request *request)
{
    SOL_NULL_CHECK(request, SOL_HTTP_METHOD_INVALID);

    return request->method;
}

static bool
set_last_modified_header(struct MHD_Response *response, time_t last_modified)
{
    struct tm result;
    ssize_t r;

    SOL_BUFFER_DECLARE_STATIC(buf, 128);

    SOL_NULL_CHECK(gmtime_r(&last_modified, &result), false);

    r = sol_util_strftime(&buf, "%a, %d %b %Y %H:%M:%S GMT", &result, false);
    if (!r) {
        SOL_WRN("Could not create the last modified date string");
        return false;
    }

    if (MHD_add_response_header(response, SOL_HTTP_PARAM_LAST_MODIFIED, buf.data) == MHD_NO) {
        SOL_WRN("Could not add the last modified header to the response");
        return false;
    }

    return true;
}

static struct MHD_Response *
build_mhd_response_params(struct MHD_Response *r,
    const struct sol_http_response *response)
{
    struct sol_buffer buf;
    uint16_t idx;
    struct sol_http_param_value *value;

    sol_buffer_init(&buf);
    SOL_HTTP_PARAMS_FOREACH_IDX (&response->param, value, idx) {
        int ret;

        buf.used = 0;
        switch (value->type) {
        case SOL_HTTP_PARAM_HEADER:
            ret = sol_buffer_append_slice(&buf, value->value.key_value.key);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, 0);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_slice(&buf, value->value.key_value.value);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            if (MHD_add_response_header(r, sol_buffer_at(&buf, 0), sol_buffer_at(&buf,
                value->value.key_value.key.len + 1)) == MHD_NO) {
                SOL_WRN("Could not add the header: %.*s", SOL_STR_SLICE_PRINT(value->value.key_value.key));
                goto err;
            }
            break;
        case SOL_HTTP_PARAM_COOKIE:
            ret = sol_buffer_append_slice(&buf, value->value.key_value.key);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_char(&buf, '=');
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            ret = sol_buffer_append_slice(&buf, value->value.key_value.value);
            SOL_INT_CHECK_GOTO(ret, < 0, err);
            if (MHD_add_response_header(r, MHD_HTTP_HEADER_SET_COOKIE, sol_buffer_at(&buf, 0)) == MHD_NO) {
                SOL_WRN("Could not add the cookie: %.*s", SOL_STR_SLICE_PRINT(value->value.key_value.key));
                goto err;
            }
            break;
        case SOL_HTTP_PARAM_QUERY_PARAM:
        case SOL_HTTP_PARAM_ALLOW_REDIR:
        case SOL_HTTP_PARAM_TIMEOUT:
        case SOL_HTTP_PARAM_AUTH_BASIC:
        default:
            break;
        }
    }

    sol_buffer_fini(&buf);
    return r;

err:
    sol_buffer_fini(&buf);
    MHD_destroy_response(r);
    return NULL;
}

static struct MHD_Response *
build_mhd_response(const struct sol_http_response *response, time_t last_modified)
{
    struct MHD_Response *r;

    r = MHD_create_response_from_buffer(response->content.used, response->content.data,
        MHD_RESPMEM_MUST_COPY);
    if (!r)
        return NULL;

    r = build_mhd_response_params(r, response);
    if (!r)
        return NULL;

    if (!set_last_modified_header(r, last_modified)) {
        MHD_destroy_response(r);
        return NULL;
    }

    return r;
}

static void
progressive_response_del_cb(void *data)
{
    struct sol_blob *blob;
    uint16_t i;
    struct sol_http_progressive_response *progressive = data;

    SOL_PTR_VECTOR_FOREACH_IDX (&progressive->pending_blobs, blob, i) {
        if (progressive->on_feed_done)
            progressive->on_feed_done((void *)progressive->cb_data, progressive, blob, -ECANCELED);
        sol_blob_unref(blob);
    }

    if (progressive->on_close)
        progressive->on_close((void *)progressive->cb_data, progressive);

    sol_ptr_vector_clear(&progressive->pending_blobs);
    free(progressive);
}

static ssize_t
progressive_response_cb(void *data, uint64_t pos, char *buf, size_t size)
{
    size_t len;
    struct sol_blob *blob;
    struct sol_http_progressive_response *progressive = data;

    if (progressive->delete_me) {
        if (!progressive->graceful_del ||
            (progressive->graceful_del && !sol_ptr_vector_get_len(&progressive->pending_blobs)))
            return MHD_CONTENT_READER_END_OF_STREAM;
    }

    if (!sol_ptr_vector_get_len(&progressive->pending_blobs)) {
        MHD_suspend_connection(progressive->request->connection);
        progressive->request->suspended = true;
        return 0;
    }

    blob = sol_ptr_vector_get_no_check(&progressive->pending_blobs, 0);
    len = sol_util_min(size, blob->size);
    memcpy(buf, (char *)blob->mem + progressive->written, len);
    progressive->written += len;

    if (progressive->written == blob->size) {
        progressive->accumulated_bytes -= blob->size;
        sol_ptr_vector_del(&progressive->pending_blobs, 0);
        progressive->written = 0;
        if (progressive->on_feed_done)
            progressive->on_feed_done((void *)progressive->cb_data, progressive, blob, 0);
        sol_blob_unref(blob);
    }

    return len;
}

static struct MHD_Response *
build_mhd_progressive_response(const struct sol_http_response *response,
    struct sol_http_progressive_response *progressive)
{
    struct MHD_Response *r;

    r = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 4096,
        progressive_response_cb, progressive, progressive_response_del_cb);
    if (!r)
        return NULL;

    return build_mhd_response_params(r, response);
}

static int
post_iterator(void *data, enum MHD_ValueKind kind, const char *key,
    const char *filename, const char *type,
    const char *encoding, const char *value, uint64_t off, size_t size)
{
    int r;
    char *str;
    struct sol_http_request *request = data;

    if (off) {
        r = sol_buffer_append_bytes(&request->buffer, (uint8_t *)value, size);
        SOL_INT_CHECK(r, < 0, MHD_NO);
        return MHD_YES;
    }

    if (request->param.value.data.key.len) {
        size_t len;
        void *copy;

        copy = sol_buffer_steal(&request->buffer, &len);
        request->param.value.data.value = SOL_STR_SLICE_STR(copy, len);
        if (sol_http_params_add(&request->params, request->param) < 0) {
            free(copy);
            SOL_WRN("Could not add %s key", key);
            return MHD_NO;
        }
        memset(&request->param, 0, sizeof(request->param));
    }

    if (request->is_multipart) {
        request->param.type = SOL_HTTP_PARAM_POST_DATA;
        if (filename) {
            str = strdup(filename);
            SOL_NULL_CHECK(str, MHD_NO);
            request->param.value.data.filename = sol_str_slice_from_str(str);
        }
    } else {
        request->param.type = SOL_HTTP_PARAM_POST_FIELD;
    }

    str = strdup(key);
    SOL_NULL_CHECK(str, MHD_NO);
    request->param.value.data.key = sol_str_slice_from_str(str);

    r = sol_buffer_append_bytes(&request->buffer, (uint8_t *)value, size);
    SOL_INT_CHECK(r, < 0, MHD_NO);

    return MHD_YES;
}

static time_t
process_if_modified_since(const char *value)
{
    char *s;
    struct tm t = { 0 };

    s = strptime(value, "%a, %d %b %Y %H:%M:%S GMT", &t);
    if (!s || *s != '\0')
        return 0;

    return timegm(&t);
}

static int
headers_iterator(void *data, enum MHD_ValueKind kind, const char *key, const char *value)
{
    struct sol_http_request *request = data;

    switch (kind) {
    case MHD_HEADER_KIND:
        if (!strcasecmp(key, SOL_HTTP_PARAM_IF_SINCE_MODIFIED)) {
            request->if_since_modified = process_if_modified_since(value);
            SOL_INT_CHECK_GOTO(request->if_since_modified, == 0, param_err);
        }
        if (!strncasecmp(value, SOL_HTTP_MULTIPART_HEADER,
            sizeof(SOL_HTTP_MULTIPART_HEADER) - 1))
            request->is_multipart = true;

        if (sol_http_params_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_HEADER(key, value)) < 0)
            goto param_err;
        break;
    case MHD_COOKIE_KIND:
        if (sol_http_params_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_COOKIE(key, value)) < 0)
            goto param_err;
        break;
    case MHD_GET_ARGUMENT_KIND:
        if (sol_http_params_add_copy(&request->params,
            SOL_HTTP_REQUEST_PARAM_QUERY(key, value)) < 0)
            goto param_err;
        break;
    default:
        goto param_err;
    }

    return MHD_YES;

param_err:
    return MHD_NO;
}

static int
get_static_file(const struct static_dir *dir, const char *url, struct sol_buffer *path)
{
    char *real_path;
    int ret;

    /* url given by microhttpd starts from /. e. g.
     * https://www.solettaproject.com => url == /
     * https://www.solettaproject.com/thankyou => url == /thankyou
     */
    while (*url && *(url + 1) == '/')
        url++;

    if (!streqn(url, dir->basename, dir->basename_len))
        return -EINVAL;

    url += dir->basename_len;
    if (*(dir->basename + dir->basename_len - 1) != '/' &&
        *url != '/')
        return -EINVAL;

    while (*url == '/')
        url++;

    path->used = 0;
    ret = sol_buffer_append_printf(path, "%s/%s", dir->root,
        *url ? url : "index.html");
    if (ret < 0)
        return ret;

    real_path = realpath(path->data, NULL);
    if (!real_path)
        return -errno;

    if (!strstartswith(real_path, dir->root)) {
        free(real_path);
        return -EINVAL;
    }

    path->used = 0;
    ret = sol_buffer_append_slice(path, sol_str_slice_from_str(real_path));
    free(real_path);

    SOL_INT_CHECK(ret, < 0, ret);

    /*  According with microhttpd fd will be closed when response is
     *  destroyed and fd should be in 'blocking' mode
     */
    return open(path->data, O_RDONLY | O_CLOEXEC);
}

static struct MHD_Response *
get_default_response(const struct sol_http_server *server, enum sol_http_status_code error)
{
    int r;
    uint16_t i;
    char buf[32];
    struct stat st;
    struct default_page *def;
    struct MHD_Response *response = NULL;

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        int fd;

        if (def->error != error)
            continue;

        fd = open(def->page, O_RDONLY | O_CLOEXEC);
        SOL_INT_CHECK(fd, < 0, NULL);

        r = fstat(fd, &st);
        if (r < 0) {
            close(fd);
            SOL_WRN("Failed to status the file: %s (%s)", def->page,
                sol_util_strerrora(errno));
            return NULL;
        }

        response = MHD_create_response_from_fd(st.st_size, fd);
        if (!response) {
            close(fd);
            SOL_WRN("Could not create the response with: %s", def->page);
            return NULL;
        }
        return response;
    }

    r = snprintf(buf, sizeof(buf), "status - %d", error);
    if (r < 0 || r >= (int)sizeof(buf)) {
        SOL_WRN("Could not set the status code on response body");
        response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    } else {
        response = MHD_create_response_from_buffer(r, buf, MHD_RESPMEM_MUST_COPY);
    }

    return response;
}

static enum sol_http_method
http_server_get_method(const char *method)
{
    static const struct sol_str_table table[] = {
        SOL_STR_TABLE_ITEM("POST",    SOL_HTTP_METHOD_POST),
        SOL_STR_TABLE_ITEM("GET",     SOL_HTTP_METHOD_GET),
        SOL_STR_TABLE_ITEM("HEAD",    SOL_HTTP_METHOD_HEAD),
        SOL_STR_TABLE_ITEM("PUT",     SOL_HTTP_METHOD_PUT),
        SOL_STR_TABLE_ITEM("TRACE",   SOL_HTTP_METHOD_TRACE),
        SOL_STR_TABLE_ITEM("DELETE",  SOL_HTTP_METHOD_DELETE),
        SOL_STR_TABLE_ITEM("OPTIONS", SOL_HTTP_METHOD_OPTIONS),
        SOL_STR_TABLE_ITEM("CONNECT", SOL_HTTP_METHOD_CONNECT),
        SOL_STR_TABLE_ITEM("PATCH",   SOL_HTTP_METHOD_PATCH),
        { }
    };

    return sol_str_table_lookup_fallback(table, sol_str_slice_from_str(method),
        SOL_HTTP_METHOD_INVALID);
}

static struct MHD_Response *
http_server_static_response(struct sol_http_server *server, struct sol_http_request *req,
    enum sol_http_status_code *status)
{
    int fd, r;
    uint16_t i;
    struct static_dir *dir;
    struct MHD_Response *response = NULL;

    SOL_BUFFER_DECLARE_STATIC(path, PATH_MAX);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        struct stat st;
        const char *mime;

        fd = get_static_file(dir, req->url, &path);
        if (fd < 0) {
            if (errno == EACCES) {
                *status = SOL_HTTP_STATUS_FORBIDDEN;
                sol_buffer_fini(&path);
                return NULL;
            }
            continue;
        }

        r = fstat(fd, &st);
        SOL_INT_CHECK_GOTO(r, < 0, err);
        if ((st.st_mode & READABLE_BY_EVERYONE) != READABLE_BY_EVERYONE) {
            *status = SOL_HTTP_STATUS_FORBIDDEN;
            goto err;
        }

        mime = get_file_mime_type(server, path.data);

        if (req->method == SOL_HTTP_METHOD_HEAD) {
            response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_MUST_COPY);
            SOL_NULL_CHECK_GOTO(response, err);

            close(fd);
        } else {
            response = MHD_create_response_from_fd(st.st_size, fd);
            SOL_NULL_CHECK_GOTO(response, err);
        }


        *status = SOL_HTTP_STATUS_OK;
        r = MHD_add_response_header(response,
            MHD_HTTP_HEADER_CONTENT_TYPE, mime);
        if (r < 0)
            SOL_WRN("Could not set the response content type to: %s. Error: %d", mime, r);
        else {
            SOL_DBG("Serving %s, path: %s, Content-type: %s, Content-Length: %zd",
                req->url, (char *)path.data, mime, (size_t)st.st_size);
        }
        sol_buffer_fini(&path);
        return response;
    }

    return response;

err:
    close(fd);
    return response;
}

static int
http_server_handler(void *data, struct MHD_Connection *connection, const char *url, const char *method,
    const char *version, const char *upload_data, size_t *upload_data_size, void **ptr)
{
    int ret;
    uint16_t i;
    char *path = NULL;
    struct MHD_Response *mhd_response = NULL;
    struct sol_http_server *server = data;
    struct http_handler *handler;
    struct sol_http_request *req = *ptr;
    enum sol_http_status_code status = SOL_HTTP_STATUS_NOT_FOUND;

    if (!req) {
        req = calloc(1, sizeof(struct sol_http_request));
        SOL_NULL_CHECK(req, MHD_NO);

        ret = sol_ptr_vector_append(&server->requests, req);
        if (ret < 0) {
            SOL_WRN("Could not append request for: %s", url);
            free(req);
            return MHD_NO;
        }

        sol_http_params_init(&req->params);
        req->url = url;
        sol_buffer_init(&req->buffer);
        req->connection = connection;
        *ptr = req;
        req->method = http_server_get_method(method);
        return MHD_YES;
    }

    MHD_get_connection_values(connection, MHD_HEADER_KIND, headers_iterator, req);
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, headers_iterator, req);
    MHD_get_connection_values(connection, MHD_COOKIE_KIND, headers_iterator, req);

    switch (req->method) {
    case SOL_HTTP_METHOD_POST:
        req->len += *upload_data_size;
        if (req->len > server->buf_size) {
            SOL_WRN("Request is bigger than buffer (%zu)", server->buf_size);
            status = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
            goto create_response;
        }

        if (!req->pp)
            req->pp = MHD_create_post_processor(connection, 1024, post_iterator, req);
        SOL_NULL_CHECK_GOTO(req->pp, end);

        if (MHD_post_process(req->pp, upload_data, *upload_data_size) == MHD_NO) {
            status = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
            goto create_response;
        }

        if (*upload_data_size) {
            *upload_data_size = 0;
            return MHD_YES;
        }
        if (req->buffer.used) {
            void *copy;
            size_t len;

            copy = sol_buffer_steal(&req->buffer, &len);
            req->param.value.data.value = SOL_STR_SLICE_STR(copy, len);
            if (sol_http_params_add(&req->params, req->param) < 0) {
                SOL_WRN("Could not add %.*s value",
                    SOL_STR_SLICE_PRINT(req->param.value.data.value));
                return MHD_NO;
            }
            memset(&req->param, 0, sizeof(req->param));
        }
        break;
    case SOL_HTTP_METHOD_GET:
    case SOL_HTTP_METHOD_HEAD:
        break;
    default:
        SOL_WRN("Method %s not implemented", method ? method : "NULL");
        status = SOL_HTTP_STATUS_NOT_IMPLEMENTED;
        goto create_response;
    }

    path = sanitize_path(url);
    if (!path) {
        status = SOL_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        goto create_response;
    }

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (!sol_str_slice_str_eq(handler->path, path))
            continue;

        free(path);
        if (handler->last_modified && (req->if_since_modified >= handler->last_modified)) {
            status = SOL_HTTP_STATUS_NOT_MODIFIED;
            goto create_response;
        } else {
            MHD_suspend_connection(connection);
            req->suspended = true;
            req->last_modified = handler->last_modified;
            ret = handler->request_cb((void *)handler->user_data, req);
            SOL_INT_CHECK(ret, < 0, MHD_NO);
        }

        return MHD_YES;
    }

    free(path);
    mhd_response = http_server_static_response(server, req, &status);
    if (status != SOL_HTTP_STATUS_NOT_FOUND)
        goto end;

create_response:
    mhd_response = get_default_response(server, status);
    SOL_NULL_CHECK(mhd_response, MHD_NO);
end:
    ret = MHD_queue_response(connection, status, mhd_response);
    MHD_destroy_response(mhd_response);
    return ret;
}

static bool
connection_watch_cb(void *data, int fd, uint32_t flags)
{
    uint16_t i;
    fd_set rs, ws, es;
    struct sol_http_server *server = data;
    struct http_connection *connection;

    SOL_NULL_CHECK(server, false);

    FD_ZERO(&rs);
    FD_ZERO(&ws);
    FD_ZERO(&es);

    if (flags & SOL_FD_FLAGS_IN)
        FD_SET(fd, &rs);
    if (flags & SOL_FD_FLAGS_OUT)
        FD_SET(fd, &ws);
    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP))
        FD_SET(fd, &es);

    /* This is far from optimized, the mainloop is given one file descriptor
       per time while it's possible to pass all the file descriptors with events
       to MHD at once. */
    if (MHD_run_from_select(server->daemon, &rs, &ws, &es) == MHD_NO)
        SOL_WRN("Something wrong happened inside microhttpd");

    if (flags & (SOL_FD_FLAGS_ERR | SOL_FD_FLAGS_NVAL | SOL_FD_FLAGS_HUP)) {
        SOL_VECTOR_FOREACH_IDX (&server->fds, connection, i) {
            if (connection->fd == fd) {
                sol_vector_del(&server->fds, i);
                break;
            }
        }

        return false;
    }

    return true;
}

static void
notify_connection_cb(void *data, struct MHD_Connection *connection, void **socket_data,
    enum MHD_ConnectionNotificationCode code)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    const union MHD_ConnectionInfo *info;
    struct sol_http_server *server = data;
    struct http_connection *conn;

    if (code != MHD_CONNECTION_NOTIFY_STARTED)
        return;

    info = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    SOL_NULL_CHECK(info);

    conn = sol_vector_append(&server->fds);
    SOL_NULL_CHECK(conn);

    conn->fd = (int)info->connect_fd;
    conn->watch = sol_fd_add(conn->fd, fd_flags, connection_watch_cb, server);

    if (!conn->watch) {
        SOL_WRN("Could not watch file descriptor: %s", sol_util_strerrora(errno));
        sol_vector_del_last(&server->fds);
    }
}

static void
free_request(struct sol_http_request *request)
{
    uint16_t idx;
    struct sol_http_param_value *param;

    if (request->pp)
        MHD_destroy_post_processor(request->pp);

    SOL_HTTP_PARAMS_FOREACH_IDX (&request->params, param, idx) {
        switch (param->type) {
        case SOL_HTTP_PARAM_POST_DATA:
            free((char *)param->value.data.filename.data);
            /* fall through */
        case SOL_HTTP_PARAM_POST_FIELD:
            free((char *)param->value.key_value.value.data);
            free((char *)param->value.key_value.key.data);
            break;
        default:
            break;
        }
    }

    free((char *)request->param.value.data.value.data);
    free((char *)request->param.value.data.key.data);
    if (request->param.type == SOL_HTTP_PARAM_POST_DATA)
        free((char *)request->param.value.data.filename.data);

    sol_http_params_clear(&request->params);
    sol_buffer_fini(&request->buffer);
    free(request);
}

static void
notify_connection_finished_cb(void *data, struct MHD_Connection *connection,
    void **con_data, enum MHD_RequestTerminationCode code)
{
    struct sol_http_server *server = data;
    struct sol_http_request *request = *con_data;

    SOL_NULL_CHECK(request);

    sol_ptr_vector_remove(&server->requests, request);
    free_request(request);
}

SOL_API struct sol_http_server *
sol_http_server_new(const struct sol_http_server_config *config)
{
    static const enum sol_fd_flags fd_flags =
        SOL_FD_FLAGS_IN | SOL_FD_FLAGS_OUT | SOL_FD_FLAGS_ERR |
        SOL_FD_FLAGS_HUP | SOL_FD_FLAGS_NVAL;
    const union MHD_DaemonInfo *info;
    struct http_connection *conn;
    struct sol_http_server *server;

    SOL_NULL_CHECK(config, NULL);
    SOL_HTTP_SERVER_CONFIG_CHECK_API_VERSION(config, NULL);

    server = calloc(1, sizeof(*server));
    SOL_NULL_CHECK(server, NULL);

    sol_vector_init(&server->handlers, sizeof(struct http_handler));
    sol_vector_init(&server->fds, sizeof(struct http_connection));
    sol_vector_init(&server->dirs, sizeof(struct static_dir));
    sol_vector_init(&server->defaults, sizeof(struct default_page));
    sol_ptr_vector_init(&server->requests);

    server->buf_size = SOL_HTTP_REQUEST_BUFFER_SIZE;

    if (config->security.cert && config->security.key) {
        struct sol_blob *cert_contents, *key_contents;

        cert_contents = sol_cert_get_contents(config->security.cert);
        if (!cert_contents) {
            SOL_WRN("Could not get the certificate contents");
            goto err_daemon;
        }

        key_contents = sol_cert_get_contents(config->security.key);
        if (!key_contents) {
            SOL_WRN("Could not get the certificate key contents");
            sol_blob_unref(cert_contents);
            goto err_daemon;
        }

        server->daemon = MHD_start_daemon(MHD_USE_SUSPEND_RESUME | MHD_USE_SSL,
            config->port, NULL, NULL,
            http_server_handler, server,
            MHD_OPTION_NOTIFY_CONNECTION, notify_connection_cb, server,
            MHD_OPTION_NOTIFY_COMPLETED, notify_connection_finished_cb, server,
            MHD_OPTION_HTTPS_MEM_KEY, (char *)key_contents->mem,
            MHD_OPTION_HTTPS_MEM_CERT, (char *)cert_contents->mem,
            MHD_OPTION_END);
        sol_blob_unref(cert_contents);
        sol_blob_unref(key_contents);
    } else {
        server->daemon = MHD_start_daemon(MHD_USE_SUSPEND_RESUME,
            config->port, NULL, NULL,
            http_server_handler, server,
            MHD_OPTION_NOTIFY_CONNECTION, notify_connection_cb, server,
            MHD_OPTION_NOTIFY_COMPLETED, notify_connection_finished_cb, server,
            MHD_OPTION_END);
    }
    SOL_NULL_CHECK_GOTO(server->daemon, err_daemon);

    info = MHD_get_daemon_info(server->daemon, MHD_DAEMON_INFO_LISTEN_FD);
    SOL_NULL_CHECK_GOTO(info, err);

    conn = sol_vector_append(&server->fds);
    SOL_NULL_CHECK_GOTO(conn, err);

    conn->fd = (int)info->listen_fd;
    conn->watch = sol_fd_add(conn->fd, fd_flags, connection_watch_cb, server);
    SOL_NULL_CHECK_GOTO(conn->watch, err);

    return server;

err:
    MHD_stop_daemon(server->daemon);
err_daemon:
    sol_vector_clear(&server->handlers);
    sol_vector_clear(&server->fds);
    sol_vector_clear(&server->dirs);
    sol_ptr_vector_clear(&server->requests);
    free(server);
    return NULL;
}

SOL_API void
sol_http_server_del(struct sol_http_server *server)
{
    uint16_t i;
    struct static_dir *dir;
    struct default_page *def;
    struct http_handler *handler;
    struct http_connection *connection;
    struct sol_http_request *request;

    SOL_NULL_CHECK(server);

    SOL_PTR_VECTOR_FOREACH_IDX (&server->requests, request, i) {
        if (request->suspended)
            MHD_resume_connection(request->connection);
    }
    sol_ptr_vector_clear(&server->requests);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i)
        free((char *)handler->path.data);
    sol_vector_clear(&server->handlers);

    SOL_VECTOR_FOREACH_IDX (&server->fds, connection, i)
        sol_fd_del(connection->watch);
    sol_vector_clear(&server->fds);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        free(dir->root);
        free(dir->basename);
    }
    sol_vector_clear(&server->dirs);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i)
        free(def->page);
    sol_vector_clear(&server->defaults);

    MHD_stop_daemon(server->daemon);

    free(server);
}

SOL_API int
sol_http_server_register_handler(struct sol_http_server *server, const char *path,
    int (*request_cb)(void *data, struct sol_http_request *request),
    const void *data)
{
    uint16_t i;
    char *p;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);
    SOL_NULL_CHECK(request_cb, -EINVAL);

    p = sanitize_path(path);
    SOL_NULL_CHECK(p, -ENOMEM);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (sol_str_slice_str_eq(handler->path, p)) {
            free(p);
            return -EINVAL;
        }
    }

    handler = sol_vector_append(&server->handlers);
    SOL_NULL_CHECK_GOTO(handler, err);

    SOL_NULL_CHECK_GOTO(p, error);
    handler->path = sol_str_slice_from_str(p);

    handler->request_cb = request_cb;
    handler->user_data = data;
    handler->last_modified = 0;

    return 0;

error:
    sol_vector_del_last(&server->handlers);
err:
    free(p);
    return -ENOMEM;
}

SOL_API int
sol_http_server_unregister_handler(struct sol_http_server *server, const char *path)
{
    uint16_t i;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, i) {
        if (sol_str_slice_str_eq(handler->path, path)) {
            free((char *)handler->path.data);
            sol_vector_del(&server->handlers, i);
            return 0;
        }
    }

    return -ENOENT;
}

SOL_API int
sol_http_server_send_response(struct sol_http_request *request, struct sol_http_response *response)
{
    int ret;
    struct MHD_Response *mhd_response;

    SOL_NULL_CHECK(request, -EINVAL);
    SOL_NULL_CHECK(request->connection, -EINVAL);
    SOL_NULL_CHECK(response, -EINVAL);

    SOL_HTTP_RESPONSE_CHECK_API_VERSION(response, -EINVAL);

    if (request->suspended) {
        MHD_resume_connection(request->connection);
        request->suspended = false;
    }

    mhd_response = build_mhd_response(response, request->last_modified);
    SOL_NULL_CHECK(mhd_response, -1);

    ret = MHD_queue_response(request->connection, response->response_code, mhd_response);
    MHD_destroy_response(mhd_response);

    SOL_INT_CHECK(ret, != MHD_YES, -1);

    return ret;
}

SOL_API struct sol_http_progressive_response *
sol_http_server_send_progressive_response(struct sol_http_request *request,
    const struct sol_http_response *response, const struct sol_http_server_progressive_config *config)
{
    int ret;
    struct sol_http_progressive_response *progressive;
    struct MHD_Response *mhd_response;

    SOL_NULL_CHECK(request, NULL);
    SOL_NULL_CHECK(request->connection, NULL);
    SOL_NULL_CHECK(response, NULL);
    SOL_NULL_CHECK(config, NULL);

#ifndef SOL_NO_API_VERSION
    if (config->api_version != SOL_HTTP_SERVER_PROGRESSIVE_CONFIG_API_VERSION) {
        SOL_WRN("Incorrect API version for struct sol_http_server_progressive_config."
            "Expected '%u' - Received: '%u'", SOL_HTTP_SERVER_PROGRESSIVE_CONFIG_API_VERSION, config->api_version);
        return 0;
    }
#endif

    progressive = calloc(1, sizeof(*progressive));
    SOL_NULL_CHECK(progressive, NULL);

    sol_ptr_vector_init(&progressive->pending_blobs);
    progressive->request = request;

    if (request->suspended) {
        MHD_resume_connection(request->connection);
        request->suspended = false;
    }

    mhd_response = build_mhd_progressive_response(response, progressive);
    SOL_NULL_CHECK_GOTO(mhd_response, err);

    ret = MHD_queue_response(request->connection,
        response->response_code, mhd_response);
    MHD_destroy_response(mhd_response);

    SOL_INT_CHECK_GOTO(ret, != MHD_YES, err);

    progressive->on_close = config->on_close;
    progressive->cb_data = config->user_data;
    progressive->on_feed_done = config->on_feed_done;
    progressive->feed_size = config->feed_size;

    return progressive;

err:
    MHD_destroy_response(mhd_response);
    free(progressive);
    return NULL;
}

SOL_API void
sol_http_progressive_response_del(struct sol_http_progressive_response *progressive, bool graceful_del)
{
    SOL_NULL_CHECK(progressive);
    SOL_EXP_CHECK(progressive->delete_me == true);

    progressive->graceful_del = graceful_del;
    progressive->delete_me = true;

    if (progressive->request->suspended) {
        MHD_resume_connection(progressive->request->connection);
        progressive->request->suspended = false;
    }
}

static int
queue_blob(struct sol_http_progressive_response *progressive,
    struct sol_blob *blob)
{
    int ret;
    size_t total;

    ret = sol_util_size_add(progressive->accumulated_bytes, blob->size, &total);
    SOL_INT_CHECK(ret, < 0, ret);

    if (progressive->feed_size && total >= progressive->feed_size)
        return -ENOSPC;

    ret = sol_ptr_vector_append(&progressive->pending_blobs, blob);
    SOL_INT_CHECK(ret, < 0, ret);

    sol_blob_ref(blob);

    if (progressive->request->suspended) {
        progressive->request->suspended = false;
        MHD_resume_connection(progressive->request->connection);
    }

    progressive->accumulated_bytes = total;

    return 0;
}

SOL_API int
sol_http_progressive_response_feed(struct sol_http_progressive_response *progressive,
    struct sol_blob *blob)
{
    SOL_NULL_CHECK(progressive, -EINVAL);
    SOL_EXP_CHECK(progressive->delete_me == true, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    return queue_blob(progressive, blob);
}

SOL_API int
sol_http_progressive_response_sse_feed(struct sol_http_progressive_response *progressive,
    struct sol_blob *blob)
{
    int r;
    bool suspended;

    SOL_NULL_CHECK(progressive, -EINVAL);
    SOL_EXP_CHECK(progressive->delete_me == true, -EINVAL);
    SOL_NULL_CHECK(blob, -EINVAL);

    suspended = progressive->request->suspended;

    r = queue_blob(progressive, &sse_prefix);
    SOL_INT_CHECK(r, < 0, r);

    r = queue_blob(progressive, blob);
    SOL_INT_CHECK_GOTO(r, < 0, err_data);

    r = queue_blob(progressive, &sse_suffix);
    SOL_INT_CHECK_GOTO(r, < 0, err_suffix);

    return 0;

err_suffix:
    progressive->accumulated_bytes -= blob->size;
    sol_ptr_vector_del_last(&progressive->pending_blobs);
    sol_blob_unref(blob);

err_data:
    progressive->accumulated_bytes -= sse_prefix.size;
    sol_ptr_vector_del_last(&progressive->pending_blobs);
    sol_blob_unref(&sse_prefix);

    if (!suspended) {
        progressive->request->suspended = false;
        MHD_resume_connection(progressive->request->connection);
    }

    return r;
}

SOL_API int
sol_http_server_set_last_modified(struct sol_http_server *server, const char *path, time_t modified)
{
    uint16_t idx;
    struct http_handler *handler;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(path, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->handlers, handler, idx) {
        if (sol_str_slice_str_eq(handler->path, path)) {
            handler->last_modified = modified;
            return 0;
        }
    }

    return -ENODATA;
}

SOL_API int
sol_http_server_add_dir(struct sol_http_server *server, const char *basename, const char *rootdir)
{
    uint16_t i;
    char *p;
    struct static_dir *dir;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(rootdir, -EINVAL);
    SOL_NULL_CHECK(basename, -EINVAL);

    p = sanitize_path(basename);
    SOL_NULL_CHECK(p, -ENOMEM);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        if (streq(dir->root, rootdir) &&
            streq(dir->basename, p)) {
            free(p);
            return -EINVAL;
        }
    }

    dir = sol_vector_append(&server->dirs);
    SOL_NULL_CHECK_GOTO(dir, err);

    dir->basename_len = strlen(p);
    dir->basename = p;
    dir->root = realpath(rootdir, NULL);
    SOL_NULL_CHECK_GOTO(dir->root, err_path);

    return 0;

err_path:
    if (sol_vector_del_last(&server->dirs) < 0)
        SOL_WRN("Could not remove %s/%s correctly",
            basename, rootdir);
err:
    free(p);
    return -ENOMEM;
}

SOL_API int
sol_http_server_remove_dir(struct sol_http_server *server, const char *basename, const char *rootdir)
{
    int r = -ENOMEM;
    uint16_t i;
    char *root = NULL, *p = NULL;
    struct static_dir *dir;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(rootdir, -EINVAL);
    SOL_NULL_CHECK(basename, -EINVAL);

    p = sanitize_path(basename);
    SOL_NULL_CHECK(p, -ENOMEM);

    root = realpath(rootdir, NULL);
    SOL_NULL_CHECK_GOTO(root, end);

    SOL_VECTOR_FOREACH_IDX (&server->dirs, dir, i) {
        if (streq(dir->root, root) &&
            streq(dir->basename, p)) {
            free(dir->root);
            free(dir->basename);
            sol_vector_del(&server->dirs, i);
            r = 0;
            goto end;
        }
    }

    r = -ENODATA;
end:
    free(root);
    free(p);
    return r;
}

static int
get_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address, bool self)
{
    int r = 0;
    const union MHD_ConnectionInfo *info;

    union {
        struct sockaddr_in6 in6;
        struct sockaddr_in in4;
    } addr;
    socklen_t addrlen = sizeof(addr);

    SOL_NULL_CHECK(request, -EINVAL);
    SOL_NULL_CHECK(address, -EINVAL);

    info = MHD_get_connection_info(request->connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    SOL_NULL_CHECK(info, -EINVAL);

    if (self)
        r = getsockname(info->connect_fd, (struct sockaddr *)&addr, &addrlen);
    else
        r = getpeername(info->connect_fd, (struct sockaddr *)&addr, &addrlen);

    if (r < 0 || addrlen > sizeof(addr)) {
        SOL_WRN("Could not get the address for request: %s", request->url);
        return -EINVAL;
    }

    address->family = sol_network_af_to_sol(((struct sockaddr *)&addr)->sa_family);
    switch (address->family) {
    case SOL_NETWORK_FAMILY_INET:
        address->port = ntohs(addr.in4.sin_port);
        memcpy(&(address->addr.in), &addr.in4.sin_addr, sizeof(addr.in4.sin_addr));
        break;
    case SOL_NETWORK_FAMILY_INET6:
        address->port = ntohs(addr.in6.sin6_port);
        memcpy(&(address->addr.in6), &addr.in6.sin6_addr, sizeof(addr.in6.sin6_addr));
        break;
    default:
        SOL_WRN("Unsupported family for request: %s", request->url);
        r = -EINVAL;
        break;
    }

    return r;
}

SOL_API int
sol_http_request_get_interface_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address)
{
    return get_address(request, address, true);
}

SOL_API int
sol_http_request_get_client_address(const struct sol_http_request *request,
    struct sol_network_link_addr *address)
{
    return get_address(request, address, false);
}

SOL_API int
sol_http_server_set_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error, const char *page)
{
    int r;
    uint16_t i;
    char *p;
    struct default_page *def;

    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(page, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        if (def->error == error) {
            SOL_WRN("There a page already registered for this error (%d)", error);
            return -EINVAL;
        }
    }

    p = realpath(page, NULL);
    SOL_NULL_CHECK(p, -errno);

    r = access(p, R_OK);
    if (r < 0) {
        r = -errno;
        SOL_WRN("Error on check file's permission: %s", sol_util_strerrora(errno));
        goto err;
    }

    r = -ENOMEM;
    def = sol_vector_append(&server->defaults);
    SOL_NULL_CHECK_GOTO(def, err);

    def->page = p;
    def->error = error;

    return 0;

err:
    free(p);
    return r;
}

SOL_API int
sol_http_server_remove_error_page(struct sol_http_server *server,
    const enum sol_http_status_code error)
{
    uint16_t i;
    struct default_page *def;

    SOL_NULL_CHECK(server, -EINVAL);

    SOL_VECTOR_FOREACH_IDX (&server->defaults, def, i) {
        if (def->error == error) {
            free(def->page);
            sol_vector_del(&server->defaults, i);
            return 0;
        }
    }

    return -ENODATA;
}

SOL_API int
sol_http_server_set_buffer_size(struct sol_http_server *server, size_t buf_size)
{
    SOL_NULL_CHECK(server, -EINVAL);

    server->buf_size = buf_size;
    return 0;
}

SOL_API int
sol_http_server_get_buffer_size(struct sol_http_server *server, size_t *buf_size)
{
    SOL_NULL_CHECK(server, -EINVAL);
    SOL_NULL_CHECK(buf_size, -EINVAL);

    *buf_size = server->buf_size;

    return 0;
}
