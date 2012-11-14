/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "mir_client/client_platform.h"
#include "mir_client/mir_client_surface.h"
#include "mir_client/client_connection.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace mcl = mir::client;

struct MockClientConnection : public mcl::ClientConnection
{
    MockClientConnection()
    {
        using namespace testing;
        EXPECT_CALL(*this, populate(_)).Times(AtLeast(0));
    }

    MOCK_METHOD1(populate, void(MirPlatformPackage&));
};

struct MockClientSurface : public mcl::ClientSurface
{
    MOCK_CONST_METHOD0(get_parameters, MirSurfaceParameters());
    MOCK_METHOD0(get_current_buffer, std::shared_ptr<mcl::ClientBuffer>());
    MOCK_METHOD2(next_buffer, MirWaitHandle*(mir_surface_lifecycle_callback, void*));
};

TEST(GBMClientPlatformTest, egl_native_window_is_client_surface)
{
    MockClientConnection connection;
    MockClientSurface surface;
    auto platform = mcl::create_client_platform(&connection);
    auto native_window = platform->create_egl_window(&surface);
    EXPECT_EQ(reinterpret_cast<EGLNativeWindowType>(&surface), native_window);
}

TEST(GBMClientPlatformTest, egl_native_display_is_client_connection)
{
    MockClientConnection connection;
    auto platform = mcl::create_client_platform(&connection);
    auto native_display = platform->create_egl_native_display();
    EGLNativeDisplayType egl_native_display = native_display->get_egl_native_display();
    EXPECT_EQ(reinterpret_cast<EGLNativeDisplayType>(&connection), egl_native_display);
}
