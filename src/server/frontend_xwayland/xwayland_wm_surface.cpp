/*
 * Copyright (C) 2018 Marius Gripsgard <marius@ubports.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define MIR_LOG_COMPONENT "xwaylandwm"
#include "mir/log.h"
#include "xwayland_log.h"

#include "xwayland_wm_surface.h"

#include "xwayland_wm_shell.h"
#include "xwayland_wm_shellsurface.h"

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <string.h>

#include <map>

namespace mf = mir::frontend;

mf::XWaylandWMSurface::XWaylandWMSurface(XWaylandWM *wm, xcb_window_t window)
    : xwm(wm), window(window), props_dirty(true)
{
    uint32_t values[1];
    xcb_get_geometry_cookie_t geometry_cookie;
    xcb_get_geometry_reply_t *geometry_reply;

    geometry_cookie = xcb_get_geometry(xwm->get_xcb_connection(), window);

    values[0] = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_change_window_attributes(xwm->get_xcb_connection(), window, XCB_CW_EVENT_MASK, values);

    geometry_reply = xcb_get_geometry_reply(xwm->get_xcb_connection(), geometry_cookie, NULL);
    if (geometry_reply == NULL)
        mir::log_error("xcb gemom reply faled");
}

mf::XWaylandWMSurface::~XWaylandWMSurface()
{
    destroyed = true;
}

void mf::XWaylandWMSurface::dirty_properties()
{
    props_dirty = true;
}

void mf::XWaylandWMSurface::set_surface_id(uint32_t id)
{
    surface_id = id;
}

void mf::XWaylandWMSurface::set_surface(WlSurface *wls)
{
    wlsurface = wls;

    auto shell = std::static_pointer_cast<XWaylandWMShell>(xwm->get_wl_connector()->get_extension("x11-support"));
    shell_surface = shell->get_shell_surface(xwm->get_wl_client(), wlsurface);
    shell_surface->set_surface(this);

    if (!properties.title.empty())
      shell_surface->set_title(properties.title);

    shell_surface->set_toplevel();
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_workspace(int workspace)
{
    // Passing a workspace < 0 deletes the property
    if (workspace >= 0)
    {
        xcb_change_property(xwm->get_xcb_connection(), XCB_PROP_MODE_REPLACE, window, xwm->xcb_atom.net_wm_desktop,
                            XCB_ATOM_CARDINAL, 31, 1, &workspace);
    }
    else
    {
        xcb_delete_property(xwm->get_xcb_connection(), window, xwm->xcb_atom.net_wm_desktop);
    }
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_wm_state(WmState state)
{
    uint32_t property[2];
    property[0] = state;
    property[1] = XCB_WINDOW_NONE;

    xcb_change_property(xwm->get_xcb_connection(), XCB_PROP_MODE_REPLACE, window, xwm->xcb_atom.wm_state,
                        xwm->xcb_atom.wm_state, 32, 2, property);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::set_net_wm_state()
{
    uint32_t property[3];
    uint32_t i = 0;

    if (fullscreen)
        property[i++] = xwm->xcb_atom.net_wm_state_fullscreen;
    if (maximized)
    {
        property[i++] = xwm->xcb_atom.net_wm_state_maximized_horz;
        property[i++] = xwm->xcb_atom.net_wm_state_maximized_vert;
    }

    xcb_change_property(xwm->get_xcb_connection(), XCB_PROP_MODE_REPLACE, window, xwm->xcb_atom.net_wm_state,
                        XCB_ATOM_ATOM, 32, i, property);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::read_properties()
{
    if (!props_dirty)
        return;
    props_dirty = false;

    std::map<xcb_atom_t, xcb_atom_t> props;
    props[XCB_ATOM_WM_CLASS] = XCB_ATOM_STRING;
    props[XCB_ATOM_WM_NAME] = XCB_ATOM_STRING;
    props[XCB_ATOM_WM_TRANSIENT_FOR] = XCB_ATOM_WINDOW;
    props[xwm->xcb_atom.wm_protocols] = TYPE_WM_PROTOCOLS;
    props[xwm->xcb_atom.wm_normal_hints] = TYPE_WM_NORMAL_HINTS;
    props[xwm->xcb_atom.net_wm_state] = TYPE_NET_WM_STATE;
    props[xwm->xcb_atom.net_wm_window_type] = XCB_ATOM_ATOM;
    props[xwm->xcb_atom.net_wm_name] = XCB_ATOM_STRING;
    props[xwm->xcb_atom.motif_wm_hints] = TYPE_MOTIF_WM_HINTS;

    std::map<xcb_atom_t, xcb_get_property_cookie_t> cookies;
    for (const auto &atom : props)
    {
        xcb_get_property_cookie_t cookie =
            xcb_get_property(xwm->get_xcb_connection(), 0, window, atom.first, XCB_ATOM_ANY, 0, 2048);
        cookies[atom.first] = cookie;
    }

    properties.deleteWindow = 0;

    if (overrideRedirect)
    {
        decorate = false;
    }

    mir::log_verbose("Properties:");

    for (const auto &atom_ptr : props)
    {
        xcb_atom_t atom = atom_ptr.first;
        xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->get_xcb_connection(), cookies[atom], nullptr);
        if (!reply)
        {
            mir::log_verbose("read_properties: Bad window, usually");
            continue;
        }

        if (reply->type == XCB_ATOM_NONE)
        {
            mir::log_verbose("read_properties: No such info");
            free(reply);
            continue;
        }

        xwm->dump_property(atom, reply);

        switch (props[atom])
        {
        case XCB_ATOM_STRING:
        {
            char *p = strndup(reinterpret_cast<char *>(xcb_get_property_value(reply)),
                              xcb_get_property_value_length(reply));
            if (atom == XCB_ATOM_WM_CLASS) {
                properties.appId = std::string(p);
            } else if (atom == XCB_ATOM_WM_NAME || xwm->xcb_atom.net_wm_name) {
                properties.title = std::string(p);
            } else {
                free(p);
            }
            mir::log_verbose("XCB_ATOM_STRING");
            break;
        }
        case XCB_ATOM_WINDOW:
        {
            mir::log_verbose("XCB_ATOM_WINDOW");
            break;
        }
        case XCB_ATOM_ATOM:
        {
            if (atom == xwm->xcb_atom.net_wm_window_type)
            {
                mir::log_verbose("XCB_ATOM_ATOM net_wm_window_type");
            }
            break;
        }
        case TYPE_WM_PROTOCOLS:
        {
            mir::log_verbose("TYPE_WM_PROTOCOLS");
            xcb_atom_t *atoms = reinterpret_cast<xcb_atom_t *>(xcb_get_property_value(reply));
            for (uint32_t i = 0; i < reply->value_len; ++i)
                if (atoms[i] == xwm->xcb_atom.wm_delete_window)
                    properties.deleteWindow = 1;
            break;
        }
        case TYPE_WM_NORMAL_HINTS:
        {
            mir::log_verbose("TYPE_WM_NORMAL_HINTS");
            break;
        }
        case TYPE_NET_WM_STATE:
        {
            mir::log_verbose("TYPE_NET_WM_STATE");
            xcb_atom_t *value = reinterpret_cast<xcb_atom_t *>(xcb_get_property_value(reply));
            uint32_t i;
            for (i = 0; i < reply->value_len; i++)
            {
                if (value[i] == xwm->xcb_atom.net_wm_state_fullscreen && !fullscreen)
                {
                    fullscreen = true;
                }
            }
            if (value[i] == xwm->xcb_atom.net_wm_state_maximized_horz && !maximized)
            {
                maximized = true;
            }
            if (value[i] == xwm->xcb_atom.net_wm_state_maximized_vert && !maximized)
            {
                maximized = true;
            }
            break;
        }
        case TYPE_MOTIF_WM_HINTS:
            mir::log_verbose("TYPE_MOTIF_WM_HINTS");
            break;
        default:
            break;
        }

        free(reply);
    }
}

void mf::XWaylandWMSurface::move_resize(uint32_t detail)
{
  switch (detail)
  {
  case _NET_WM_MOVERESIZE_MOVE:
      shell_surface->move();
      mir::log_verbose("Move!");
      break;
  case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
  case _NET_WM_MOVERESIZE_SIZE_TOP:
  case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
  case _NET_WM_MOVERESIZE_SIZE_RIGHT:
  case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
  case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
  case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
  case _NET_WM_MOVERESIZE_SIZE_LEFT:
      shell_surface->resize(detail);
      mir::log_verbose("resize!");
      break;
  default:
      break;
  }
}

void mf::XWaylandWMSurface::send_resize(const geometry::Size& new_size)
{
    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t values[2];

    values[0] = new_size.width.as_uint32_t();
    values[1] = new_size.height.as_uint32_t();

    xcb_configure_window(xwm->get_xcb_connection(), window, mask, values);
    xcb_flush(xwm->get_xcb_connection());
}

void mf::XWaylandWMSurface::send_close_request()
{
    xcb_destroy_window(xwm->get_xcb_connection(), window);
    xcb_flush(xwm->get_xcb_connection());
}
