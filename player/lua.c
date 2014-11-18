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

#include <assert.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>

#include "osdep/io.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "talloc.h"

#include "common/common.h"
#include "options/m_property.h"
#include "common/msg.h"
#include "common/msg_control.h"
#include "options/m_option.h"
#include "input/input.h"
#include "options/path.h"
#include "misc/bstr.h"
#include "misc/json.h"
#include "osdep/timer.h"
#include "osdep/threads.h"
#include "stream/stream.h"
#include "sub/osd.h"
#include "core.h"
#include "command.h"
#include "client.h"
#include "libmpv/client.h"

// List of builtin modules and their contents as strings.
// All these are generated from player/lua/*.lua
static const char * const builtin_lua_scripts[][2] = {
    {"mp.defaults",
#   include "player/lua/defaults.inc"
    },
    {"mp.assdraw",
#   include "player/lua/assdraw.inc"
    },
    {"mp.options",
#   include "player/lua/options.inc"
    },
    {"@osc.lua",
#   include "player/lua/osc.inc"
    },
    {0}
};

// Represents a loaded script. Each has its own Lua state.
struct script_ctx {
    const char *name;
    const char *filename;
    lua_State *state;
    struct mp_log *log;
    struct mpv_handle *client;
    struct MPContext *mpctx;
    int suspended;
};

#if LUA_VERSION_NUM <= 501
#define mp_cpcall lua_cpcall
#define mp_lua_len lua_objlen
#else
// Curse whoever had this stupid idea. Curse whoever thought it would be a good
// idea not to include an emulated lua_cpcall() even more.
static int mp_cpcall (lua_State *L, lua_CFunction func, void *ud)
{
    lua_pushcfunction(L, func); // doesn't allocate in 5.2 (but does in 5.1)
    lua_pushlightuserdata(L, ud);
    return lua_pcall(L, 1, 0, 0);
}
#define mp_lua_len lua_rawlen
#endif

// Ensure that the given argument exists, even if it's nil. Can be used to
// avoid confusing the last missing optional arg with the first temporary value
// pushed to the stack.
static void mp_lua_optarg(lua_State *L, int arg)
{
    while (arg > lua_gettop(L))
        lua_pushnil(L);
}

static int destroy_crap(lua_State *L)
{
    void **data = luaL_checkudata(L, 1, "ohthispain");
    talloc_free(data[0]);
    data[0] = NULL;
    return 0;
}

// Creates a small userdata object and pushes it to the Lua stack. The function
// will (on the C level) return a talloc object that will be released by the
// userdata gc routine.
// This can be used to free temporary C data structures correctly if Lua errors
// happen.
// You can't free the talloc context directly; the Lua __gc handler does this.
// In my cases, talloc_free_children(returnval) will be used to free attached
// memory in advance when it's known not to be needed anymore (a minor
// optimization). Freeing it completely must be left to the Lua GC.
static void *mp_lua_PITA(lua_State *L)
{
    void **data = lua_newuserdata(L, sizeof(void *)); // u
    if (luaL_newmetatable(L, "ohthispain")) { // u metatable
        lua_pushvalue(L, -1);  // u metatable metatable
        lua_setfield(L, -2, "__index");  // u metatable
        lua_pushcfunction(L, destroy_crap); // u metatable gc
        lua_setfield(L, -2, "__gc"); // u metatable
    }
    lua_setmetatable(L, -2); // u
    *data = talloc_new(NULL);
    return *data;
}

// Perform the equivalent of mpv_free_node_contents(node) when tmp is freed.
static void auto_free_node(void *tmp, mpv_node *node)
{
    talloc_steal(tmp, node_get_alloc(node));
}

static struct script_ctx *get_ctx(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "ctx");
    struct script_ctx *ctx = lua_touserdata(L, -1);
    lua_pop(L, 1);
    assert(ctx);
    return ctx;
}

static struct MPContext *get_mpctx(lua_State *L)
{
    return get_ctx(L)->mpctx;
}

static int error_handler(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);

    if (luaL_loadstring(L, "return debug.traceback('', 3)") == 0) { // e fn|err
        lua_call(L, 0, 1); // e backtrace
        const char *tr = lua_tostring(L, -1);
        MP_WARN(ctx, "%s\n", tr ? tr : "(unknown)");
    }
    lua_pop(L, 1); // e

    return 1;
}

// Check client API error code:
//  if err >= 0, push "true" to the stack, and return 1
//  if err < 0, push nil and then the error string to the stack, and return 2
static int check_error(lua_State *L, int err)
{
    if (err >= 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, mpv_error_string(err));
    return 2;
}

static void add_functions(struct script_ctx *ctx);

static void load_file(lua_State *L, const char *fname)
{
    struct script_ctx *ctx = get_ctx(L);
    char *res_name = mp_get_user_path(NULL, ctx->mpctx->global, fname);
    MP_VERBOSE(ctx, "loading file %s\n", res_name);
    int r = luaL_loadfile(L, res_name);
    talloc_free(res_name); // careful to not leak this on Lua errors
    if (r)
        lua_error(L);
    lua_call(L, 0, 0);
}

static int load_builtin(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char dispname[80];
    snprintf(dispname, sizeof(dispname), "@%s", name);
    for (int n = 0; builtin_lua_scripts[n][0]; n++) {
        if (strcmp(name, builtin_lua_scripts[n][0]) == 0) {
            const char *script = builtin_lua_scripts[n][1];
            if (luaL_loadbuffer(L, script, strlen(script), dispname))
                lua_error(L);
            lua_call(L, 0, 1);
            return 1;
        }
    }
    luaL_error(L, "builtin module '%s' not found\n", name);
    return 0;
}

// Execute "require " .. name
static void require(lua_State *L, const char *name)
{
    struct script_ctx *ctx = get_ctx(L);
    MP_VERBOSE(ctx, "loading %s\n", name);
    // Lazy, but better than calling the "require" function manually
    char buf[80];
    snprintf(buf, sizeof(buf), "require '%s'", name);
    if (luaL_loadstring(L, buf))
        lua_error(L);
    lua_call(L, 0, 0);
}

// Push the table of a module. If it doesn't exist, it's created.
// The Lua script can call "require(module)" to "load" it.
static void push_module_table(lua_State *L, const char *module)
{
    lua_getglobal(L, "package"); // package
    lua_getfield(L, -1, "loaded"); // package loaded
    lua_remove(L, -2); // loaded
    lua_getfield(L, -1, module); // loaded module
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1); // loaded
        lua_newtable(L); // loaded module
        lua_pushvalue(L, -1); // loaded module module
        lua_setfield(L, -3, module); // loaded module
    }
    lua_remove(L, -2); // module
}

static int load_scripts(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *fname = ctx->filename;

    require(L, "mp.defaults");

    if (fname[0] == '@') {
        require(L, fname);
    } else {
        load_file(L, fname);
    }

    lua_getglobal(L, "mp_event_loop"); // fn
    if (lua_isnil(L, -1))
        luaL_error(L, "no event loop function\n");
    lua_call(L, 0, 0); // -

    return 0;
}

static void set_path(lua_State *L)
{
    void *tmp = talloc_new(NULL);

    lua_getglobal(L, "package"); // package
    lua_getfield(L, -1, "path"); // package path
    const char *path = lua_tostring(L, -1);

    char *newpath = talloc_strdup(tmp, path ? path : "");
    char **luadir = mp_find_all_config_files(tmp, get_mpctx(L)->global, "lua");
    for (int i = 0; luadir && luadir[i]; i++) {
        newpath = talloc_asprintf_append(newpath, ";%s",
                mp_path_join(tmp, bstr0(luadir[i]), bstr0("?.lua")));
    }

    lua_pushstring(L, newpath);  // package path newpath
    lua_setfield(L, -3, "path"); // package path
    lua_pop(L, 2);  // -

    talloc_free(tmp);
}

static int run_lua(lua_State *L)
{
    struct script_ctx *ctx = lua_touserdata(L, -1);
    lua_pop(L, 1); // -

    luaL_openlibs(L);

    // used by get_ctx()
    lua_pushlightuserdata(L, ctx); // ctx
    lua_setfield(L, LUA_REGISTRYINDEX, "ctx"); // -

    add_functions(ctx); // mp

    push_module_table(L, "mp"); // mp

    // "mp" is available by default, and no "require 'mp'" is needed
    lua_pushvalue(L, -1); // mp mp
    lua_setglobal(L, "mp"); // mp

    lua_pushstring(L, ctx->name); // mp name
    lua_setfield(L, -2, "script_name"); // mp

    // used by pushnode()
    lua_newtable(L); // mp table
    lua_pushvalue(L, -1); // mp table table
    lua_setfield(L, LUA_REGISTRYINDEX, "UNKNOWN_TYPE"); // mp table
    lua_setfield(L, -2, "UNKNOWN_TYPE"); // mp
    lua_newtable(L); // mp table
    lua_pushvalue(L, -1); // mp table table
    lua_setfield(L, LUA_REGISTRYINDEX, "MAP"); // mp table
    lua_setfield(L, -2, "MAP"); // mp
    lua_newtable(L); // mp table
    lua_pushvalue(L, -1); // mp table table
    lua_setfield(L, LUA_REGISTRYINDEX, "ARRAY"); // mp table
    lua_setfield(L, -2, "ARRAY"); // mp

    lua_pop(L, 1); // -

    assert(lua_gettop(L) == 0);

    // Add a preloader for each builtin Lua module
    lua_getglobal(L, "package"); // package
    assert(lua_type(L, -1) == LUA_TTABLE);
    lua_getfield(L, -1, "preload"); // package preload
    assert(lua_type(L, -1) == LUA_TTABLE);
    for (int n = 0; builtin_lua_scripts[n][0]; n++) {
        lua_pushcfunction(L, load_builtin); // package preload load_builtin
        lua_setfield(L, -2, builtin_lua_scripts[n][0]);
    }
    lua_pop(L, 2); // -

    assert(lua_gettop(L) == 0);

    set_path(L);
    assert(lua_gettop(L) == 0);

    // run this under an error handler that can do backtraces
    lua_pushcfunction(L, error_handler); // errf
    lua_pushcfunction(L, load_scripts); // errf fn
    if (lua_pcall(L, 0, 0, -2)) { // errf [error]
        const char *e = lua_tostring(L, -1);
        MP_FATAL(ctx, "Lua error: %s\n", e ? e : "(unknown)");
    }

    return 0;
}

static int load_lua(struct mpv_handle *client, const char *fname)
{
    struct MPContext *mpctx = mp_client_get_core(client);
    int r = -1;

    struct script_ctx *ctx = talloc_ptrtype(NULL, ctx);
    *ctx = (struct script_ctx) {
        .mpctx = mpctx,
        .client = client,
        .name = mpv_client_name(client),
        .log = mp_client_get_log(client),
        .filename = fname,
    };

    lua_State *L = ctx->state = luaL_newstate();
    if (!L)
        goto error_out;

    if (mp_cpcall(L, run_lua, ctx)) {
        const char *err = "unknown error";
        if (lua_type(L, -1) == LUA_TSTRING) // avoid allocation
            err = lua_tostring(L, -1);
        MP_FATAL(ctx, "Lua error: %s\n", err);
        goto error_out;
    }

    r = 0;

error_out:
    if (ctx->suspended)
        mpv_resume(ctx->client);
    if (ctx->state)
        lua_close(ctx->state);
    talloc_free(ctx);
    return r;
}

static int check_loglevel(lua_State *L, int arg)
{
    const char *level = luaL_checkstring(L, arg);
    for (int n = 0; n < MSGL_MAX; n++) {
        if (mp_log_levels[n] && strcasecmp(mp_log_levels[n], level) == 0)
            return n;
    }
    luaL_error(L, "Invalid log level '%s'", level);
    abort();
}

static int script_log(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);

    int msgl = check_loglevel(L, 1);

    int last = lua_gettop(L);
    lua_getglobal(L, "tostring"); // args... tostring
    for (int i = 2; i <= last; i++) {
        lua_pushvalue(L, -1); // args... tostring tostring
        lua_pushvalue(L, i); // args... tostring tostring args[i]
        lua_call(L, 1, 1); // args... tostring str
        const char *s = lua_tostring(L, -1);
        if (s == NULL)
            return luaL_error(L, "Invalid argument");
        mp_msg(ctx->log, msgl, "%s%s", s, i > 0 ? " " : "");
        lua_pop(L, 1);  // args... tostring
    }
    mp_msg(ctx->log, msgl, "\n");

    return 0;
}

static int script_find_config_file(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    const char *s = luaL_checkstring(L, 1);
    char *path = mp_find_config_file(NULL, mpctx->global, s);
    if (path) {
        lua_pushstring(L, path);
    } else {
        lua_pushnil(L);
    }
    talloc_free(path);
    return 1;
}

static int script_suspend(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    if (!ctx->suspended)
        mpv_suspend(ctx->client);
    ctx->suspended++;
    return 0;
}

static int script_resume(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    if (ctx->suspended < 1)
        luaL_error(L, "trying to resume, but core is not suspended");
    ctx->suspended--;
    if (!ctx->suspended)
        mpv_resume(ctx->client);
    return 0;
}

static void resume_all(struct script_ctx *ctx)
{
    if (ctx->suspended)
        mpv_resume(ctx->client);
    ctx->suspended = 0;
}

static int script_resume_all(lua_State *L)
{
    resume_all(get_ctx(L));
    return 0;
}

static void pushnode(lua_State *L, mpv_node *node);

static int script_wait_event(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);

    double timeout = luaL_optnumber(L, 1, 1e20);

    // This will almost surely lead to a deadlock. (Polling is still ok.)
    if (ctx->suspended && timeout > 0)
        luaL_error(L, "attempting to wait while core is suspended");

    mpv_event *event = mpv_wait_event(ctx->client, timeout);

    lua_newtable(L); // event
    lua_pushstring(L, mpv_event_name(event->event_id)); // event name
    lua_setfield(L, -2, "event"); // event

    if (event->reply_userdata) {
        lua_pushnumber(L, event->reply_userdata);
        lua_setfield(L, -2, "id");
    }

    if (event->error < 0) {
        lua_pushstring(L, mpv_error_string(event->error)); // event err
        lua_setfield(L, -2, "error"); // event
    }

    switch (event->event_id) {
    case MPV_EVENT_LOG_MESSAGE: {
        mpv_event_log_message *msg = event->data;

        lua_pushstring(L, msg->prefix); // event s
        lua_setfield(L, -2, "prefix"); // event
        lua_pushstring(L, msg->level); // event s
        lua_setfield(L, -2, "level"); // event
        lua_pushstring(L, msg->text); // event s
        lua_setfield(L, -2, "text"); // event
        break;
    }
    case MPV_EVENT_SCRIPT_INPUT_DISPATCH: {
        mpv_event_script_input_dispatch *msg = event->data;

        lua_pushinteger(L, msg->arg0); // event i
        lua_setfield(L, -2, "arg0"); // event
        lua_pushstring(L, msg->type); // event s
        lua_setfield(L, -2, "type"); // event
        break;
    }
    case MPV_EVENT_CLIENT_MESSAGE: {
        mpv_event_client_message *msg = event->data;

        lua_newtable(L); // event args
        for (int n = 0; n < msg->num_args; n++) {
            lua_pushinteger(L, n + 1); // event args N
            lua_pushstring(L, msg->args[n]); // event args N val
            lua_settable(L, -3); // event args
        }
        lua_setfield(L, -2, "args"); // event
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = event->data;
        lua_pushstring(L, prop->name);
        lua_setfield(L, -2, "name");
        switch (prop->format) {
        case MPV_FORMAT_NODE:
            pushnode(L, prop->data);
            break;
        case MPV_FORMAT_DOUBLE:
            lua_pushnumber(L, *(double *)prop->data);
            break;
        case MPV_FORMAT_FLAG:
            lua_pushboolean(L, *(int *)prop->data);
            break;
        case MPV_FORMAT_STRING:
            lua_pushstring(L, *(char **)prop->data);
            break;
        default:
            lua_pushnil(L);
        }
        lua_setfield(L, -2, "data");
        break;
    }
    default: ;
    }

    // return event
    return 1;
}

static int script_request_event(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *event = luaL_checkstring(L, 1);
    bool enable = lua_toboolean(L, 2);
    // brute force event name -> id; stops working for events > assumed max
    int event_id = -1;
    for (int n = 0; n < 256; n++) {
        const char *name = mpv_event_name(n);
        if (name && strcmp(name, event) == 0) {
            event_id = n;
            break;
        }
    }
    lua_pushboolean(L, mpv_request_event(ctx->client, event_id, enable) >= 0);
    return 1;
}

static int script_enable_messages(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    check_loglevel(L, 1);
    const char *level = luaL_checkstring(L, 1);
    return check_error(L, mpv_request_log_messages(ctx->client, level));
}

static int script_command(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *s = luaL_checkstring(L, 1);

    return check_error(L, mpv_command_string(ctx->client, s));
}

static int script_commandv(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    int num = lua_gettop(L);
    const char *args[50];
    if (num + 1 > MP_ARRAY_SIZE(args))
        luaL_error(L, "too many arguments");
    for (int n = 1; n <= num; n++) {
        const char *s = lua_tostring(L, n);
        if (!s)
            luaL_error(L, "argument %d is not a string", n);
        args[n - 1] = s;
    }
    args[num] = NULL;
    return check_error(L, mpv_command(ctx->client, args));
}

static int script_set_property(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *p = luaL_checkstring(L, 1);
    const char *v = luaL_checkstring(L, 2);

    return check_error(L, mpv_set_property_string(ctx->client, p, v));
}

static int script_set_property_bool(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *p = luaL_checkstring(L, 1);
    int v = lua_toboolean(L, 2);

    return check_error(L, mpv_set_property(ctx->client, p, MPV_FORMAT_FLAG, &v));
}

static bool is_int(double d)
{
    int64_t v = d;
    return d == (double)v;
}

static int script_set_property_number(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *p = luaL_checkstring(L, 1);
    double d = luaL_checknumber(L, 2);
    // If the number might be an integer, then set it as integer. The mpv core
    // will (probably) convert INT64 to DOUBLE when setting, but not the other
    // way around.
    int res;
    if (is_int(d)) {
        res = mpv_set_property(ctx->client, p, MPV_FORMAT_INT64, &(int64_t){d});
    } else {
        res = mpv_set_property(ctx->client, p, MPV_FORMAT_DOUBLE, &d);
    }
    return check_error(L, res);
}

static void makenode(void *tmp, mpv_node *dst, lua_State *L, int t)
{
    if (t < 0)
        t = lua_gettop(L) + (t + 1);
    switch (lua_type(L, t)) {
    case LUA_TNIL:
        dst->format = MPV_FORMAT_NONE;
        break;
    case LUA_TNUMBER: {
        double d = lua_tonumber(L, t);
        if (is_int(d)) {
            dst->format = MPV_FORMAT_INT64;
            dst->u.int64 = d;
        } else {
            dst->format = MPV_FORMAT_DOUBLE;
            dst->u.double_ = d;
        }
        break;
    }
    case LUA_TBOOLEAN:
        dst->format = MPV_FORMAT_FLAG;
        dst->u.flag = !!lua_toboolean(L, t);
        break;
    case LUA_TSTRING:
        dst->format = MPV_FORMAT_STRING;
        dst->u.string = talloc_strdup(tmp, lua_tostring(L, t));
        break;
    case LUA_TTABLE: {
        // Lua uses the same type for arrays and maps, so guess the correct one.
        int format = MPV_FORMAT_NONE;
        if (lua_getmetatable(L, t)) { // mt
            lua_getfield(L, -1, "type"); // mt val
            if (lua_type(L, -1) == LUA_TSTRING) {
                const char *type = lua_tostring(L, -1);
                if (strcmp(type, "MAP") == 0) {
                    format = MPV_FORMAT_NODE_MAP;
                } else if (strcmp(type, "ARRAY") == 0) {
                    format = MPV_FORMAT_NODE_ARRAY;
                }
            }
            lua_pop(L, 2);
        }
        if (format == MPV_FORMAT_NONE) {
            // If all keys are integers, and they're in sequence, take it
            // as an array.
            int count = 0;
            for (int n = 1; ; n++) {
                lua_pushinteger(L, n); // n
                lua_gettable(L, t); // t[n]
                bool empty = lua_isnil(L, -1); // t[n]
                lua_pop(L, 1); // -
                if (empty) {
                    count = n;
                    break;
                }
            }
            if (count > 0)
                format = MPV_FORMAT_NODE_ARRAY;
            lua_pushnil(L); // nil
            while (lua_next(L, t) != 0) { // key value
                count--;
                lua_pop(L, 1); // key
                if (count < 0) {
                    lua_pop(L, 1); // -
                    format = MPV_FORMAT_NODE_MAP;
                    break;
                }
            }
        }
        if (format == MPV_FORMAT_NONE)
            format = MPV_FORMAT_NODE_ARRAY; // probably empty table; assume array
        mpv_node_list *list = talloc_zero(tmp, mpv_node_list);
        dst->format = format;
        dst->u.list = list;
        if (format == MPV_FORMAT_NODE_ARRAY) {
            for (int n = 0; ; n++) {
                lua_pushinteger(L, n + 1); // n1
                lua_gettable(L, t); // t[n1]
                if (lua_isnil(L, -1))
                    break;
                MP_TARRAY_GROW(tmp, list->values, list->num);
                makenode(tmp, &list->values[n], L, -1);
                list->num++;
                lua_pop(L, 1); // -
            }
            lua_pop(L, 1); // -
        } else {
            lua_pushnil(L); // nil
            while (lua_next(L, t) != 0) { // key value
                MP_TARRAY_GROW(tmp, list->values, list->num);
                MP_TARRAY_GROW(tmp, list->keys, list->num);
                makenode(tmp, &list->values[list->num], L, -1);
                if (lua_type(L, -2) != LUA_TSTRING) {
                    luaL_error(L, "key must be a string, but got %s",
                               lua_typename(L, -2));
                }
                list->keys[list->num] = talloc_strdup(tmp, lua_tostring(L, -2));
                list->num++;
                lua_pop(L, 1); // key
            }
        }
        break;
    }
    default:
        // unknown type
        luaL_error(L, "disallowed Lua type found: %s\n", lua_typename(L, t));
    }
}

static int script_set_property_native(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *p = luaL_checkstring(L, 1);
    struct mpv_node node;
    void *tmp = mp_lua_PITA(L);
    makenode(tmp, &node, L, 2);
    int res = mpv_set_property(ctx->client, p, MPV_FORMAT_NODE, &node);
    talloc_free_children(tmp);
    return check_error(L, res);

}

static int script_get_property(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *name = luaL_checkstring(L, 1);
    int type = lua_tointeger(L, lua_upvalueindex(1))
               ? MPV_FORMAT_OSD_STRING : MPV_FORMAT_STRING;

    char *result = NULL;
    int err = mpv_get_property(ctx->client, name, type, &result);
    if (err >= 0) {
        lua_pushstring(L, result);
        talloc_free(result);
        return 1;
    } else {
        if (lua_isnoneornil(L, 2) && type == MPV_FORMAT_OSD_STRING) {
            lua_pushstring(L, "");
        } else {
            lua_pushvalue(L, 2);
        }
        lua_pushstring(L, mpv_error_string(err));
        return 2;
    }
}

static int script_get_property_bool(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *name = luaL_checkstring(L, 1);

    int result = 0;
    int err = mpv_get_property(ctx->client, name, MPV_FORMAT_FLAG, &result);
    if (err >= 0) {
        lua_pushboolean(L, !!result);
        return 1;
    } else {
        lua_pushvalue(L, 2);
        lua_pushstring(L, mpv_error_string(err));
        return 2;
    }
}

static int script_get_property_number(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *name = luaL_checkstring(L, 1);

    // Note: the mpv core will (hopefully) convert INT64 to DOUBLE
    double result = 0;
    int err = mpv_get_property(ctx->client, name, MPV_FORMAT_DOUBLE, &result);
    if (err >= 0) {
        lua_pushnumber(L, result);
        return 1;
    } else {
        lua_pushvalue(L, 2);
        lua_pushstring(L, mpv_error_string(err));
        return 2;
    }
}

static void pushnode(lua_State *L, mpv_node *node)
{
    luaL_checkstack(L, 6, "stack overflow");

    switch (node->format) {
    case MPV_FORMAT_STRING:
        lua_pushstring(L, node->u.string);
        break;
    case MPV_FORMAT_INT64:
        lua_pushnumber(L, node->u.int64);
        break;
    case MPV_FORMAT_DOUBLE:
        lua_pushnumber(L, node->u.double_);
        break;
    case MPV_FORMAT_NONE:
        lua_pushnil(L);
        break;
    case MPV_FORMAT_FLAG:
        lua_pushboolean(L, node->u.flag);
        break;
    case MPV_FORMAT_NODE_ARRAY:
        lua_newtable(L); // table
        lua_getfield(L, LUA_REGISTRYINDEX, "ARRAY"); // table mt
        lua_setmetatable(L, -2); // table
        for (int n = 0; n < node->u.list->num; n++) {
            pushnode(L, &node->u.list->values[n]); // table value
            lua_rawseti(L, -2, n + 1); // table
        }
        break;
    case MPV_FORMAT_NODE_MAP:
        lua_newtable(L); // table
        lua_getfield(L, LUA_REGISTRYINDEX, "MAP"); // table mt
        lua_setmetatable(L, -2); // table
        for (int n = 0; n < node->u.list->num; n++) {
            lua_pushstring(L, node->u.list->keys[n]); // table key
            pushnode(L, &node->u.list->values[n]); // table key value
            lua_rawset(L, -3);
        }
        break;
    default:
        // unknown value - what do we do?
        // for now, set a unique dummy value
        lua_newtable(L); // table
        lua_getfield(L, LUA_REGISTRYINDEX, "UNKNOWN_TYPE");
        lua_setmetatable(L, -2); // table
        break;
    }
}

static int script_get_property_native(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    const char *name = luaL_checkstring(L, 1);
    mp_lua_optarg(L, 2);
    void *tmp = mp_lua_PITA(L);

    mpv_node node;
    int err = mpv_get_property(ctx->client, name, MPV_FORMAT_NODE, &node);
    if (err >= 0) {
        auto_free_node(tmp, &node);
        pushnode(L, &node);
        talloc_free_children(tmp);
        return 1;
    }
    lua_pushvalue(L, 2);
    lua_pushstring(L, mpv_error_string(err));
    return 2;
}

static mpv_format check_property_format(lua_State *L, int arg)
{
    if (lua_isnil(L, arg))
        return MPV_FORMAT_NONE;
    const char *fmts[] = {"none", "native", "bool", "string", "number", NULL};
    switch (luaL_checkoption(L, arg, "none", fmts)) {
    case 0: return MPV_FORMAT_NONE;
    case 1: return MPV_FORMAT_NODE;
    case 2: return MPV_FORMAT_FLAG;
    case 3: return MPV_FORMAT_STRING;
    case 4: return MPV_FORMAT_DOUBLE;
    }
    abort();
}

// It has a raw_ prefix, because there is a more high level API in defaults.lua.
static int script_raw_observe_property(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    uint64_t id = luaL_checknumber(L, 1);
    const char *name = luaL_checkstring(L, 2);
    mpv_format format = check_property_format(L, 3);
    return check_error(L, mpv_observe_property(ctx->client, id, name, format));
}

static int script_raw_unobserve_property(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    uint64_t id = luaL_checknumber(L, 1);
    lua_pushnumber(L, mpv_unobserve_property(ctx->client, id));
    return 1;
}

static int script_command_native(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    mp_lua_optarg(L, 2);
    struct mpv_node node;
    struct mpv_node result;
    void *tmp = mp_lua_PITA(L);
    makenode(tmp, &node, L, 1);
    int err = mpv_command_node(ctx->client, &node, &result);
    if (err >= 0) {
        auto_free_node(tmp, &result);
        pushnode(L, &result);
        talloc_free_children(tmp);
        return 1;
    }
    lua_pushvalue(L, 2);
    lua_pushstring(L, mpv_error_string(err));
    return 2;
}

static int script_set_osd_ass(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    int res_x = luaL_checkinteger(L, 1);
    int res_y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    osd_set_external(mpctx->osd, res_x, res_y, (char *)text);
    mp_input_wakeup(mpctx->input);
    return 0;
}

static int script_get_osd_resolution(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    int w, h;
    osd_object_get_resolution(mpctx->osd, OSDTYPE_EXTERNAL, &w, &h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int script_get_screen_size(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    struct mp_osd_res vo_res = osd_get_vo_res(mpctx->osd, OSDTYPE_EXTERNAL);
    double aspect = 1.0 * vo_res.w / MPMAX(vo_res.h, 1) /
                    (vo_res.display_par ? vo_res.display_par : 1);
    lua_pushnumber(L, vo_res.w);
    lua_pushnumber(L, vo_res.h);
    lua_pushnumber(L, aspect);
    return 3;
}

static int script_get_mouse_pos(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    int px, py;
    mp_input_get_mouse_pos(mpctx->input, &px, &py);
    double sw, sh;
    osd_object_get_scale_factor(mpctx->osd, OSDTYPE_EXTERNAL, &sw, &sh);
    lua_pushnumber(L, px * sw);
    lua_pushnumber(L, py * sh);
    return 2;
}

static int script_get_time(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    lua_pushnumber(L, mpv_get_time_us(ctx->client) / (double)(1000 * 1000));
    return 1;
}

static int script_input_define_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    char *contents = (char *)luaL_checkstring(L, 2);
    char *flags = (char *)luaL_optstring(L, 3, "");
    bool builtin = true;
    if (strcmp(flags, "default") == 0) {
        builtin = true;
    } else if (strcmp(flags, "force") == 0) {
        builtin = false;
    } else if (strcmp(flags, "") == 0) {
        //pass
    } else {
        luaL_error(L, "invalid flags: '%s'", flags);
    }
    mp_input_define_section(mpctx->input, section, "<script>", contents, builtin);
    return 0;
}

static int script_input_enable_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    char *sflags = (char *)luaL_optstring(L, 2, "");
    bstr bflags = bstr0(sflags);
    int flags = 0;
    while (bflags.len) {
        bstr val;
        bstr_split_tok(bflags, "|", &val, &bflags);
        if (bstr_equals0(val, "allow-hide-cursor")) {
            flags |= MP_INPUT_ALLOW_HIDE_CURSOR;
        } else if (bstr_equals0(val, "allow-vo-dragging")) {
            flags |= MP_INPUT_ALLOW_VO_DRAGGING;
        } else if (bstr_equals0(val, "exclusive")) {
            flags |= MP_INPUT_EXCLUSIVE;
        } else {
            luaL_error(L, "invalid flag: '%.*s'", BSTR_P(val));
        }
    }
    mp_input_enable_section(mpctx->input, section, flags);
    return 0;
}

static int script_input_disable_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    mp_input_disable_section(mpctx->input, section);
    return 0;
}

static int script_input_set_section_mouse_area(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);

    double sw, sh;
    osd_object_get_scale_factor(mpctx->osd, OSDTYPE_EXTERNAL, &sw, &sh);

    char *section = (char *)luaL_checkstring(L, 1);
    int x0 = sw ? luaL_checkinteger(L, 2) / sw : 0;
    int y0 = sh ? luaL_checkinteger(L, 3) / sh : 0;
    int x1 = sw ? luaL_checkinteger(L, 4) / sw : 0;
    int y1 = sh ? luaL_checkinteger(L, 5) / sh : 0;
    mp_input_set_section_mouse_area(mpctx->input, section, x0, y0, x1, y1);
    return 0;
}

static int script_format_time(lua_State *L)
{
    double t = luaL_checknumber(L, 1);
    const char *fmt = luaL_optstring(L, 2, "%H:%M:%S");
    char *r = mp_format_time_fmt(fmt, t);
    if (!r)
        luaL_error(L, "Invalid time format string '%s'", fmt);
    lua_pushstring(L, r);
    talloc_free(r);
    return 1;
}

static int script_get_wakeup_pipe(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    lua_pushinteger(L, mpv_get_wakeup_pipe(ctx->client));
    return 1;
}

static int script_getcwd(lua_State *L)
{
    char *cwd = mp_getcwd(NULL);
    if (!cwd) {
        lua_pushnil(L);
        lua_pushstring(L, "error");
        return 2;
    }
    lua_pushstring(L, cwd);
    talloc_free(cwd);
    return 1;
}

static int script_readdir(lua_State *L)
{
    //                    0      1        2       3
    const char *fmts[] = {"all", "files", "dirs", "normal", NULL};
    const char *path = luaL_checkstring(L, 1);
    int t = luaL_checkoption(L, 2, "normal", fmts);
    DIR *dir = opendir(path);
    if (!dir) {
        lua_pushnil(L);
        lua_pushstring(L, "error");
        return 2;
    }
    lua_newtable(L); // list
    char *fullpath = NULL;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(dir))) {
        char *name = e->d_name;
        if (t) {
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (fullpath)
                fullpath[0] = '\0';
            fullpath = talloc_asprintf_append(fullpath, "%s/%s", path, name);
            struct stat st;
            if (stat(fullpath, &st))
                continue;
            if (!(((t & 1) && S_ISREG(st.st_mode)) ||
                  ((t & 2) && S_ISDIR(st.st_mode))))
                continue;
        }
        lua_pushinteger(L, ++n); // list index
        lua_pushstring(L, name); // list index name
        lua_settable(L, -3); // list
    }
    talloc_free(fullpath);
    return 1;
}

static int script_split_path(lua_State *L)
{
    const char *p = luaL_checkstring(L, 1);
    bstr fname = mp_dirname(p);
    lua_pushlstring(L, fname.start, fname.len);
    lua_pushstring(L, mp_basename(p));
    return 2;
}

static int script_join_path(lua_State *L)
{
    const char *p1 = luaL_checkstring(L, 1);
    const char *p2 = luaL_checkstring(L, 2);
    char *r = mp_path_join(NULL, bstr0(p1), bstr0(p2));
    lua_pushstring(L, r);
    talloc_free(r);
    return 1;
}

typedef void (*read_cb)(void *ctx, char *data, size_t size);

#ifdef __MINGW32__
#include <windows.h>
#include "osdep/io.h"
#include "osdep/atomics.h"

static void write_arg(bstr *cmdline, char *arg)
{
    // If the string doesn't have characters that need to be escaped, it's best
    // to leave it alone for the sake of Windows programs that don't process
    // quoted args correctly.
    if (!strpbrk(arg, " \t\"")) {
        bstr_xappend(NULL, cmdline, bstr0(arg));
        return;
    }

    // If there are characters that need to be escaped, write a quoted string
    bstr_xappend(NULL, cmdline, bstr0("\""));

    // Escape the argument. To match the behavior of CommandLineToArgvW,
    // backslashes are only escaped if they appear before a quote or the end of
    // the string.
    int num_slashes = 0;
    for (int pos = 0; arg[pos]; pos++) {
        switch (arg[pos]) {
        case '\\':
            // Count backslashes that appear in a row
            num_slashes++;
            break;
        case '"':
            bstr_xappend(NULL, cmdline, (struct bstr){arg, pos});

            // Double preceding slashes
            for (int i = 0; i < num_slashes; i++)
                bstr_xappend(NULL, cmdline, bstr0("\\"));

            // Escape the following quote
            bstr_xappend(NULL, cmdline, bstr0("\\"));

            arg += pos;
            pos = 0;
            num_slashes = 0;
            break;
        default:
            num_slashes = 0;
        }
    }

    // Write the rest of the argument
    bstr_xappend(NULL, cmdline, bstr0(arg));

    // Double slashes that appear at the end of the string
    for (int i = 0; i < num_slashes; i++)
        bstr_xappend(NULL, cmdline, bstr0("\\"));

    bstr_xappend(NULL, cmdline, bstr0("\""));
}

// Convert an array of arguments to a properly escaped command-line string
static wchar_t *write_cmdline(void *ctx, char **argv)
{
    bstr cmdline = {0};

    for (int i = 0; argv[i]; i++) {
        write_arg(&cmdline, argv[i]);
        if (argv[i + 1])
            bstr_xappend(NULL, &cmdline, bstr0(" "));
    }

    wchar_t *wcmdline = mp_from_utf8(ctx, cmdline.start);
    talloc_free(cmdline.start);
    return wcmdline;
}

static int create_overlapped_pipe(HANDLE *read, HANDLE *write)
{
    static atomic_ulong counter = ATOMIC_VAR_INIT(0);

    // Generate pipe name
    unsigned long id = atomic_fetch_add(&counter, 1);
    unsigned pid = GetCurrentProcessId();
    wchar_t buf[36];
    swprintf(buf, sizeof(buf), L"\\\\?\\pipe\\mpv-anon-%08x-%08lx", pid, id);

    // The function for creating anonymous pipes (CreatePipe) can't create
    // overlapped pipes, so instead, use a named pipe with a unique name
    *read = CreateNamedPipeW(buf, PIPE_ACCESS_INBOUND |
        FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 0, 4096, 0, NULL);
    if (!*read)
        goto error;

    // Open the write end of the pipe as a synchronous handle
    *write = CreateFileW(buf, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (*write == INVALID_HANDLE_VALUE)
        goto error;

    return 0;
error:
    *read = *write = INVALID_HANDLE_VALUE;
    return -1;
}

// Helper method similar to sparse_poll, skips NULL handles
static int sparse_wait(HANDLE *handles, unsigned num_handles)
{
    unsigned w_num_handles = 0;
    HANDLE w_handles[num_handles];
    int map[num_handles];

    for (unsigned i = 0; i < num_handles; i++) {
        if (!handles[i])
            continue;

        w_handles[w_num_handles] = handles[i];
        map[w_num_handles] = i;
        w_num_handles++;
    }

    if (w_num_handles == 0)
        return -1;
    DWORD i = WaitForMultipleObjects(w_num_handles, w_handles, FALSE, INFINITE);
    i -= WAIT_OBJECT_0;

    if (i >= w_num_handles)
        return -1;
    return map[i];
}

// Wrapper for ReadFile that treats ERROR_IO_PENDING as success
static int async_read(HANDLE file, void *buf, unsigned size, OVERLAPPED* ol)
{
    if (!ReadFile(file, buf, size, NULL, ol))
        return (GetLastError() == ERROR_IO_PENDING) ? 0 : -1;
    return 0;
}

static int subprocess(char **args, struct mp_cancel *cancel, void *ctx,
                      read_cb on_stdout, read_cb on_stderr, char **error)
{
    wchar_t *tmp = talloc_new(NULL);
    int status = -1;
    struct {
        HANDLE read;
        HANDLE write;
        OVERLAPPED ol;
        char buf[4096];
        read_cb read_cb;
    } pipes[2] = {
        { .read_cb = on_stdout },
        { .read_cb = on_stderr },
    };

    // If the function exits before CreateProcess, there was an init error
    *error = "init";

    for (int i = 0; i < 2; i++) {
        pipes[i].ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!pipes[i].ol.hEvent)
            goto done;
        if (create_overlapped_pipe(&pipes[i].read, &pipes[i].write))
            goto done;
        if (!SetHandleInformation(pipes[i].write, HANDLE_FLAG_INHERIT, 1))
            goto done;
    }

    // Convert the args array to a UTF-16 Windows command-line string
    wchar_t *cmdline = write_cmdline(tmp, args);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOW si = {
        .cb = sizeof(si),
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = NULL,
        .hStdOutput = pipes[0].write,
        .hStdError = pipes[1].write,
    };

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        NULL, NULL, &si, &pi))
        goto done;
    talloc_free(cmdline);
    CloseHandle(pi.hThread);

    // Init is finished
    *error = NULL;

    // List of handles to watch with sparse_wait
    HANDLE handles[] = { pipes[0].ol.hEvent, pipes[1].ol.hEvent, pi.hProcess,
                         cancel ? mp_cancel_get_event(cancel) : NULL };

    for (int i = 0; i < 2; i++) {
        // Close our copy of the write end of the pipes
        CloseHandle(pipes[i].write);
        pipes[i].write = NULL;

        // Do the first read operation on each pipe
        if (async_read(pipes[i].read, pipes[i].buf, 4096, &pipes[i].ol)) {
            CloseHandle(pipes[i].read);
            handles[i] = pipes[i].read = NULL;
        }
    }

    DWORD r;
    DWORD exit_code;
    while (pipes[0].read || pipes[1].read || pi.hProcess) {
        int i = sparse_wait(handles, MP_ARRAY_SIZE(handles));
        switch (i) {
        case 0:
        case 1:
            // Complete the read operation on the pipe
            if (!GetOverlappedResult(pipes[i].read, &pipes[i].ol, &r, TRUE)) {
                CloseHandle(pipes[i].read);
                handles[i] = pipes[i].read = NULL;
                break;
            }

            pipes[i].read_cb(ctx, pipes[i].buf, r);

            // Begin the next read operation on the pipe
            if (async_read(pipes[i].read, pipes[i].buf, 4096, &pipes[i].ol)) {
                CloseHandle(pipes[i].read);
                handles[i] = pipes[i].read = NULL;
            }

            break;
        case 2:
            GetExitCodeProcess(pi.hProcess, &exit_code);
            status = exit_code;

            CloseHandle(pi.hProcess);
            handles[i] = pi.hProcess = NULL;
            break;
        case 3:
            if (pi.hProcess) {
                TerminateProcess(pi.hProcess, 1);
                *error = "killed";
                goto done;
            }
            break;
        default:
            goto done;
        }
    }

done:
    for (int i = 0; i < 2; i++) {
        if (pipes[i].ol.hEvent) CloseHandle(pipes[i].ol.hEvent);
        if (pipes[i].read) CloseHandle(pipes[i].read);
        if (pipes[i].write) CloseHandle(pipes[i].write);
    }
    if (pi.hProcess) CloseHandle(pi.hProcess);
    talloc_free(tmp);
    return status;
}
#endif

#if HAVE_POSIX_SPAWN
#include <spawn.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

// Normally, this must be declared manually, but glibc is retarded.
#ifndef __GLIBC__
extern char **environ;
#endif

// A silly helper: automatically skips entries with negative FDs
static int sparse_poll(struct pollfd *fds, int num_fds, int timeout)
{
    struct pollfd p_fds[10];
    int map[10];
    if (num_fds > MP_ARRAY_SIZE(p_fds))
        return -1;
    int p_num_fds = 0;
    for (int n = 0; n < num_fds; n++) {
        map[n] = -1;
        if (fds[n].fd < 0)
            continue;
        map[n] = p_num_fds;
        p_fds[p_num_fds++] = fds[n];
    }
    int r = poll(p_fds, p_num_fds, timeout);
    for (int n = 0; n < num_fds; n++)
        fds[n].revents = (map[n] < 0 && r >= 0) ? 0 : p_fds[map[n]].revents;
    return r;
}

static int subprocess(char **args, struct mp_cancel *cancel, void *ctx,
                      read_cb on_stdout, read_cb on_stderr, char **error)
{
    posix_spawn_file_actions_t fa;
    bool fa_destroy = false;
    int status = -1;
    int p_stdout[2] = {-1, -1};
    int p_stderr[2] = {-1, -1};
    pid_t pid = -1;

    if (mp_make_cloexec_pipe(p_stdout) < 0)
        goto done;
    if (mp_make_cloexec_pipe(p_stderr) < 0)
        goto done;

    if (posix_spawn_file_actions_init(&fa))
        goto done;
    fa_destroy = true;
    // redirect stdout and stderr
    if (posix_spawn_file_actions_adddup2(&fa, p_stdout[1], 1))
        goto done;
    if (posix_spawn_file_actions_adddup2(&fa, p_stderr[1], 2))
        goto done;

    if (posix_spawnp(&pid, args[0], &fa, NULL, args, environ)) {
        pid = -1;
        goto done;
    }

    close(p_stdout[1]);
    p_stdout[1] = -1;
    close(p_stderr[1]);
    p_stderr[1] = -1;

    int *read_fds[2] = {&p_stdout[0], &p_stderr[0]};
    read_cb read_cbs[2] = {on_stdout, on_stderr};

    while (p_stdout[0] >= 0 || p_stderr[0] >= 0) {
        struct pollfd fds[] = {
            {.events = POLLIN, .fd = *read_fds[0]},
            {.events = POLLIN, .fd = *read_fds[1]},
            {.events = POLLIN, .fd = cancel ? mp_cancel_get_fd(cancel) : -1},
        };
        if (sparse_poll(fds, MP_ARRAY_SIZE(fds), -1) < 0 && errno != EINTR)
            break;
        for (int n = 0; n < 2; n++) {
            if (fds[n].revents) {
                char buf[4096];
                ssize_t r = read(*read_fds[n], buf, sizeof(buf));
                if (r < 0 && errno == EINTR)
                    continue;
                if (r > 0 && read_cbs[n])
                    read_cbs[n](ctx, buf, r);
                if (r <= 0) {
                    close(*read_fds[n]);
                    *read_fds[n] = -1;
                }
            }
        }
        if (fds[2].revents) {
            kill(pid, SIGKILL);
            break;
        }
    }

    // Note: it can happen that a child process closes the pipe, but does not
    //       terminate yet. In this case, we would have to run waitpid() in
    //       a separate thread and use pthread_cancel(), or use other weird
    //       and laborious tricks. So this isn't handled yet.
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

done:
    if (fa_destroy)
        posix_spawn_file_actions_destroy(&fa);
    close(p_stdout[0]);
    close(p_stdout[1]);
    close(p_stderr[0]);
    close(p_stderr[1]);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 127) {
        *error = NULL;
        status = WEXITSTATUS(status);
    } else {
        *error = WEXITSTATUS(status) == 127 ? "init" : "killed";
        status = -1;
    }

    return status;
}
#endif

#if HAVE_POSIX_SPAWN || defined(__MINGW32__)
struct subprocess_cb_ctx {
    struct mp_log *log;
    void* talloc_ctx;
    int64_t max_size;
    bstr output;
};

static void subprocess_stdout(void *p, char *data, size_t size)
{
    struct subprocess_cb_ctx *ctx = p;
    if (ctx->output.len < ctx->max_size)
        bstr_xappend(ctx->talloc_ctx, &ctx->output, (bstr){data, size});
}

static void subprocess_stderr(void *p, char *data, size_t size)
{
    struct subprocess_cb_ctx *ctx = p;
    MP_INFO(ctx, "%.*s", (int)size, data);
}

static int script_subprocess(lua_State *L)
{
    struct script_ctx *ctx = get_ctx(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    void *tmp = mp_lua_PITA(L);

    resume_all(ctx);

    lua_getfield(L, 1, "args"); // args
    int num_args = mp_lua_len(L, -1);
    char *args[256];
    if (num_args > MP_ARRAY_SIZE(args) - 1) // last needs to be NULL
        luaL_error(L, "too many arguments");
    if (num_args < 1)
        luaL_error(L, "program name missing");
    for (int n = 0; n < num_args; n++) {
        lua_pushinteger(L, n + 1); // args n
        lua_gettable(L, -2); // args arg
        args[n] = talloc_strdup(tmp, lua_tostring(L, -1));
        if (!args[n])
            luaL_error(L, "program arguments must be strings");
        lua_pop(L, 1); // args
    }
    args[num_args] = NULL;
    lua_pop(L, 1); // -

    lua_getfield(L, 1, "cancellable"); // c
    struct mp_cancel *cancel = NULL;
    if (lua_isnil(L, -1) ? true : lua_toboolean(L, -1))
        cancel = ctx->mpctx->playback_abort;
    lua_pop(L, 1); // -

    lua_getfield(L, 1, "max_size"); // m
    int64_t max_size = lua_isnil(L, -1) ? 16 * 1024 * 1024 : lua_tointeger(L, -1);

    struct subprocess_cb_ctx cb_ctx = {
        .log = ctx->log,
        .talloc_ctx = tmp,
        .max_size = max_size,
    };

    char *error = NULL;
    int status = subprocess(args, cancel, &cb_ctx, subprocess_stdout,
                            subprocess_stderr, &error);

    lua_newtable(L); // res
    if (error) {
        lua_pushstring(L, error); // res e
        lua_setfield(L, -2, "error"); // res
    }
    lua_pushinteger(L, status); // res s
    lua_setfield(L, -2, "status"); // res
    lua_pushlstring(L, cb_ctx.output.start, cb_ctx.output.len); // res d
    lua_setfield(L, -2, "stdout"); // res
    return 1;
}
#endif

static int script_parse_json(lua_State *L)
{
    mp_lua_optarg(L, 2);
    void *tmp = mp_lua_PITA(L);
    char *text = talloc_strdup(tmp, luaL_checkstring(L, 1));
    bool trail = lua_toboolean(L, 2);
    bool ok = false;
    struct mpv_node node;
    if (json_parse(tmp, &node, &text, 32) >= 0) {
        json_skip_whitespace(&text);
        ok = !text[0] || trail;
    }
    if (ok) {
        pushnode(L, &node);
        lua_pushnil(L);
    } else {
        lua_pushnil(L);
        lua_pushstring(L, "error");
    }
    lua_pushstring(L, text);
    talloc_free_children(tmp);
    return 3;
}

#define FN_ENTRY(name) {#name, script_ ## name}
struct fn_entry {
    const char *name;
    int (*fn)(lua_State *L);
};

static const struct fn_entry main_fns[] = {
    FN_ENTRY(log),
    FN_ENTRY(suspend),
    FN_ENTRY(resume),
    FN_ENTRY(resume_all),
    FN_ENTRY(wait_event),
    FN_ENTRY(request_event),
    FN_ENTRY(find_config_file),
    FN_ENTRY(command),
    FN_ENTRY(commandv),
    FN_ENTRY(command_native),
    FN_ENTRY(get_property_bool),
    FN_ENTRY(get_property_number),
    FN_ENTRY(get_property_native),
    FN_ENTRY(set_property),
    FN_ENTRY(set_property_bool),
    FN_ENTRY(set_property_number),
    FN_ENTRY(set_property_native),
    FN_ENTRY(raw_observe_property),
    FN_ENTRY(raw_unobserve_property),
    FN_ENTRY(set_osd_ass),
    FN_ENTRY(get_osd_resolution),
    FN_ENTRY(get_screen_size),
    FN_ENTRY(get_mouse_pos),
    FN_ENTRY(get_time),
    FN_ENTRY(input_define_section),
    FN_ENTRY(input_enable_section),
    FN_ENTRY(input_disable_section),
    FN_ENTRY(input_set_section_mouse_area),
    FN_ENTRY(format_time),
    FN_ENTRY(enable_messages),
    FN_ENTRY(get_wakeup_pipe),
    {0}
};

static const struct fn_entry utils_fns[] = {
    FN_ENTRY(getcwd),
    FN_ENTRY(readdir),
    FN_ENTRY(split_path),
    FN_ENTRY(join_path),
#if HAVE_POSIX_SPAWN || defined(__MINGW32__)
    FN_ENTRY(subprocess),
#endif
    FN_ENTRY(parse_json),
    {0}
};

static void register_package_fns(lua_State *L, char *module,
                                 const struct fn_entry *e)
{
    push_module_table(L, module); // modtable
    for (int n = 0; e[n].name; n++) {
        lua_pushcclosure(L, e[n].fn, 0); // modtable fn
        lua_setfield(L, -2, e[n].name); // modtable
    }
    lua_pop(L, 1); // -
}

static void add_functions(struct script_ctx *ctx)
{
    lua_State *L = ctx->state;

    register_package_fns(L, "mp", main_fns);

    push_module_table(L, "mp"); // mp

    lua_pushinteger(L, 0);
    lua_pushcclosure(L, script_get_property, 1);
    lua_setfield(L, -2, "get_property");

    lua_pushinteger(L, 1);
    lua_pushcclosure(L, script_get_property, 1);
    lua_setfield(L, -2, "get_property_osd");

    lua_pop(L, 1); // -

    register_package_fns(L, "mp.utils", utils_fns);
}

const struct mp_scripting mp_scripting_lua = {
    .file_ext = "lua",
    .load = load_lua,
};
