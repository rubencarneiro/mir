/*
 * Copyright © 2014-2017 Canonical Ltd.
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
 * Authored By: Alan Griffiths <alan@octopull.co.uk>
 */

#include "server_example_test_client.h"
#include "mir/fd.h"
#include "mir/server.h"
#include "mir/main_loop.h"
#include "mir/logging/logger.h"
#include "mir/log.h"
#include "mir/options/option.h"

#include <chrono>
#include <future>

#include <csignal>
#include <sys/wait.h>

namespace me = mir::examples;
namespace ml = mir::logging;

namespace
{
static auto const component = "server_example_test_client.cpp";

bool exit_success(pid_t pid)
{
    int status;

    auto const wait_rc = waitpid(pid, &status, WNOHANG);

    if (wait_rc == 0)
    {
        ml::log(ml::Severity::informational, "Terminating client", component);
        kill(pid, SIGKILL);
        return false;
    }
    else if (wait_rc != pid)
    {
        ml::log(ml::Severity::informational, "No status available for client", component);
        return false;
    }
    else if (WIFEXITED(status))
    {
        auto const exit_status = WEXITSTATUS(status);
        if (exit_status == EXIT_SUCCESS)
        {
            ml::log(ml::Severity::informational, "Client exited successfully", component);
            return true;
        }
        else
        {
            char const format[] = "Client has exited with status %d";
            char buffer[sizeof format + 10];
            snprintf(buffer, sizeof buffer, format, exit_status);
            ml::log(ml::Severity::informational, buffer, component);
            return false;
        }
    }
    else if (WIFSIGNALED(status))
    {
        auto const signal = WTERMSIG(status);
        char const format[] = "Client terminated by signal %d";
        char buffer[sizeof format];
        snprintf(buffer, sizeof buffer, format, signal);
        ml::log(ml::Severity::informational, buffer, component);
        return false;
    }
    else
    {
        ml::log(ml::Severity::informational, "Client died mysteriously", component);
        return false;
    }
}
}

struct me::TestClientRunner::Self
{
    std::unique_ptr<time::Alarm> client_kill_action;
    std::unique_ptr<time::Alarm> server_stop_action;
    std::atomic<bool> test_failed;
};

me::TestClientRunner::TestClientRunner() : self{std::make_shared<Self>()} {}

void me::TestClientRunner::operator()(mir::Server& server)
{
    static const char* const test_client_opt = "test-client";
    static const char* const test_client_descr = "client executable";

    static const char* const test_timeout_opt = "test-timeout";
    static const char* const test_timeout_descr = "Seconds to run before sending SIGTERM to client";

    server.add_configuration_option(test_client_opt, test_client_descr, OptionType::string);
    server.add_configuration_option(test_timeout_opt, test_timeout_descr, 10);

    server.add_init_callback([&server, self = self]
    {
        const auto options1 = server.get_options();

        if (options1->is_set(test_client_opt))
        {
            self->test_failed = true;

            auto const pid = fork();

            if (pid == 0)
            {
                // Enable tests with toolkits supporting mirclient
                if (auto mir_socket_name = server.mir_socket_name())
                {
                    auto const mir_socket = mir_socket_name.value();
                    setenv("MIR_SOCKET", mir_socket.c_str(), 1);
                }

                // Enable tests with toolkits supporting Wayland
                auto const wayland_display = server.wayland_display();
                setenv("WAYLAND_DISPLAY", wayland_display.value().c_str(),  true);   // configure Wayland socket
                setenv("GDK_BACKEND", "wayland", true);             // configure GTK to use Wayland
                setenv("QT_QPA_PLATFORM", "wayland", true);         // configure Qt to use Wayland
                unsetenv("QT_QPA_PLATFORMTHEME");                   // Discourage Qt from unsupported theme
                setenv("SDL_VIDEODRIVER", "wayland", true);         // configure SDL to use Wayland

                auto const client = options1->get<std::string>(test_client_opt);
                log(logging::Severity::informational, "mir::examples", "Starting test client: %s", client.c_str());
                execlp(client.c_str(), client.c_str(), static_cast<char const*>(nullptr));
                // If execl() returns then something is badly wrong
                log(logging::Severity::critical, "mir::examples",
                    "Failed to execute client (%s) error: %s", client.c_str(), strerror(errno));
                abort();
            }
            else if (pid > 0)
            {
                self->client_kill_action = server.the_main_loop()->create_alarm(
                    [pid]
                    {
                        kill(pid, SIGTERM);
                    });

                self->server_stop_action = server.the_main_loop()->create_alarm(
                    [pid, &server, self]()
                    {
                        self->test_failed = !exit_success(pid);
                        server.stop();
                    });

                self->client_kill_action->reschedule_in(std::chrono::seconds(options1->get<int>(test_timeout_opt)));
                self->server_stop_action->reschedule_in(std::chrono::seconds(options1->get<int>(test_timeout_opt) + 1));
            }
            else
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("Client failed to launch"));
            }
        }
        else
        {
            self->test_failed = false;
        }
    });
}

bool me::TestClientRunner::test_failed() const { return self->test_failed; }
