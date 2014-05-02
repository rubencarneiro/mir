/*
 * Copyright © 2012 Canonical Ltd.
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
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 *              Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "default_display_buffer_compositor.h"

#include "mir/compositor/scene.h"
#include "mir/compositor/renderer.h"
#include "mir/graphics/renderable.h"
#include "mir/graphics/display_buffer.h"
#include "mir/graphics/buffer.h"
#include "mir/graphics/cursor.h"
#include "mir/compositor/buffer_stream.h"
#include "bypass.h"
#include "occlusion.h"
#include <mutex>
#include <cstdlib>
#include <algorithm>

namespace mc = mir::compositor;
namespace mg = mir::graphics;
using namespace mir;

namespace
{

class SoftCursor : public mg::Cursor
{
public:
    SoftCursor(mc::DefaultDisplayBufferCompositor& compositor)
        : compositor(compositor)
    {
    }

    void set_image(mg::CursorImage const&) override
    {
        // TODO: Implement software cursor image setting later
    }

    void move_to(geometry::Point position) override
    {
        compositor.on_cursor_movement(position);
    }

private:
    mc::DefaultDisplayBufferCompositor& compositor;
};

}

mc::DefaultDisplayBufferCompositor::DefaultDisplayBufferCompositor(
    mg::DisplayBuffer& display_buffer,
    std::shared_ptr<mc::Scene> const& scene,
    std::shared_ptr<mc::Renderer> const& renderer,
    std::shared_ptr<mc::CompositorReport> const& report)
    : display_buffer(display_buffer),
      scene{scene},
      renderer{renderer},
      report{report},
      soft_cursor{std::make_shared<SoftCursor>(*this)},
      last_pass_rendered_anything{false},
      viewport(display_buffer.view_area()),
      zoom_mag{1.0f}
{
}

bool mc::DefaultDisplayBufferCompositor::composite()
{
    report->began_frame(this);

    bool bypassed = false;
    auto renderable_list = scene->renderable_list_for(this);
    mc::filter_occlusions_from(renderable_list, viewport);

    //TODO: the DisplayBufferCompositor should not have to figure out if it has to force
    //      a subsequent compositon. The MultiThreadedCompositor should be smart enough to 
    //      schedule compositions when they're needed. 
    bool uncomposited_buffers{false};
    for(auto const& renderable : renderable_list)
        uncomposited_buffers |= (renderable->buffers_ready_for_compositor() > 1);

    if (viewport == display_buffer.view_area() &&
        display_buffer.can_bypass())
    {
        mc::BypassMatch bypass_match(viewport);

        auto bypass_it = std::find_if(renderable_list.rbegin(), renderable_list.rend(), bypass_match);
        if (bypass_it != renderable_list.rend())
        {
            auto bypass_buf = (*bypass_it)->buffer();
            if (bypass_buf->can_bypass())
            {
                display_buffer.post_update(bypass_buf);
                bypassed = true;
                renderer->suspend();
            }
        }
    }

    if (!bypassed)
    {
        display_buffer.make_current();

        renderer->set_rotation(display_buffer.orientation());
        renderer->set_viewport(viewport);
        renderer->begin();

        for(auto const& renderable : renderable_list)
            renderer->render(*renderable);

        display_buffer.post_update();
        renderer->end();

        // This is a frig to avoid lp:1286190
        if (last_pass_rendered_anything && renderable_list.empty())
            uncomposited_buffers = true;

        last_pass_rendered_anything = !renderable_list.empty();
        // End of frig
    }

    report->finished_frame(bypassed, this);
    return uncomposited_buffers;
}

std::weak_ptr<graphics::Cursor>
mc::DefaultDisplayBufferCompositor::cursor() const
{
    return soft_cursor;
}

void mc::DefaultDisplayBufferCompositor::on_cursor_movement(
    geometry::Point const& p)
{
    cursor_pos = p;
    if (zoom_mag != 1.0f)
        update_viewport();
}

void mc::DefaultDisplayBufferCompositor::zoom(float mag)
{
    zoom_mag = mag;
    update_viewport();
}

void mc::DefaultDisplayBufferCompositor::update_viewport()
{
    auto const& view_area = display_buffer.view_area();

    if (zoom_mag == 1.0f)
    {
        // The below calculations should yield the same result as this, but
        // just in case there are any floating point precision errors,
        // set it precisely:
        viewport = view_area;
    }
    else
    {
        int db_width = view_area.size.width.as_int();
        int db_height = view_area.size.height.as_int();
        int db_x = view_area.top_left.x.as_int();
        int db_y = view_area.top_left.y.as_int();
    
        float zoom_width = db_width / zoom_mag;
        float zoom_height = db_height / zoom_mag;
    
        float screen_x = cursor_pos.x.as_int() - db_x;
        float screen_y = cursor_pos.y.as_int() - db_y;

        float normal_x = screen_x / db_width;
        float normal_y = screen_y / db_height;
    
        // Position the viewport so the cursor location matches up.
        // This assumes the hardware cursor still traverses the physical
        // screen and isn't being warped.
        int zoom_x = db_x + (db_width - zoom_width) * normal_x;
        int zoom_y = db_y + (db_height - zoom_height) * normal_y;

        viewport = {{zoom_x, zoom_y}, {zoom_width, zoom_height}};
    }
}
