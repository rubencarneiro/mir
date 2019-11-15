/*
 * Copyright © 2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#ifndef  MIR_GRAPHICS_WAYLAND_BUFFER_ALLOCATOR_
#define  MIR_GRAPHICS_WAYLAND_BUFFER_ALLOCATOR_

#include <mir/graphics/egl_extensions.h>
#include <mir/graphics/graphic_buffer_allocator.h>
#include <mir/graphics/wayland_allocator.h>
#include <mir/renderer/gl/context.h>

#include <memory>

namespace mir
{
namespace graphics
{
class Display;

namespace wayland
{
class BufferAllocator: public GraphicBufferAllocator,
                       public WaylandAllocator
{
public:
    BufferAllocator(graphics::Display const& output);

    std::shared_ptr<Buffer> alloc_buffer(BufferProperties const& buffer_properties) override;

    std::shared_ptr<Buffer> alloc_software_buffer(geometry::Size size, MirPixelFormat format) override;
    std::shared_ptr<Buffer> alloc_buffer(geometry::Size size, uint32_t native_format, uint32_t native_flags) override;

    void bind_display(wl_display* display, std::shared_ptr<Executor> wayland_executor) override;

    auto buffer_from_resource(
        wl_resource* buffer,
        std::function<void()>&& on_consumed,
        std::function<void()>&& on_release) -> std::shared_ptr<Buffer> override;

    std::vector<MirPixelFormat> supported_pixel_formats() override;

private:
    std::shared_ptr<Executor> wayland_executor;
    std::shared_ptr<EGLExtensions> const egl_extensions;
    std::shared_ptr<renderer::gl::Context> const ctx;
};
}
}
}

#endif //  MIR_GRAPHICS_WAYLAND_BUFFER_ALLOCATOR_
