/*
 * Copyright © 2018-2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
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
 */

#ifndef MIRAL_WAYLAND_EXTENSIONS_H
#define MIRAL_WAYLAND_EXTENSIONS_H

#include "application.h"

#include <functional>
#include <memory>
#include <string>
#include <set>

struct wl_display;
struct wl_client;
struct wl_resource;

namespace mir { class Server; }

namespace miral
{
class Window;

/// Enable configuration of the Wayland extensions enabled at runtime.
///
/// This adds the command line option '--wayland-extensions' the corresponding
/// MIR_SERVER_WAYLAND_EXTENSIONS environment variable, and the wayland-extensions
/// config line.
///   * The server can add support for additional extensions
///   * The server can specify the configuration defaults
///   * Mir's option handling allows the defaults to be overridden
/// \remark Since MirAL 2.4
class WaylandExtensions
{
public:
    /// Default to enabling the extensions recommended by Mir
    WaylandExtensions();

    /// Initialize "enabled by default" to a custom set of extensions (colon
    /// separated list).
    /// \note This can only be a subset of supported_extensions()
    /// \deprecated A better option is to use the default constructor, enable()
    /// and disable(). You can call disable() on all recommended() extensions
    /// if you want complete control over which are enabled
    explicit WaylandExtensions(std::string const& default_value);

    void operator()(mir::Server& server) const;

    /// All Wayland extensions currently supported (colon separated list).
    /// This includes both the recommended_extensions() and any extensions that
    /// have been added using add_extension().
    /// \deprecated This is of no real use to the server, just for documenting
    /// the configuration option.
    auto supported_extensions() const -> std::string;

    /// Default for extensions to enabled recommended by Mir (colon separated list)
    /// \remark Since MirAL 2.5
    /// \deprecated Instead of overridding the whole extension list in the constructor and using this to get the
    /// recommended ones, you can now just enable() the extensions you want.
    static auto recommended_extensions() -> std::string;

    ~WaylandExtensions();
    WaylandExtensions(WaylandExtensions const&);
    auto operator=(WaylandExtensions const&) -> WaylandExtensions&;

    /// Context information useful for implementing Wayland extensions
    /// \remark Since MirAL 2.5
    class Context
    {
    public:
        virtual auto display() const -> wl_display* = 0;
        virtual void run_on_wayland_mainloop(std::function<void()>&& work) const = 0;

    protected:
        Context() = default;
        virtual ~Context() = default;
        Context(Context const&) = delete;
        Context& operator=(Context const&) = delete;
    };

    /// A Builder creates and registers an extension protocol.
    /// \remark Since MirAL 2.5
    struct Builder
    {
        /// Name of the protocol extension
        std::string name;

        /// Functor that creates and registers an extension protocol
        /// \param context giving access to:
        ///   * the wl_display (so that, for example, the extension can be registered); and,
        ///   * allowing server initiated code to be executed on the Wayland mainloop.
        /// \return a shared pointer to the implementation. (Mir will manage the lifetime)
        std::function<std::shared_ptr<void>(Context const* context)> build;
    };

    /// \remark Since MirAL 2.5
    using Filter = std::function<bool(Application const& app, char const* protocol)>;

    /// Set an extension filter callback to control the extensions available to specific clients
    /// \remark Since MirAL 2.5
    void set_filter(Filter const& extension_filter);

    /**
     * Supported wayland extensions that are not enabled by default.
     * These can be passed into WaylandExtensions::enable() to turn them on.
     * @{ */

    /// Enables shell components such as panels, notifications and lock screens.
    /// It is recommended to use this in conjunction with set_filter() as malicious
    /// clients could potentially use this protocol to steal input focus or
    /// otherwise bother the user.
    /// \remark Since MirAL 2.6
    static char const* const zwlr_layer_shell_v1;

    /// Allows clients to retrieve additional information about outputs
    /// \remark Since MirAL 2.6
    static char const* const zxdg_output_manager_v1;
    /** @} */

    /// Add a bespoke Wayland extension both to "supported" and "enabled by default".
    /// \remark Since MirAL 2.5
    void add_extension(Builder const& builder);

    /// Add a bespoke Wayland extension both to "supported" but not "enabled by default".
    /// \remark Since MirAL 2.5
    void add_extension_disabled_by_default(Builder const& builder);

    /// The set of Wayland extensions that Mir recommends.
    /// Also the set that is enabled by default upon construction of a WaylandExtensions object.
    /// \remark Since MirAL 2.6
    static auto recommended() -> std::set<std::string>;

    /// The set of Wayland extensions that core Mir supports.
    /// Does not include bespoke extensions
    /// A superset of recommended()
    /// \remark Since MirAL 2.6
    static auto supported() -> std::set<std::string>;

    /// Enable a Wayland extension
    /// Throws a std::runtime_error if the extension is not supported
    /// \remark Since MirAL 2.6
    auto enable(std::string name) -> WaylandExtensions&;

    /// Disable a Wayand extension
    /// Throws a std::runtime_error if the extension is not supported
    /// \remark Since MirAL 2.6
    auto disable(std::string name) -> WaylandExtensions&;

private:
    struct Self;
    std::shared_ptr<Self> self;
};

/// Get the MirAL application for a wl_client.
/// \return The application (null if no application is found)
/// \remark Since MirAL 2.5
auto application_for(wl_client* client) -> Application;

/// Get the MirAL application for a wl_resource.
/// \return The application (null if no application is found)
/// \remark Since MirAL 2.5
auto application_for(wl_resource* resource) -> Application;

/// Get the MirAL Window for a Wayland Surface, XdgSurface, etc.
/// Note that there may not be a corresponding miral::Window (e.g. the
/// surface is created and assigned properties before 'commit' creates the
/// miral::Window).
///
/// \return The window (null if no window is found)
/// \remark Since MirAL 2.5
auto window_for(wl_resource* surface) -> Window;
}

#endif //MIRAL_WAYLAND_EXTENSIONS_H
