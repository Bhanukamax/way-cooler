/*
 * widget.c - widget managing
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
 * Copyright © 2007 Aldo Cortesi <aldo@nullcube.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <math.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

#include "widget.h"
#include "statusbar.h"
#include "titlebar.h"
#include "event.h"
#include "screen.h"
#include "lua.h"

extern awesome_t globalconf;

#include "widgetgen.h"

/** Compute offset for drawing the first pixel of a widget.
 * \param barwidth The statusbar width.
 * \param widgetwidth The widget width.
 * \param alignment The widget alignment on statusbar.
 * \return The x coordinate to draw at.
 */
int
widget_calculate_offset(int barwidth, int widgetwidth, int offset, int alignment)
{
    switch(alignment)
    {
      case AlignLeft:
      case AlignFlex:
        return offset;
    }
    return barwidth - offset - widgetwidth;
}

/** Find a widget on a screen by its name.
 * \param name The widget name.
 * \return A widget pointer.
 */
widget_t *
widget_getbyname(const char *name)
{
    widget_node_t *widget;
    statusbar_t *sb;
    int screen;

    for(screen = 0; screen < globalconf.screens_info->nscreen; screen++)
        for(sb = globalconf.screens[screen].statusbar; sb; sb = sb->next)
            for(widget = sb->widgets; widget; widget = widget->next)
                if(!a_strcmp(name, widget->widget->name))
                    return widget->widget;

    return NULL;
}

/** Common function for button press event on widget.
 * It will look into configuration to find the callback function to call.
 * \param w The widget node.
 * \param ev The button press event the widget received.
 * \param screen The screen number.
 * \param p The object where user clicked.
 */
static void
widget_common_button_press(widget_node_t *w,
                           xcb_button_press_event_t *ev,
                           int screen __attribute__ ((unused)),
                           void *p __attribute__ ((unused)))
{
    button_t *b;

    for(b = w->widget->buttons; b; b = b->next)
        if(ev->detail == b->button && CLEANMASK(ev->state) == b->mod && b->fct)
            luaA_dofunction(globalconf.L, b->fct, 0);
}

/** Common tell function for widget, which only warn user that widget
 * cannot be told anything.
 * \param widget The widget.
 * \param new_value Unused argument.
 * \return The status of the command, which is always an error in this case.
 */
static widget_tell_status_t
widget_common_tell(widget_t *widget,
                   const char *property __attribute__ ((unused)),
                   const char *new_value __attribute__ ((unused)))
{
    warn("%s widget does not accept commands.", widget->name);
    return WIDGET_ERROR_CUSTOM;
}

/** Render a list of widgets.
 * \param wnode The list of widgets.
 * \param ctx The draw context where to render.
 * \param rotate_dw The rotate drawable: where to rotate and render the final
 * \param screen The logical screen used to render.
 * \param position The object position.
 * \param x The x coordinates of the object.
 * \param y The y coordinates of the object.
 * drawable when the object position is right or left.
 * \param object The object pointer.
 * \todo Remove GC.
 */
void
widget_render(widget_node_t *wnode, draw_context_t *ctx, xcb_gcontext_t gc, xcb_drawable_t rotate_dw,
              int screen, position_t position,
              int x, int y, void *object)
{
    xcb_window_t rootwin;
    xcb_pixmap_t rootpix;
    widget_node_t *w;
    int left = 0, right = 0;
    char *data;
    xcb_get_property_reply_t *prop_r;
    xcb_get_property_cookie_t prop_c;
    area_t rectangle = { 0, 0, 0, 0, NULL, NULL }, rootsize;
    xcb_atom_t rootpix_atom, pixmap_atom;
    xutil_intern_atom_request_t rootpix_atom_req, pixmap_atom_req;

    /* Send requests needed for transparency */
    if(ctx->bg.alpha != 0xffff)
    {
        pixmap_atom_req = xutil_intern_atom(globalconf.connection, &globalconf.atoms, "PIXMAP");
        rootpix_atom_req = xutil_intern_atom(globalconf.connection, &globalconf.atoms, "_XROOTPMAP_ID");
    }

    rectangle.width = ctx->width;
    rectangle.height = ctx->height;

    if(ctx->bg.alpha != 0xffff)
    {
        rootwin = xcb_aux_get_screen(globalconf.connection, ctx->phys_screen)->root;
        pixmap_atom = xutil_intern_atom_reply(globalconf.connection, &globalconf.atoms, pixmap_atom_req);
        rootpix_atom = xutil_intern_atom_reply(globalconf.connection, &globalconf.atoms, rootpix_atom_req);
        prop_c = xcb_get_property_unchecked(globalconf.connection, false, rootwin, rootpix_atom,
                                            pixmap_atom, 0, 1);
        if((prop_r = xcb_get_property_reply(globalconf.connection, prop_c, NULL)))
        {
            if((data = xcb_get_property_value(prop_r)))
            {
               rootpix = *(xcb_pixmap_t *) data;
                rootsize = get_display_area(ctx->phys_screen, NULL, NULL);
               switch(position)
               {
                 case Left:
                   draw_rotate(ctx,
                               rootpix, ctx->drawable,
                               rootsize.width, rootsize.height,
                               ctx->width, ctx->height,
                               M_PI_2,
                               y + ctx->width,
                               - x);
                   break;
                 case Right:
                   draw_rotate(ctx,
                               rootpix, ctx->drawable,
                               rootsize.width, rootsize.height,
                               ctx->width, ctx->height,
                               - M_PI_2,
                               - y,
                               x + ctx->height);
                   break;
                 default:
                   xcb_copy_area(globalconf.connection, rootpix,
                                 rotate_dw, gc,
                                 x, y,
                                 0, 0, 
                                 ctx->width, ctx->height);
                   break;
               }
            }
            p_delete(&prop_r);
        }
    }

    draw_rectangle(ctx, rectangle, 1.0, true, ctx->bg);

    for(w = wnode; w; w = w->next)
        if(w->widget->isvisible && w->widget->align == AlignLeft)
            left += w->widget->draw(ctx, screen, w, left, (left + right), object);

    /* renders right widget from last to first */
    for(w = *widget_node_list_last(&wnode); w; w = w->prev)
        if(w->widget->isvisible && w->widget->align == AlignRight)
            right += w->widget->draw(ctx, screen, w, right, (left + right), object);

    for(w = wnode; w; w = w->next)
        if(w->widget->isvisible && w->widget->align == AlignFlex)
            left += w->widget->draw(ctx, screen, w, left, (left + right), object);

    switch(position)
    {
        case Right:
          draw_rotate(ctx, ctx->drawable, rotate_dw,
                      ctx->width, ctx->height,
                      ctx->height, ctx->width,
                      M_PI_2, ctx->height, 0);
          break;
        case Left:
          draw_rotate(ctx, ctx->drawable, rotate_dw,
                      ctx->width, ctx->height,
                      ctx->height, ctx->width,
                      - M_PI_2, 0, ctx->width);
          break;
        default:
          break;
    }
}


/** Common function for creating a widget.
 * \param widget The allocated widget.
 */
void
widget_common_new(widget_t *widget)
{
    widget->align = AlignLeft;
    widget->tell = widget_common_tell;
    widget->button_press = widget_common_button_press;
}

/** Invalidate widgets which should be refresh upon
 * external modifications. widget_t who watch flags will
 * be set to be refreshed.
 * \param screen Virtual screen number.
 * \param flags Cache flags to invalidate.
 */
void
widget_invalidate_cache(int screen, int flags)
{
    statusbar_t *statusbar;
    widget_node_t *widget;

    for(statusbar = globalconf.screens[screen].statusbar;
        statusbar;
        statusbar = statusbar->next)
        for(widget = statusbar->widgets; widget; widget = widget->next)
            if(widget->widget->cache_flags & flags)
            {
                statusbar->need_update = true;
                break;
            }
}

/** Set a statusbar needs update because it has widget, or redraw a titlebar.
 * \todo Probably needs more optimization.
 * \param widget The widget to look for.
 */
static void
widget_invalidate_bywidget(widget_t *widget)
{
    int screen;
    statusbar_t *statusbar;
    widget_node_t *witer;
    client_t *c;

    for(screen = 0; screen < globalconf.screens_info->nscreen; screen++)
        for(statusbar = globalconf.screens[screen].statusbar;
            statusbar;
            statusbar = statusbar->next)
            for(witer = statusbar->widgets; witer; witer = witer->next)
                if(witer->widget == widget)
                {
                    statusbar->need_update = true;
                    break;
                }

    for(c = globalconf.clients; c; c = c->next)
        if(c->titlebar)
            for(witer = c->titlebar->widgets; witer; witer = witer->next)
                if(witer->widget == widget)
                    titlebar_draw(c);
}

/** Create a new widget userdata. The object is pushed on the stack.
 * \param widget The widget.
 * \return Return luaA_settype() return value.
 */
static int
luaA_widget_userdata_new(widget_t *widget)
{
    widget_t **w;
    w = lua_newuserdata(globalconf.L, sizeof(widget_t *));
    *w = widget;
    widget_ref(w);
    return luaA_settype(globalconf.L, "widget");
}

/** Create a new widget.
 * \param A table with at least a name and a type value. Optionnal attributes
 * are: align.
 * \return A brand new widget.
 */
static int
luaA_widget_new(lua_State *L)
{
    const char *type;
    widget_t *w = NULL;
    widget_constructor_t *wc;
    alignment_t align;

    luaA_checktable(L, 1);
    align = draw_align_get_from_str(luaA_getopt_string(L, 1, "align", "left"));

    type = luaA_getopt_string(L, 1, "type", NULL);

    if((wc = name_func_lookup(type, WidgetList)))
        w = wc(align);
    else
        luaL_error(L, "unkown widget type: %s", type);

    /* Set visible by default. */
    w->isvisible = true;

    w->name = luaA_name_init(L);

    return luaA_widget_userdata_new(w);
}

/** Add a mouse button bindings to a widget.
 * \param A table containing modifiers keys.
 * \param A button number.
 * \param A function to execute. Some widgets may passe arguments to this
 * function.
 */
static int
luaA_widget_mouse(lua_State *L)
{
    size_t i, len;
    /* arg 1 is object */
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    int b;
    button_t *button;

    /* arg 2 is modkey table */
    luaA_checktable(L, 2);
    /* arg 3 is mouse button */
    b = luaL_checknumber(L, 3);
    /* arg 4 is cmd to run */
    luaA_checkfunction(L, 4);

    button = p_new(button_t, 1);
    button->button = xutil_button_fromint(b);
    button->fct = luaL_ref(L, LUA_REGISTRYINDEX);

    len = lua_objlen(L, 2);
    for(i = 1; i <= len; i++)
    {
        lua_rawgeti(L, 2, i);
        button->mod |= xutil_keymask_fromstr(luaL_checkstring(L, -1));
    }

    button_list_push(&(*widget)->buttons, button);

    return 0;
}

/** Do what should be done with a widget_tell_status_t for a widget.
 * \param widget The widget.
 * \param status The status returned by the tell function of the widget.
 * \para property The property updated.
 */
void
widget_tell_managestatus(widget_t *widget, widget_tell_status_t status, const char *property)
{
    switch(status)
    {
      case WIDGET_ERROR:
        warn("error changing property %s of widget %s",
             property, widget->name);
        break;
      case WIDGET_ERROR_NOVALUE:
        warn("error changing property %s of widget %s, no value given",
              property, widget->name);
        break;
      case WIDGET_ERROR_FORMAT_FONT:
        warn("error changing property %s of widget %s, must be a valid font",
             property, widget->name);
        break;
      case WIDGET_ERROR_FORMAT_COLOR:
        warn("error changing property %s of widget %s, must be a valid color",
             property, widget->name);
        break;
      case WIDGET_ERROR_FORMAT_SECTION:
        warn("error changing property %s of widget %s, section/title not found",
             property, widget->name);
        break;
      case WIDGET_NOERROR:
        widget_invalidate_bywidget(widget);
        break;
      case WIDGET_ERROR_CUSTOM:
        break;
    }
}

/** Set a widget property. Each widget type has its own set of property.
 * \param The property name.
 * \param The property value.
 */
static int
luaA_widget_set(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    const char *property, *value;
    widget_tell_status_t status;

    property = luaL_checkstring(L, 2);
    value = luaL_checkstring(L, 3);

    status = (*widget)->tell(*widget, property, value);
    widget_tell_managestatus(*widget, status, property);
    return 0;
}

/** Handle widget garbage collection.
 */
static int
luaA_widget_gc(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    widget_unref(widget);
    return 0;
}

/** Convert a widget into a printable string.
 */
static int
luaA_widget_tostring(lua_State *L)
{
    widget_t **p = luaL_checkudata(L, 1, "widget");
    lua_pushfstring(L, "[widget udata(%p) name(%s)]", *p, (*p)->name);
    return 1;
}

/** Check for widget equality.
 * \param Another widget.
 */
static int
luaA_widget_eq(lua_State *L)
{
    widget_t **t1 = luaL_checkudata(L, 1, "widget");
    widget_t **t2 = luaL_checkudata(L, 2, "widget");
    lua_pushboolean(L, (*t1 == *t2));
    return 1;
}

/** Get all widget from all statusbars.
 * \return A table with all widgets from all statusbars.
 */
static int
luaA_widget_get(lua_State *L)
{
    statusbar_t *sb;
    widget_node_t *widget;
    int i = 1, screen;
    bool add = true;
    widget_node_t *wlist = NULL, *witer;

    lua_newtable(L);

    for(screen = 0; screen < globalconf.screens_info->nscreen; screen++)
        for(sb = globalconf.screens[screen].statusbar; sb; sb = sb->next)
            for(widget = sb->widgets; widget; widget = widget->next)
            {
                for(witer = wlist; witer; witer = witer->next)
                    if(witer->widget == widget->widget)
                    {
                        add = false;
                        break;
                    }
                if(add)
                {
                    witer = p_new(widget_node_t, 1);
                    widget_node_list_push(&wlist, witer);
                    luaA_widget_userdata_new(witer->widget);
                    /* ref again for the list */
                    widget_ref(&widget->widget);
                    lua_rawseti(L, -2, i++);
                }
                add = true;
            }

    widget_node_list_wipe(&wlist);

    return 1;
}

/** Set the widget name.
 * \param A string with the new widget name.
 */
static int
luaA_widget_name_set(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    const char *name = luaL_checkstring(L, 2);
    p_delete(&(*widget)->name);
    (*widget)->name = a_strdup(name);
    return 0;
}

/** Get the widget name.
 * \return A string with the name of the widget.
 */
static int
luaA_widget_name_get(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    lua_pushstring(L, (*widget)->name);
    return 1;
}

/** Set the visible attribute of a widget. If a widget is not visible, it is not
 * drawn on the statusbar.
 * \param A boolean value.
 */
static int
luaA_widget_visible_set(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    (*widget)->isvisible = luaA_checkboolean(L, 2);
    widget_invalidate_bywidget(*widget);
    return 0;
}

/** Get the visible attribute of a widget.
 * \return A boolean value, true if the widget is visible, false otherwise.
 */
static int
luaA_widget_visible_get(lua_State *L)
{
    widget_t **widget = luaL_checkudata(L, 1, "widget");
    lua_pushboolean(L, (*widget)->isvisible);
    return 1;
}

const struct luaL_reg awesome_widget_methods[] =
{
    { "new", luaA_widget_new },
    { "get", luaA_widget_get },
    { NULL, NULL }
};
const struct luaL_reg awesome_widget_meta[] =
{
    { "mouse", luaA_widget_mouse },
    { "set", luaA_widget_set },
    { "name_set", luaA_widget_name_set },
    { "name_get", luaA_widget_name_get },
    { "visible_set", luaA_widget_visible_set },
    { "visible_get", luaA_widget_visible_get },
    { "__gc", luaA_widget_gc },
    { "__eq", luaA_widget_eq },
    { "__tostring", luaA_widget_tostring },
    { NULL, NULL }
};

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
