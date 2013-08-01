/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "src/server/frontend/message_sender.h"
#include "src/server/frontend/event_sender.h"
#include "mir_test_doubles/stub_display_configuration.h"
#include "mir_test/fake_shared.h"

#include <vector>
#include "mir_protobuf.pb.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace mt=mir::test;
namespace mtd=mir::test::doubles;
namespace mfd=mir::frontend::detail;
namespace geom=mir::geometry;

namespace 
{
struct MockMsgSender : public mfd::MessageSender
{
    MOCK_METHOD1(send, void(std::string const&));
    MOCK_METHOD1(send_fds, void(std::vector<int32_t> const&));
};
}

TEST(TestEventSender, display_send)
{
    using namespace testing;

    mtd::StubDisplayConfig config;
    MockMsgSender mock_msg_sender;

    auto msg_validator = [](std::string const& ){
/*
        mp::EventSequence seq;
        seq.grabit(str)

        EXPECT_THAT(seq, MATCHES(config))
*/
    };

    EXPECT_CALL(mock_msg_sender, send(_))
        .Times(1)
        .WillOnce(Invoke(msg_validator));

    mfd::EventSender sender(mt::fake_shared(mock_msg_sender));

    sender.send_display_config(config);
}
