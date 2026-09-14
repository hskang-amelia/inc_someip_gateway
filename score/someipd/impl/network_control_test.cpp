/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "network_control.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nm_control_protocol.h"

namespace score::someipd {
namespace {

using namespace std::chrono_literals;

/// Long enough that a loaded machine does not fail these, short enough that a
/// real failure does not hold the suite up.
constexpr auto kPatience{5s};

std::string TestSocketPath(const char* tag) {
    static std::atomic<unsigned> next{0U};
    return std::string{"/tmp/someipd-nm-test-"} + tag + "-" + std::to_string(::getpid()) + "-" +
           std::to_string(next.fetch_add(1U)) + ".sock";
}

std::array<std::uint8_t, kNmRecordSize> MakeRecord(std::uint8_t version, std::uint8_t com_state,
                                                   std::uint8_t nm_state, const std::string& name) {
    std::array<std::uint8_t, kNmRecordSize> record{};
    record[0] = version;
    record[1] = com_state;
    record[2] = nm_state;
    record[3] = static_cast<std::uint8_t>(name.size());
    std::memcpy(&record[4], name.data(), name.size());
    return record;
}

int ConnectTo(const std::string& path) {
    const int fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (fd < 0) {
        return -1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/// Retries until the condition holds or patience runs out.
template <typename Predicate>
bool Within(Predicate condition) {
    const auto deadline{std::chrono::steady_clock::now() + kPatience};
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

/// Collects what the listener hands over.
class Sink {
   public:
    NetworkControl::Handler Handler() {
        return [this](const NmComStateRecord& record) {
            const std::lock_guard<std::mutex> guard{lock_};
            records_.push_back(record);
        };
    }

    std::size_t Count() const {
        const std::lock_guard<std::mutex> guard{lock_};
        return records_.size();
    }

    NmComStateRecord At(std::size_t index) const {
        const std::lock_guard<std::mutex> guard{lock_};
        return index < records_.size() ? records_[index] : NmComStateRecord{};
    }

   private:
    mutable std::mutex lock_{};
    std::vector<NmComStateRecord> records_{};
};

TEST(NetworkControl, DecodesAWholeRecord) {
    const std::string path{TestSocketPath("whole")};
    Sink sink{};
    auto control = NetworkControl::Create(path, sink.Handler());
    ASSERT_NE(control, nullptr);

    const int client{ConnectTo(path)};
    ASSERT_GE(client, 0);

    const auto record{MakeRecord(kNmProtocolVersion, 1U, 3U, "EthernetCluster")};
    ASSERT_EQ(::write(client, record.data(), record.size()), static_cast<ssize_t>(record.size()));

    EXPECT_TRUE(Within([&] { return sink.Count() == 1U; }));
    EXPECT_EQ(sink.At(0).cluster, "EthernetCluster");
    EXPECT_EQ(sink.At(0).com_state, ComState::kFullCom);
    EXPECT_EQ(sink.At(0).nm_state, NmState::kNormalOperation);

    ::close(client);
}

TEST(NetworkControl, ReassemblesARecordSplitAcrossWrites) {
    // A stream socket is entitled to split a record. Discarding the first part
    // would desynchronise every record after it, so the reader carries it over.
    const std::string path{TestSocketPath("split")};
    Sink sink{};
    auto control = NetworkControl::Create(path, sink.Handler());
    ASSERT_NE(control, nullptr);

    const int client{ConnectTo(path)};
    ASSERT_GE(client, 0);

    const auto record{MakeRecord(kNmProtocolVersion, 0U, 1U, "DiagnosticsCluster")};
    constexpr std::size_t kFirstChunk{10U};
    ASSERT_EQ(::write(client, record.data(), kFirstChunk), static_cast<ssize_t>(kFirstChunk));
    std::this_thread::sleep_for(50ms);
    ASSERT_EQ(::write(client, record.data() + kFirstChunk, record.size() - kFirstChunk),
              static_cast<ssize_t>(record.size() - kFirstChunk));

    EXPECT_TRUE(Within([&] { return sink.Count() == 1U; }));
    EXPECT_EQ(sink.At(0).cluster, "DiagnosticsCluster");
    EXPECT_EQ(sink.At(0).com_state, ComState::kNoCom);
    // Prepare-Bus-Sleep is not Bus-Sleep: the cluster is on its way down and can
    // still be pulled back up without a wake-up.
    EXPECT_EQ(sink.At(0).nm_state, NmState::kPrepareBusSleep);
    EXPECT_GT(control->GetObservations().partial_reads, 0U);

    ::close(client);
}

TEST(NetworkControl, RefusesARecordItCannotTrust) {
    // Each of these would otherwise become a guess about whether a bus may
    // sleep, so none of them reaches the handler.
    const std::string path{TestSocketPath("refuse")};
    Sink sink{};
    auto control = NetworkControl::Create(path, sink.Handler());
    ASSERT_NE(control, nullptr);

    const int client{ConnectTo(path)};
    ASSERT_GE(client, 0);

    const auto wrong_version{MakeRecord(kNmProtocolVersion + 1U, 1U, 3U, "EthernetCluster")};
    ASSERT_EQ(::write(client, wrong_version.data(), wrong_version.size()),
              static_cast<ssize_t>(wrong_version.size()));

    const auto unknown_state{MakeRecord(kNmProtocolVersion, 1U, 77U, "EthernetCluster")};
    ASSERT_EQ(::write(client, unknown_state.data(), unknown_state.size()),
              static_cast<ssize_t>(unknown_state.size()));

    auto impossible_name{MakeRecord(kNmProtocolVersion, 1U, 3U, "EthernetCluster")};
    impossible_name[3] = static_cast<std::uint8_t>(kNmMaxClusterName + 1U);
    ASSERT_EQ(::write(client, impossible_name.data(), impossible_name.size()),
              static_cast<ssize_t>(impossible_name.size()));

    EXPECT_TRUE(Within([&] { return control->GetObservations().malformed == 3U; }));
    EXPECT_EQ(sink.Count(), 0U);

    ::close(client);
}

TEST(NetworkControl, AcceptsNetworkManagementComingBack) {
    // Network Management restarting must not cost this daemon its control
    // channel — and what NM last said stands until it says otherwise.
    const std::string path{TestSocketPath("reconnect")};
    Sink sink{};
    auto control = NetworkControl::Create(path, sink.Handler());
    ASSERT_NE(control, nullptr);

    const int first{ConnectTo(path)};
    ASSERT_GE(first, 0);
    EXPECT_TRUE(Within([&] { return control->GetObservations().connected; }));
    ::close(first);
    EXPECT_TRUE(Within([&] { return !control->GetObservations().connected; }));

    const int second{ConnectTo(path)};
    ASSERT_GE(second, 0);
    EXPECT_TRUE(Within([&] { return control->GetObservations().connections == 2U; }));

    const auto record{MakeRecord(kNmProtocolVersion, 0U, 0U, "EthernetCluster")};
    ASSERT_EQ(::write(second, record.data(), record.size()), static_cast<ssize_t>(record.size()));
    EXPECT_TRUE(Within([&] { return sink.Count() == 1U; }));
    EXPECT_EQ(sink.At(0).com_state, ComState::kNoCom);

    ::close(second);
}

TEST(NetworkControl, StopsPromptlyAndMoreThanOnce) {
    const std::string path{TestSocketPath("stop")};
    auto control = NetworkControl::Create(path, {});
    ASSERT_NE(control, nullptr);

    const auto started{std::chrono::steady_clock::now()};
    control->Stop();
    control->Stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);

    // A socket file left behind would make the next start of this daemon fail
    // to bind.
    EXPECT_NE(::access(path.c_str(), F_OK), 0);
}

TEST(NetworkControl, RefusesASocketPathThatCannotBeBound) {
    // Truncating it would bind a different socket than the one named, which
    // Network Management would then never find.
    EXPECT_EQ(NetworkControl::Create(std::string(200U, 'x'), {}), nullptr);
    EXPECT_EQ(NetworkControl::Create("", {}), nullptr);
}

TEST(NmControlProtocol, RefusesALengthThatIsNotARecord) {
    const auto record{MakeRecord(kNmProtocolVersion, 1U, 3U, "EthernetCluster")};
    NmComStateRecord decoded{};

    EXPECT_FALSE(DecodeNmComStateRecord(record.data(), record.size() - 1U, decoded));
    EXPECT_FALSE(DecodeNmComStateRecord(record.data(), record.size() + 1U, decoded));
    EXPECT_FALSE(DecodeNmComStateRecord(nullptr, record.size(), decoded));
    EXPECT_TRUE(DecodeNmComStateRecord(record.data(), record.size(), decoded));
}

TEST(NmControlProtocol, CarriesTheAbsenceOfAnNmState) {
    // Network Management reporting only the two-value answer is a legitimate
    // configuration on its side, not a degraded one.
    const auto record{MakeRecord(kNmProtocolVersion, 1U, kNmStateUnknown, "EthernetCluster")};
    NmComStateRecord decoded{};

    ASSERT_TRUE(DecodeNmComStateRecord(record.data(), record.size(), decoded));
    EXPECT_EQ(decoded.nm_state, NmState::kUnknown);
    EXPECT_EQ(decoded.com_state, ComState::kFullCom);
}

TEST(NmControlProtocol, RefusesARecordNamingNoCluster) {
    const auto record{MakeRecord(kNmProtocolVersion, 1U, 3U, "")};
    NmComStateRecord decoded{};

    EXPECT_FALSE(DecodeNmComStateRecord(record.data(), record.size(), decoded));
}

TEST(NmControlProtocol, ReadsANameThatExactlyFillsTheRecord) {
    const std::string exact(kNmMaxClusterName, 'C');
    const auto record{MakeRecord(kNmProtocolVersion, 1U, 3U, exact)};
    NmComStateRecord decoded{};

    ASSERT_TRUE(DecodeNmComStateRecord(record.data(), record.size(), decoded));
    EXPECT_EQ(decoded.cluster, exact);
}

}  // namespace
}  // namespace score::someipd
