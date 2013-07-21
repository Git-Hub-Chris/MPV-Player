#include <assert.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <libavutil/common.h>

#include "talloc.h"

#include "mp_lua.h"
#include "mp_core.h"
#include "mp_msg.h"
#include "m_property.h"
#include "m_option.h"
#include "command.h"
#include "input/input.h"
#include "sub/sub.h"
#include "osdep/timer.h"

static const char lua_defaults[] =
// Generated from defaults.lua
#include "lua_defaults.h"
;

struct lua_ctx {
    lua_State *state;
    double start_time;
};

static struct MPContext *get_mpctx(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "mpctx");
    struct MPContext *ctx = lua_touserdata(L, -1);
    lua_pop(L, 1);
    assert(ctx);
    return ctx;
}

static int wrap_cpcall(lua_State *L)
{
    lua_CFunction fn = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return fn(L);
}

// Call the given function fn under a Lua error handler (similar to lua_cpcall).
// Pass the given number of args from the Lua stack to fn.
// Returns 0 (and empty stack) on success.
// Returns LUA_ERR[RUN|MEM|ERR] otherwise, with the error value on the stack.
static int mp_cpcall(lua_State *L, lua_CFunction fn, int args)
{
    // Don't use lua_pushcfunction() - it allocates memory on Lua 5.1.
    // Instead, emulate C closures by making wrap_cpcall call fn.
    lua_pushlightuserdata(L, fn); // args... fn
    // Will always succeed if mp_lua_init() set it up correctly.
    lua_getfield(L, LUA_REGISTRYINDEX, "wrap_cpcall"); // args... fn wrap_cpcall
    lua_insert(L, -(args + 2)); // wrap_cpcall args... fn
    return lua_pcall(L, args + 1, 0, 0);
}

static void report_error(lua_State *L)
{
    const char *err = lua_tostring(L, -1);
    mp_msg(MSGT_CPLAYER, MSGL_WARN, "[lua] Error: %s\n",
           err ? err : "[unknown]");
    lua_pop(L, 1);
}

static void add_functions(struct MPContext *mpctx);

void mp_lua_run(struct MPContext *mpctx, const char *source)
{
    if (!mpctx->lua_ctx)
        return;
    lua_State *L = mpctx->lua_ctx->state;
    if (luaL_loadstring(L, source) || lua_pcall(L, 0, 0, 0))
        report_error(L);
    assert(lua_gettop(L) == 0);
}

void mp_lua_load_file(struct MPContext *mpctx, const char *fname)
{
    if (!mpctx->lua_ctx)
        return;
    lua_State *L = mpctx->lua_ctx->state;
    if (luaL_loadfile(L, fname) || lua_pcall(L, 0, 0, 0))
        report_error(L);
    assert(lua_gettop(L) == 0);
}

void mp_lua_init(struct MPContext *mpctx)
{
    mpctx->lua_ctx = talloc(mpctx, struct lua_ctx);
    lua_State *L = mpctx->lua_ctx->state = luaL_newstate();
    if (!L)
        goto error_out;

    // For avoiding wrap arounds and loss of precission
    mpctx->lua_ctx->start_time = mp_time_sec();

    // used by get_mpctx()
    lua_pushlightuserdata(L, mpctx); // mpctx
    lua_setfield(L, LUA_REGISTRYINDEX, "mpctx"); // -

    lua_pushcfunction(L, wrap_cpcall); // closure
    lua_setfield(L, LUA_REGISTRYINDEX, "wrap_cpcall"); // -

    luaL_openlibs(L);

    lua_newtable(L); // mp
    lua_pushvalue(L, -1); // mp mp
    lua_setglobal(L, "mp"); // mp

    add_functions(mpctx); // mp

    lua_pop(L, 1); // -

    if (luaL_loadstring(L, lua_defaults) || lua_pcall(L, 0, 0, 0))
        report_error(L);

    assert(lua_gettop(L) == 0);

    if (mpctx->opts.lua_file)
        mp_lua_load_file(mpctx, mpctx->opts.lua_file);

    return;

error_out:
    if (mpctx->lua_ctx->state)
        lua_close(mpctx->lua_ctx->state);
    talloc_free(mpctx->lua_ctx);
    mpctx->lua_ctx = NULL;
}

void mp_lua_uninit(struct MPContext *mpctx)
{
    if (!mpctx->lua_ctx)
        return;
    lua_close(mpctx->lua_ctx->state);
    talloc_free(mpctx->lua_ctx);
    mpctx->lua_ctx = NULL;
}

static int run_event(lua_State *L)
{
    lua_getglobal(L, "mp_event"); // name arg mp_event
    if (lua_isnil(L, -1))
        return 0;
    lua_insert(L, -3); // mp_event name arg
    lua_call(L, 2, 0);
    return 0;
}

void mp_lua_event(struct MPContext *mpctx, const char *name, const char *arg)
{
    if (!mpctx->lua_ctx)
        return;
    lua_State *L = mpctx->lua_ctx->state;
    lua_pushstring(L, name);
    if (arg) {
        lua_pushstring(L, arg);
    } else {
        lua_pushnil(L);
    }
    if (mp_cpcall(L, run_event, 2) != 0)
        report_error(L);
}

static int run_script_dispatch(lua_State *L)
{
    int id = lua_tointeger(L, 1);
    const char *event = lua_tostring(L, 2);
    lua_getglobal(L, "mp_script_dispatch");
    if (lua_isnil(L, -1))
        return 0;
    lua_pushinteger(L, id);
    lua_pushstring(L, event);
    lua_call(L, 2, 0);
    return 0;
}

void mp_lua_script_dispatch(struct MPContext *mpctx, int id, char *event)
{
    if (!mpctx->lua_ctx)
        return;
    lua_State *L = mpctx->lua_ctx->state;
    lua_pushinteger(L, id);
    lua_pushstring(L, event);
    if (mp_cpcall(L, run_script_dispatch, 2) != 0)
        report_error(L);
}

static int send_command(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    const char *s = luaL_checkstring(L, 1);

    mp_cmd_t *cmd = mp_input_parse_cmd(bstr0((char*)s), "<lua>");
    if (!cmd)
        luaL_error(L, "error parsing command");
    mp_input_queue_cmd(mpctx->input, cmd);

    return 0;
}

static int property_list(lua_State *L)
{
    const struct m_option *props = mp_get_property_list();
    lua_newtable(L);
    for (int i = 0; props[i].name; i++) {
        lua_pushinteger(L, i + 1);
        lua_pushstring(L, props[i].name);
        lua_settable(L, -3);
    }
    return 1;
}

static int property_string(lua_State *L)
{
    const struct m_option *props = mp_get_property_list();
    struct MPContext *mpctx = get_mpctx(L);
    const char *name = luaL_checkstring(L, 1);
    int type = lua_tointeger(L, lua_upvalueindex(1))
               ? M_PROPERTY_PRINT : M_PROPERTY_GET_STRING;

    char *result = NULL;
    if (m_property_do(props, name, type, &result, mpctx) >= 0 && result) {
        lua_pushstring(L, result);
        talloc_free(result);
        return 1;
    }
    if (type == M_PROPERTY_PRINT) {
        lua_pushstring(L, "");
        return 1;
    }
    return 0;
}

static int set_osd_ass(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    int res_x = luaL_checkinteger(L, 1);
    int res_y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    if (!mpctx->osd->external || strcmp(mpctx->osd->external, text) != 0) {
        talloc_free(mpctx->osd->external);
        mpctx->osd->external = talloc_strdup(mpctx->osd, text);
        mpctx->osd->external_res_x = res_x;
        mpctx->osd->external_res_y = res_y;
        osd_changed(mpctx->osd, OSDTYPE_EXTERNAL);
    }
    return 0;
}

static int get_osd_resolution(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    int w, h;
    osd_object_get_resolution(mpctx->osd, mpctx->osd->objs[OSDTYPE_EXTERNAL],
                              &w, &h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int get_screen_size(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    struct osd_object *obj = mpctx->osd->objs[OSDTYPE_EXTERNAL];
    double aspect = 1.0 * obj->vo_res.w / FFMAX(obj->vo_res.h, 1) /
                    obj->vo_res.display_par;
    lua_pushnumber(L, obj->vo_res.w);
    lua_pushnumber(L, obj->vo_res.h);
    lua_pushnumber(L, aspect);
    return 3;
}

static int get_mouse_pos(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    float px, py;
    mp_get_osd_mouse_pos(mpctx, &px, &py);
    double sw, sh;
    osd_object_get_scale_factor(mpctx->osd, mpctx->osd->objs[OSDTYPE_EXTERNAL],
                                &sw, &sh);
    lua_pushnumber(L, px * sw);
    lua_pushnumber(L, py * sh);
    return 2;
}

static int get_timer(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    lua_pushnumber(L, mp_time_sec() - mpctx->lua_ctx->start_time);
    return 1;
}

static int get_chapter_list(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    lua_newtable(L); // list
    int num = get_chapter_count(mpctx);
    for (int n = 0; n < num; n++) {
        double time = chapter_start_time(mpctx, n);
        char *name = chapter_display_name(mpctx, n);
        lua_newtable(L); // list ch
        lua_pushnumber(L, time); // list ch time
        lua_setfield(L, -2, "time"); // list ch
        lua_pushstring(L, name); // list ch name
        lua_setfield(L, -2, "name"); // list ch
        lua_pushinteger(L, n + 1); // list ch n1
        lua_insert(L, -2); // list n1 ch
        lua_settable(L, -3); // list
        talloc_free(name);
    }
    return 1;
}

static const char *stream_type(enum stream_type t)
{
    switch (t) {
    case STREAM_VIDEO: return "video";
    case STREAM_AUDIO: return "audio";
    case STREAM_SUB:   return "sub";
    default:           return "unknown";
    }
}

static int get_track_list(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    lua_newtable(L); // list
    for (int n = 0; n < mpctx->num_tracks; n++) {
        struct track *track = mpctx->tracks[n];
        lua_newtable(L); // list track

        lua_pushstring(L, stream_type(track->type));
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, track->user_tid);
        lua_setfield(L, -2, "id");
        lua_pushboolean(L, track->default_track);
        lua_setfield(L, -2, "default");
        lua_pushboolean(L, track->attached_picture);
        lua_setfield(L, -2, "attached_picture");
        if (track->lang) {
            lua_pushstring(L, track->lang);
            lua_setfield(L, -2, "language");
        }
        lua_pushboolean(L, track->is_external);
        lua_setfield(L, -2, "external");
        if (track->external_filename) {
            lua_pushstring(L, track->external_filename);
            lua_setfield(L, -2, "external_filename");
        }
        lua_pushboolean(L, track->auto_loaded);
        lua_setfield(L, -2, "auto_loaded");

        lua_pushinteger(L, n + 1); // list track n1
        lua_insert(L, -2); // list n1 track
        lua_settable(L, -3); // list
    }
    return 1;
}

static int input_define_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    char *contents = (char *)luaL_checkstring(L, 2);
    mp_input_define_section(mpctx->input, section, "<script>", contents, true);
    return 0;
}

static int input_enable_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    mp_input_enable_section(mpctx->input, section, 0);
    return 0;
}

static int input_disable_section(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);
    char *section = (char *)luaL_checkstring(L, 1);
    mp_input_disable_section(mpctx->input, section);
    return 0;
}

static int input_set_section_mouse_area(lua_State *L)
{
    struct MPContext *mpctx = get_mpctx(L);

    double sw, sh;
    struct osd_object *obj = mpctx->osd->objs[OSDTYPE_EXTERNAL];
    osd_object_get_scale_factor(mpctx->osd, obj, &sw, &sh);

    char *section = (char *)luaL_checkstring(L, 1);
    int x0 = luaL_checkinteger(L, 2) / sw;
    int y0 = luaL_checkinteger(L, 3) / sh;
    int x1 = luaL_checkinteger(L, 4) / sw;
    int y1 = luaL_checkinteger(L, 5) / sh;
    mp_input_set_section_mouse_area(mpctx->input, section, x0, y0, x1, y1);
    return 0;
}

// On stack: mp table
static void add_functions(struct MPContext *mpctx)
{
    lua_State *L = mpctx->lua_ctx->state;

    lua_pushcfunction(L, send_command);
    lua_setfield(L, -2, "send_command");

    lua_pushcfunction(L, property_list);
    lua_setfield(L, -2, "property_list");

    lua_pushinteger(L, 0);
    lua_pushcclosure(L, property_string, 1);
    lua_setfield(L, -2, "property_get");

    lua_pushinteger(L, 1);
    lua_pushcclosure(L, property_string, 1);
    lua_setfield(L, -2, "property_get_string");

    lua_pushcfunction(L, set_osd_ass);
    lua_setfield(L, -2, "set_osd_ass");

    lua_pushcfunction(L, get_osd_resolution);
    lua_setfield(L, -2, "get_osd_resolution");

    lua_pushcfunction(L, get_screen_size);
    lua_setfield(L, -2, "get_screen_size");

    lua_pushcfunction(L, get_mouse_pos);
    lua_setfield(L, -2, "get_mouse_pos");

    lua_pushcfunction(L, get_timer);
    lua_setfield(L, -2, "get_timer");

    lua_pushcfunction(L, get_chapter_list);
    lua_setfield(L, -2, "get_chapter_list");

    lua_pushcfunction(L, get_track_list);
    lua_setfield(L, -2, "get_track_list");

    lua_pushcfunction(L, input_define_section);
    lua_setfield(L, -2, "input_define_section");

    lua_pushcfunction(L, input_enable_section);
    lua_setfield(L, -2, "input_enable_section");

    lua_pushcfunction(L, input_disable_section);
    lua_setfield(L, -2, "input_disable_section");

    lua_pushcfunction(L, input_set_section_mouse_area);
    lua_setfield(L, -2, "input_set_section_mouse_area");
}
