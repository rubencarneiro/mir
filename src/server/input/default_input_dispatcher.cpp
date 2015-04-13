/*
 * Copyright © 2015 Canonical Ltd.
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
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */

#include "default_input_dispatcher.h"

#include "mir/input/scene.h"
#include "mir/input/surface.h"
#include "mir/scene/observer.h"
#include "mir/events/event_builders.h"
#include "mir/events/event_private.h"

#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace mi = mir::input;
namespace ms = mir::scene;
namespace mev = mir::events;
namespace geom = mir::geometry;

namespace
{
class InputDispatcherSceneObserver : public ms::Observer
{
    void surface_added(ms::Surface* surface)
    {
        (void) surface;
    }
    void surface_removed(ms::Surface* surface)
    {
        (void) surface;
    }
    void surfaces_reordered()
    {
    }
    void scene_changed()
    {
    }

    void surface_exists(ms::Surface* surface)
    {
        (void) surface;
    }
    void end_observation()
    {
    }
};
}

mi::DefaultInputDispatcher::DefaultInputDispatcher(std::shared_ptr<mi::Scene> const& scene)
    : scene(scene)
{
    scene_observer = std::make_shared<InputDispatcherSceneObserver>();
    scene->add_observer(scene_observer);
}

mi::DefaultInputDispatcher::~DefaultInputDispatcher()
{
    scene->remove_observer(scene_observer);
}

void mi::DefaultInputDispatcher::configuration_changed(std::chrono::nanoseconds when)
{
    (void) when;
}

void mi::DefaultInputDispatcher::device_reset(int32_t device_id, std::chrono::nanoseconds when)
{
    (void) device_id;
    (void) when;
}

bool mi::DefaultInputDispatcher::dispatch_key(MirInputDeviceId id, MirKeyboardEvent const* kev)
{
    std::lock_guard<std::mutex> lg(dispatcher_mutex);
    auto strong_focus = focus_surface.lock();
    if (!strong_focus)
        return false;

    if (!focus_surface_key_input_state.handle_event(id, kev))
        return false;

    deliver(strong_focus, kev);

    return true;
}

namespace
{
bool is_gesture_terminator(MirPointerEvent const* pev)
{
    auto const buttons = {
        mir_pointer_button_primary,
        mir_pointer_button_secondary,
        mir_pointer_button_tertiary,
        mir_pointer_button_back,
        mir_pointer_button_forward
    };
    for (auto button : buttons)
        if (mir_pointer_event_button_state(pev, button)) return false;
    return mir_pointer_event_action(pev) == mir_pointer_action_button_up;
}
}

void mi::DefaultInputDispatcher::deliver(std::shared_ptr<mi::Surface> const& surface, MirTouchEvent const* tev)
{
    deliver(surface, reinterpret_cast<MirEvent const*>(tev));
}

void mi::DefaultInputDispatcher::deliver(std::shared_ptr<mi::Surface> const& surface, MirPointerEvent const* pev)
{
    deliver(surface, reinterpret_cast<MirEvent const*>(pev));
}

void mi::DefaultInputDispatcher::deliver(std::shared_ptr<mi::Surface> const& surface, MirKeyboardEvent const* kev)
{
    deliver(surface, reinterpret_cast<MirEvent const*>(kev));
}

void mi::DefaultInputDispatcher::deliver(std::shared_ptr<mi::Surface> const& surface, MirEvent const* ev)
{
    // TODO: A little weird to use deprecated API
    if (ev->type == mir_event_type_motion)
    {
        auto sx = surface->input_bounds().top_left.x.as_int();
        auto sy = surface->input_bounds().top_left.y.as_int();

        // TODO: I guess mir_event_copy is the answer
        auto not_const_ev = const_cast<MirEvent*>(ev);
        for (unsigned i = 0; i < ev->motion.pointer_count; i++)
        {
            not_const_ev->motion.pointer_coordinates[i].x -= sx;
            not_const_ev->motion.pointer_coordinates[i].y -= sy;
        }
    }
    surface->consume(*ev);
}

std::shared_ptr<mi::Surface> mi::DefaultInputDispatcher::find_target_surface(geom::Point const& point)
{
    std::shared_ptr<mi::Surface> top_target = nullptr;
    scene->for_each([&top_target, &point](std::shared_ptr<mi::Surface> const& target) {
            if (target->input_area_contains(point))
                top_target = target;
    });
    return top_target;
}

// TODO: TIMESTAMP
// TODO: MODIFIERS
// TODO: BUTTONS
// TODO: HSCROLL
// TODO: VSCROLL
// (Obviously copy event)    
void mi::DefaultInputDispatcher::send_enter_exit_event(std::shared_ptr<mi::Surface> const& surface,
    MirInputDeviceId id, MirPointerAction action, geom::Point const& point)
{
    deliver(surface, &*mev::make_event(id, 0, 0, action, {}, point.x.as_int(), point.y.as_int(), 0, 0));
}

mi::DefaultInputDispatcher::PointerInputState& mi::DefaultInputDispatcher::ensure_pointer_state(MirInputDeviceId id)
{
    if (pointer_state_by_id.find(id) == pointer_state_by_id.end())
    {
        pointer_state_by_id[id] = { nullptr, nullptr };
    }
    return pointer_state_by_id[id];
}

mi::DefaultInputDispatcher::TouchInputState& mi::DefaultInputDispatcher::ensure_touch_state(MirInputDeviceId id)
{
    if (touch_state_by_id.find(id) == touch_state_by_id.end())
    {
        touch_state_by_id[id] = { nullptr };
    }
    return touch_state_by_id[id];
}

// TODO: Handle device reset
bool mi::DefaultInputDispatcher::dispatch_pointer(MirInputDeviceId id, MirPointerEvent const* pev)
{
    std::lock_guard<std::mutex> lg(dispatcher_mutex);
    auto action = mir_pointer_event_action(pev);
    auto& pointer_state = ensure_pointer_state(id);
    geom::Point event_x_y = { mir_pointer_event_axis_value(pev,mir_pointer_axis_x),
                              mir_pointer_event_axis_value(pev,mir_pointer_axis_y) };

    if (pointer_state.gesture_owner)
    {
        deliver(pointer_state.gesture_owner, pev);

        if (is_gesture_terminator(pev))
        {
            pointer_state.gesture_owner.reset();

            // TODO: Deduplicate a little
            auto target = find_target_surface(event_x_y);
            printf("No buttons pressed %p %p\n", (void*)pointer_state.current_target.get(), (void*)target.get());

            if (pointer_state.current_target != target)
            {
                if (pointer_state.current_target)
                    send_enter_exit_event(pointer_state.current_target, id, mir_pointer_action_leave, event_x_y);

                pointer_state.current_target = target;
                if (target)
                    send_enter_exit_event(target, id, mir_pointer_action_enter, event_x_y);
            }
        }

        return true;
    }
    else if (action == mir_pointer_action_button_up)
    {
        // If we have an up but no gesture owner
        // then we never delivered the corresponding
        // down to anyone so we drop this event.
        return false;
    }
    else
    {
        auto target = find_target_surface(event_x_y);
        bool sent_ev = false;
        if (pointer_state.current_target != target)
        {
            if (pointer_state.current_target)
                send_enter_exit_event(pointer_state.current_target, id, mir_pointer_action_leave, event_x_y);

            pointer_state.current_target = target;
            if (target)
                send_enter_exit_event(target, id, mir_pointer_action_enter, event_x_y);

            sent_ev = true;
        }
        if (!target)
            return sent_ev;
        if (action == mir_pointer_action_button_down)
        {
            pointer_state.gesture_owner = target;
        }

        deliver(target, pev);
        return true;
    }
    return false;
}

bool mi::DefaultInputDispatcher::dispatch_touch(MirInputDeviceId id, MirTouchEvent const* tev)
{
    std::lock_guard<std::mutex> lg(dispatcher_mutex);
    // Either we have a gesture owner or a gesture is just beginning

    auto point_count = mir_touch_event_point_count(tev);
    
    auto& touch_state = ensure_touch_state(id);
    if (touch_state.gesture_owner)
    {
        deliver(touch_state.gesture_owner, tev);
        if (point_count == 1 && mir_touch_event_action(tev, 0) == mir_touch_action_up)
        {
            // Last touch is coming up. Gesture is over.
            touch_state.gesture_owner = nullptr;
        }
        return true;
    }

    if (point_count == 1 && mir_touch_event_action(tev, 0) == mir_touch_action_down)
    {
        geom::Point event_x_y = { mir_touch_event_axis_value(tev, 0, mir_touch_axis_x),
                                  mir_touch_event_axis_value(tev, 0, mir_touch_axis_y) };

        auto target = find_target_surface(event_x_y);
        if (target)
        {
            touch_state.gesture_owner = target;
            deliver(target, tev);
            return true;
        }
    }
        
    return false;
}

bool mi::DefaultInputDispatcher::dispatch(MirEvent const& event)
{
    if (mir_event_get_type(&event) != mir_event_type_input)
        BOOST_THROW_EXCEPTION(std::logic_error("InputDispatcher got a non-input event"));
    auto iev = mir_event_get_input_event(&event);
    auto id = mir_input_event_get_device_id(iev);
    switch (mir_input_event_get_type(iev))
    {
    case mir_input_event_type_key:
        return dispatch_key(id, mir_input_event_get_keyboard_event(iev));
    case mir_input_event_type_touch:
        return dispatch_touch(id, mir_input_event_get_touch_event(iev));
    case mir_input_event_type_pointer:
        return dispatch_pointer(id, mir_input_event_get_pointer_event(iev));
    default:
        BOOST_THROW_EXCEPTION(std::logic_error("InputDispatcher got an input event of unknown type"));
    }
    
    return true;
}

void mi::DefaultInputDispatcher::start()
{
    // TODO: Trigger hover here?
}

void mi::DefaultInputDispatcher::stop()
{
}

void mi::DefaultInputDispatcher::set_focus(std::shared_ptr<input::Surface> const& target)
{
    std::lock_guard<std::mutex> lg(dispatcher_mutex);

    // TODO: Clear from observer too
    focus_surface_key_input_state.clear();
    focus_surface = target;
}

bool mi::DefaultInputDispatcher::KeyInputState::handle_event(MirInputDeviceId id, MirKeyboardEvent const* kev)
{
    auto action = mir_keyboard_event_action(kev);
    if (action == mir_keyboard_action_up)
    {
        return release_key(id, mir_keyboard_event_scan_code(kev));
    }
    else if (action == mir_keyboard_action_down)
    {
        return press_key(id, mir_keyboard_event_scan_code(kev));
    }
    else if (action == mir_keyboard_action_repeat)
    {
        // TODO: Handle repeat case
        return false;
    }
    return false;
}

bool mi::DefaultInputDispatcher::KeyInputState::press_key(MirInputDeviceId id, int scan_code)
{
    // TODO: How do we handle the device dissapearing?

    // First key press for a device
    if (depressed_scancodes.find(id) == depressed_scancodes.end())
    {
        depressed_scancodes[id] = {};
    }

    auto& device_key_state = depressed_scancodes[id];
    if (device_key_state.find(scan_code) != device_key_state.end())
        return false;
    device_key_state.insert(scan_code);
    return true;
}

bool mi::DefaultInputDispatcher::KeyInputState::release_key(MirInputDeviceId id, int scan_code)
{
    if (depressed_scancodes.find(id) == depressed_scancodes.end())
    {
        return false;
    }

    auto& device_key_state = depressed_scancodes[id];
    if (device_key_state.find(scan_code) == device_key_state.end())
        return false;
    device_key_state.erase(scan_code);
    return true;
}

void mi::DefaultInputDispatcher::KeyInputState::clear()
{
    depressed_scancodes.clear();
}
