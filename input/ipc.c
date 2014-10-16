/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "config.h"

#include "osdep/io.h"

#include "common/common.h"
#include "common/msg.h"
#include "input/input.h"
#include "libmpv/client.h"
#include "misc/bstr.h"
#include "misc/json.h"
#include "options/m_option.h"
#include "options/path.h"
#include "player/core.h"
#include "player/client.h"

struct thread_arg {
    struct MPContext *mpctx;
    const char *path;

    pthread_t thread;
    int death_pipe[2];
};

struct client_arg {
    struct mp_log *log;
    struct mpv_handle *client;

    int client_fd;

    char *(*encode_event)(mpv_event *event);
    char *(*execute_command)(struct client_arg *arg, bstr msg);
};

static mpv_node *mpv_node_map_get(mpv_node *src, const char *key)
{
    if (src->format != MPV_FORMAT_NODE_MAP)
        return NULL;

    for (int i = 0; i < src->u.list->num; i++)
        if (!strcmp(key, src->u.list->keys[i]))
            return &src->u.list->values[i];

    return NULL;
}

static mpv_node *mpv_node_array_get(mpv_node *src, int index)
{
    if (src->format != MPV_FORMAT_NODE_ARRAY)
        return NULL;

    if (src->u.list->num < (index + 1))
        return NULL;

    return &src->u.list->values[index];
}

static void mpv_node_array_add(void *ta_parent, mpv_node *src,  mpv_node *val)
{
    if (src->format != MPV_FORMAT_NODE_ARRAY)
        return;

    if (!src->u.list)
        src->u.list = talloc_zero(ta_parent, mpv_node_list);

    MP_TARRAY_GROW(src->u.list, src->u.list->values, src->u.list->num);

    static const struct m_option type = { .type = CONF_TYPE_NODE };
    m_option_get_node(&type, ta_parent, &src->u.list->values[src->u.list->num], val);

    src->u.list->num++;
}

static void mpv_node_array_add_string(void *ta_parent, mpv_node *src, const char *val)
{
    mpv_node val_node = {.format = MPV_FORMAT_STRING, .u.string = (char *)val};
    mpv_node_array_add(ta_parent, src, &val_node);
}

static void mpv_node_map_add(void *ta_parent, mpv_node *src, const char *key, mpv_node *val)
{
    if (src->format != MPV_FORMAT_NODE_MAP)
        return;

    if (!src->u.list)
        src->u.list = talloc_zero(ta_parent, mpv_node_list);

    MP_TARRAY_GROW(src->u.list, src->u.list->keys, src->u.list->num);
    MP_TARRAY_GROW(src->u.list, src->u.list->values, src->u.list->num);

    src->u.list->keys[src->u.list->num] = talloc_strdup(ta_parent, key);

    static const struct m_option type = { .type = CONF_TYPE_NODE };
    m_option_get_node(&type, ta_parent, &src->u.list->values[src->u.list->num], val);

    src->u.list->num++;
}

static void mpv_node_map_add_null(void *ta_parent, mpv_node *src, const char *key)
{
    mpv_node val_node = {.format = MPV_FORMAT_NONE};
    mpv_node_map_add(ta_parent, src, key, &val_node);
}

static void mpv_node_map_add_flag(void *ta_parent, mpv_node *src, const char *key, bool val)
{
    mpv_node val_node = {.format = MPV_FORMAT_FLAG, .u.flag = val};

    mpv_node_map_add(ta_parent, src, key, &val_node);
}

static void mpv_node_map_add_int64(void *ta_parent, mpv_node *src, const char *key, int64_t val)
{
    mpv_node val_node = {.format = MPV_FORMAT_INT64, .u.int64 = val};
    mpv_node_map_add(ta_parent, src, key, &val_node);
}

static void mpv_node_map_add_double(void *ta_parent, mpv_node *src, const char *key, double val)
{
    mpv_node val_node = {.format = MPV_FORMAT_DOUBLE, .u.double_ = val};
    mpv_node_map_add(ta_parent, src, key, &val_node);
}

static void mpv_node_map_add_string(void *ta_parent, mpv_node *src, const char *key, const char *val)
{
    mpv_node val_node = {.format = MPV_FORMAT_STRING, .u.string = (char*)val};
    mpv_node_map_add(ta_parent, src, key, &val_node);
}

static void mpv_event_to_node(void *ta_parent, mpv_event *event, mpv_node *dst)
{
    mpv_node_map_add_string(ta_parent, dst, "event", mpv_event_name(event->event_id));

    if (event->reply_userdata)
        mpv_node_map_add_int64(ta_parent, dst, "id", event->reply_userdata);

    if (event->error < 0)
        mpv_node_map_add_string(ta_parent, dst, "error", mpv_error_string(event->error));

    switch (event->event_id) {
    case MPV_EVENT_LOG_MESSAGE: {
        mpv_event_log_message *msg = event->data;

        mpv_node_map_add_string(ta_parent, dst, "prefix", msg->prefix);
        mpv_node_map_add_string(ta_parent, dst, "level",  msg->level);
        mpv_node_map_add_string(ta_parent, dst, "text",   msg->text);

        break;
    }

    case MPV_EVENT_SCRIPT_INPUT_DISPATCH: {
        mpv_event_script_input_dispatch *msg = event->data;

        mpv_node_map_add_int64(ta_parent, dst, "arg0", msg->arg0);
        mpv_node_map_add_string(ta_parent, dst, "type", msg->type);

        break;
    }

    case MPV_EVENT_CLIENT_MESSAGE: {
        mpv_event_client_message *msg = event->data;

        mpv_node args_node = {.format = MPV_FORMAT_NODE_ARRAY, .u.list = NULL};
        for (int n = 0; n < msg->num_args; n++)
            mpv_node_array_add_string(ta_parent, &args_node, msg->args[n]);
        mpv_node_map_add(ta_parent, dst, "args", &args_node);
        break;
    }

    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = event->data;

        mpv_node_map_add_string(ta_parent, dst, "name", prop->name);

        switch (prop->format) {
        case MPV_FORMAT_NODE:
            mpv_node_map_add(ta_parent, dst, "data", prop->data);
            break;
        case MPV_FORMAT_DOUBLE:
            mpv_node_map_add_double(ta_parent, dst, "data", *(double *)prop->data);
            break;
        case MPV_FORMAT_FLAG:
            mpv_node_map_add_flag(ta_parent, dst, "data", *(int *)prop->data);
            break;
        case MPV_FORMAT_STRING:
            mpv_node_map_add_string(ta_parent, dst, "data", *(char **)prop->data);
            break;
        default:
            mpv_node_map_add_null(ta_parent, dst, "data");
        }
        break;
    }
    }
}

static char *json_encode_event(mpv_event *event)
{
    void *ta_parent = talloc_new(NULL);
    mpv_node event_node = {.format = MPV_FORMAT_NODE_MAP, .u.list = NULL};

    mpv_event_to_node(ta_parent, event, &event_node);

    char *output = talloc_strdup(NULL, "");
    json_write(&output, &event_node);
    output = ta_talloc_strdup_append(output, "\n");

    talloc_free(ta_parent);

    return output;
}

static char *json_execute_command(struct client_arg *arg, bstr msg)
{
    int rc;
    const char *cmd = NULL;

    mpv_node msg_node;
    mpv_node reply_node = {.format = MPV_FORMAT_NODE_MAP, .u.list = NULL};

    void *ta_parent = talloc_new(NULL);

    char *src = bstrdup0(ta_parent, msg);
    rc = json_parse(ta_parent, &msg_node, &src, 3);
    if (rc < 0) {
        rc = MPV_ERROR_INVALID_PARAMETER;
        goto error;
    }

    if (msg_node.format != MPV_FORMAT_NODE_MAP) {
        rc = MPV_ERROR_INVALID_PARAMETER;
        goto error;
    }

    mpv_node *cmd_node = mpv_node_map_get(&msg_node, "command");
    if (!cmd_node ||
        (cmd_node->format != MPV_FORMAT_NODE_ARRAY) ||
        !cmd_node->u.list->num)
    {
        rc = MPV_ERROR_INVALID_PARAMETER;
        goto error;
    }

    mpv_node *cmd_str_node = mpv_node_array_get(cmd_node, 0);
    if (!cmd_str_node || (cmd_str_node->format != MPV_FORMAT_STRING)) {
        rc = MPV_ERROR_INVALID_PARAMETER;
        goto error;
    }

    cmd = cmd_str_node->u.string;

    if (!strcmp("client_name", cmd)) {
        const char *client_name = mpv_client_name(arg->client);
        mpv_node_map_add_string(ta_parent, &reply_node, "data", client_name);
        rc = MPV_ERROR_SUCCESS;
    } else if (!strcmp("get_time_us", cmd)) {
        int64_t time_us = mpv_get_time_us(arg->client);
        mpv_node_map_add_int64(ta_parent, &reply_node, "data", time_us);
        rc = MPV_ERROR_SUCCESS;
    } else if (!strcmp("get_property", cmd)) {
        mpv_node result_node;

        if (cmd_node->u.list->num != 2) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_get_property(arg->client, cmd_node->u.list->values[1].u.string,
                              MPV_FORMAT_NODE, &result_node);
        if (rc >= 0) {
            mpv_node_map_add(ta_parent, &reply_node, "data", &result_node);
            mpv_free_node_contents(&result_node);
        }
    } else if (!strcmp("get_property_string", cmd)) {
        if (cmd_node->u.list->num != 2) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        char *result = mpv_get_property_string(arg->client,
                                        cmd_node->u.list->values[1].u.string);
        if (!result) {
            mpv_node_map_add_null(ta_parent, &reply_node, "data");
        } else {
            mpv_node_map_add_string(ta_parent, &reply_node, "data", result);
            mpv_free(result);
        }
    } else if (!strcmp("set_property", cmd)) {
        if (cmd_node->u.list->num != 3) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_set_property(arg->client, cmd_node->u.list->values[1].u.string,
                              MPV_FORMAT_NODE, &cmd_node->u.list->values[2]);
    } else if (!strcmp("set_property_string", cmd)) {
        if (cmd_node->u.list->num != 3) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[2].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_set_property_string(arg->client,
                                     cmd_node->u.list->values[1].u.string,
                                     cmd_node->u.list->values[2].u.string);
    } else if (!strcmp("observe_property", cmd)) {
        if (cmd_node->u.list->num != 3) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_INT64) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[2].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_observe_property(arg->client,
                                  cmd_node->u.list->values[1].u.int64,
                                  cmd_node->u.list->values[2].u.string,
                                  MPV_FORMAT_NODE);
    } else if (!strcmp("observe_property_string", cmd)) {
        if (cmd_node->u.list->num != 3) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_INT64) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[2].format != MPV_FORMAT_STRING) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_observe_property(arg->client,
                                  cmd_node->u.list->values[1].u.int64,
                                  cmd_node->u.list->values[2].u.string,
                                  MPV_FORMAT_STRING);
    } else if (!strcmp("unobserve_property", cmd)) {
        if (cmd_node->u.list->num != 2) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        if (cmd_node->u.list->values[1].format != MPV_FORMAT_INT64) {
            rc = MPV_ERROR_INVALID_PARAMETER;
            goto error;
        }

        rc = mpv_unobserve_property(arg->client,
                                  cmd_node->u.list->values[1].u.int64);
    } else if (!strcmp("suspend", cmd)) {
        mpv_suspend(arg->client);
        rc = MPV_ERROR_SUCCESS;
    } else if (!strcmp("resume", cmd)) {
        mpv_resume(arg->client);
        rc = MPV_ERROR_SUCCESS;
    } else {
        mpv_node result_node;

        rc = mpv_command_node(arg->client, cmd_node, &result_node);
        if (rc >= 0)
            mpv_node_map_add(ta_parent, &reply_node, "data", &result_node);
    }

error:
    mpv_node_map_add_string(ta_parent, &reply_node, "error", mpv_error_string(rc));

    char *output = talloc_strdup(NULL, "");
    json_write(&output, &reply_node);
    output = ta_talloc_strdup_append(output, "\n");

    talloc_free(ta_parent);

    return output;
}

static int ipc_write(int fd, const char *buf, size_t count)
{
    while (count > 0) {
        ssize_t rc = write(fd, buf, count);
        if (rc <= 0) {
            if (rc == 0)
                return ECONNRESET;

            if (rc == EINTR)
                continue;

            if (rc == EAGAIN)
                return 0;

            return rc;
        }

        count -= rc;
        buf   += rc;
    }

    return 0;
}

static void *client_thread(void *p)
{
    pthread_detach(pthread_self());

    int rc;

    struct client_arg *arg = p;

    int pipe_fd = mpv_get_wakeup_pipe(arg->client);
    if (pipe_fd < 0) {
        MP_ERR(arg, "Could not get wakeup pipe\n");
        goto done;
    }

    MP_INFO(arg, "Client connected\n");

    struct pollfd fds[2] = {
        {.events = POLLIN, .fd = pipe_fd},
        {.events = POLLIN, .fd = arg->client_fd},
    };

    fcntl(arg->client_fd, F_SETFL, fcntl(arg->client_fd, F_GETFL, 0) | O_NONBLOCK);

    bstr client_msg = { talloc_strdup(NULL, ""), 0 };
    while (1) {
        rc = poll(fds, 2, -1);
        if (rc < 0) {
            MP_ERR(arg, "Poll error\n");
            continue;
        }

        if (fds[0].revents & POLLIN) {
            char discard[100];
            read(pipe_fd, discard, sizeof(discard));

            while (1) {
                mpv_event *event = mpv_wait_event(arg->client, 0);

                if (event->event_id == MPV_EVENT_NONE)
                    break;

                if (event->event_id == MPV_EVENT_SHUTDOWN)
                    goto done;

                if (!arg->encode_event)
                    continue;

                char *event_msg = arg->encode_event(event);
                if (!event_msg) {
                    MP_ERR(arg, "Encoding error\n");
                    goto done;
                }

                rc = ipc_write(arg->client_fd, event_msg, strlen(event_msg));
                talloc_free(event_msg);
                if (rc < 0) {
                    MP_ERR(arg, "Write error\n");
                    goto done;
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            while (1) {
                char buf[128];
                bstr append = { buf, 0 };

                ssize_t bytes = read(arg->client_fd, buf, sizeof(buf));
                if (bytes < 0) {
                    if (errno == EAGAIN)
                        break;

                    MP_ERR(arg, "Read error\n");
                    goto done;
                }

                if (bytes == 0) {
                    MP_INFO(arg, "Client disconnected\n");
                    goto done;
                }

                append.len = bytes;

                bstr_xappend(NULL, &client_msg, append);

                while (bstrchr(client_msg, '\n') != -1) {
                    bstr rest, tmp;
                    bstr line = bstr_getline(client_msg, &rest);

                    if (arg->execute_command) {
                        char *reply_msg = arg->execute_command(arg, line);
                        if (!reply_msg)
                            goto command_done;

                        rc = ipc_write(arg->client_fd, reply_msg,
                                       strlen(reply_msg));
                        talloc_free(reply_msg);
                        if (rc < 0) {
                            MP_ERR(arg, "Write error\n");
                            talloc_free(client_msg.start);
                            goto done;
                        }
                    }

command_done:
                    tmp = bstrdup(NULL, rest);
                    talloc_free(client_msg.start);
                    client_msg = tmp;
                }
            }
        }
    }

done:
    close(arg->client_fd);
    mpv_detach_destroy(arg->client);
    talloc_free(arg);
    return NULL;
}

static void ipc_start_client(struct MPContext *mpctx, int id, int fd)
{
    struct client_arg *client = talloc_ptrtype(NULL, client);
    char *client_name = talloc_asprintf(client, "ipc-%d", id);
    *client = (struct client_arg){
        .client    = mp_new_client(mpctx->clients, client_name),
        .client_fd = fd,

        .encode_event    = json_encode_event,
        .execute_command = json_execute_command,
    };

    client->log = mp_client_get_log(client->client);

    pthread_t client_thr;
    if (pthread_create(&client_thr, NULL, client_thread, client)) {
        mpv_detach_destroy(client->client);
        close(client->client_fd);
        talloc_free(client);
    }
}

static void *ipc_thread(void *p)
{
    int rc;

    int ipc_fd;
    struct sockaddr_un ipc_un;

    struct thread_arg *arg = p;

    MP_INFO(arg->mpctx, "Starting IPC master\n");

    ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_fd < 0) {
        MP_ERR(arg->mpctx, "Could not create IPC socket\n");
        goto done;
    }

    size_t path_len = strlen(arg->path);
    if (path_len >= sizeof(ipc_un.sun_path) - 1) {
        MP_ERR(arg->mpctx, "Could not create IPC socket\n");
        goto done;
    }

    ipc_un.sun_family = AF_UNIX,
    strncpy(ipc_un.sun_path, arg->path, sizeof(ipc_un.sun_path));

    unlink(ipc_un.sun_path);

    if (ipc_un.sun_path[0] == '@') {
        ipc_un.sun_path[0] = '\0';
        path_len--;
    }

    size_t addr_len = offsetof(struct sockaddr_un, sun_path) + 1 + path_len;
    rc = bind(ipc_fd, (struct sockaddr *) &ipc_un, addr_len);
    if (rc < 0) {
        MP_ERR(arg->mpctx, "Could not bind IPC socket\n");
        goto done;
    }

    rc = listen(ipc_fd, 10);
    if (rc < 0) {
        MP_ERR(arg->mpctx, "Could not listen on IPC socket\n");
        goto done;
    }

    int client_num = 0;

    struct pollfd fds[2] = {
        {.events = POLLIN, .fd = arg->death_pipe[0]},
        {.events = POLLIN, .fd = ipc_fd},
    };

    while (1) {
        rc = poll(fds, 2, -1);
        if (rc < 0) {
            MP_ERR(arg->mpctx, "Poll error\n");
            continue;
        }

        if (fds[0].revents & POLLIN)
            goto done;

        if (fds[1].revents & POLLIN) {
            int client_fd = accept(ipc_fd, NULL, NULL);
            if (client_fd < 0) {
                MP_ERR(arg->mpctx, "Could not accept IPC client\n");
                goto done;
            }

            ipc_start_client(arg->mpctx, client_num++, client_fd);
        }
    }

done:
    if (ipc_fd >= 0)
        close(ipc_fd);

    close(arg->death_pipe[0]);
    arg->death_pipe[0] = -1;
    close(arg->death_pipe[1]);
    arg->death_pipe[1] = -1;
    arg->mpctx->ipc_ctx = NULL;

    talloc_free(arg);
    return NULL;
}

void mp_init_ipc(struct MPContext *mpctx)
{
    if (!mpctx->opts->ipc_path || !*mpctx->opts->ipc_path)
        return;

    struct thread_arg *arg = talloc_ptrtype(NULL, arg);
    *arg = (struct thread_arg){
        .mpctx  = mpctx,
        .path   = mp_get_user_path(arg, mpctx->global, mpctx->opts->ipc_path),
    };

    if (mp_make_wakeup_pipe(arg->death_pipe) < 0) {
        talloc_free(arg);
        return;
    }

    mpctx->ipc_ctx = arg;

    if (pthread_create(&arg->thread, NULL, ipc_thread, arg))
        talloc_free(arg);
}

void mp_uninit_ipc(struct MPContext *mpctx)
{
    struct thread_arg *arg = mpctx->ipc_ctx;

    if (!arg)
        return;

    if (arg->death_pipe[1] != -1)
        write(arg->death_pipe[1], &(char){0}, 1);
    pthread_join(arg->thread, NULL);

    mpctx->ipc_ctx = NULL;
}
