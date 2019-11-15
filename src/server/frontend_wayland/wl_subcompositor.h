/*
 * Copyright © 2018 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
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
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 *              William Wold <william.wold@canonical.com>
 */

#ifndef MIR_FRONTEND_WL_SUBSURFACE_H
#define MIR_FRONTEND_WL_SUBSURFACE_H

#include "wayland_wrapper.h"
#include "wl_surface_role.h"
#include "wl_surface.h"

#include <vector>
#include <memory>

namespace mir
{
namespace shell
{
class StreamSpecification;
}
namespace frontend
{

class WlSurface;
class WlSubcompositorInstance;

class WlSubcompositor : wayland::Subcompositor::Global
{
public:
    WlSubcompositor(wl_display* display);

private:
    void bind(wl_resource* new_wl_subcompositor);
};

class WlSubsurface: public WlSurfaceRole, wayland::Subsurface
{
public:
    WlSubsurface(wl_resource* new_subsurface, WlSurface* surface, WlSurface* parent_surface);
    ~WlSubsurface();

    void populate_surface_data(std::vector<shell::StreamSpecification>& buffer_streams,
                               std::vector<mir::geometry::Rectangle>& input_shape_accumulator,
                               geometry::Displacement const& parent_offset) const;

    geometry::Displacement total_offset() const override { return parent->total_offset(); }
    bool synchronized() const override;
    auto scene_surface() const -> std::experimental::optional<std::shared_ptr<scene::Surface>> override;

    void parent_has_committed();

    WlSurface::Position transform_point(geometry::Point point);

private:
    void set_position(int32_t x, int32_t y) override;
    void place_above(struct wl_resource* sibling) override;
    void place_below(struct wl_resource* sibling) override;
    void set_sync() override;
    void set_desync() override;

    void destroy() override; // overrides function in both WlSurfaceRole and wayland::Subsurface

    void refresh_surface_data_now() override;
    virtual void commit(WlSurfaceState const& state) override;
    virtual void visiblity(bool visible) override;

    WlSurface* const surface;
    // manages parent/child relationship, but does not manage parent's memory
    // see WlSurface::add_child() for details
    std::unique_ptr<WlSurface, std::function<void(WlSurface*)>> const parent;
    std::shared_ptr<bool> const parent_destroyed;
    bool synchronized_;
    std::experimental::optional<WlSurfaceState> cached_state;
};

}
}

#endif // MIR_FRONTEND_WL_SUBSURFACE_H
