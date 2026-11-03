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

#include "score/serializer/codec.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "score/serializer/test_codecs_types.h"

namespace {

using score::someip_gateway::serializer::Codec;
using score::someip_gateway::serializer::GetCodecs;
namespace types = score::someip_gateway::serializer::test;

const Codec& FindCodec(std::string_view name) {
    for (const auto& codec : GetCodecs()) {
        if (codec.type_name == name) {
            return codec;
        }
    }
    static const Codec kNone{};
    return kNone;
}

types::Message MakeMessage() {
    types::Message message{};
    message.id = 0x01020304U;
    message.delta = -1;
    message.mode = types::Mode::kA;
    message.fixed = {0x1122U, 0x3344U};
    message.bytes.size = 3U;
    message.bytes.data = {9U, 8U, 7U, 0U};
    message.label.size = 2U;
    message.label.data[0] = 'h';
    message.label.data[1] = 'i';
    message.inners.size = 1U;
    message.inners.data[0] = {true, 1.0F};
    message.value = 2.0;
    return message;
}

// Big endian, PRS_SOMEIPProtocol layout.
const std::vector<std::uint8_t> kMessageBytes{
    0x01, 0x02, 0x03, 0x04,                                     // id
    0xFF,                                                       // delta
    0xFF, 0xFE,                                                 // mode
    0x11, 0x22, 0x33, 0x44,                                     // fixed
    0x03, 0x09, 0x08, 0x07,                                     // bytes: 8-bit length + content
    0x00, 0x00, 0x00, 0x06, 0xEF, 0xBB, 0xBF, 'h',  'i', 0x00,  // label: length, BOM, text, NUL
    0x00, 0x00, 0x00, 0x07,                                     // inners: 32-bit length
    0x00, 0x05, 0x01, 0x3F, 0x80, 0x00, 0x00,        // Inner: 16-bit length, flag, temperature
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // value
};

class Codec_test : public ::testing::Test {
   protected:
    const Codec& message_codec_{FindCodec("score::someip_gateway::serializer::test::Message")};
    const Codec& inner_codec_{FindCodec("score::someip_gateway::serializer::test::Inner")};
};

TEST_F(Codec_test, every_named_type_has_a_codec) {
    EXPECT_EQ(GetCodecs().size(), 3U);
    EXPECT_NE(FindCodec("score::someip_gateway::serializer::test::Mode").serialize, nullptr);
    ASSERT_NE(message_codec_.serialize, nullptr);
    EXPECT_EQ(message_codec_.size_of, sizeof(types::Message));
    EXPECT_EQ(message_codec_.align_of, alignof(types::Message));
}

TEST_F(Codec_test, max_serialized_size_covers_all_bounds) {
    // 4 + 1 + 2 + 4 + (1 + 4) + (4 + 3 + 8 + 1) + (4 + 2 * 7) + 8
    EXPECT_EQ(message_codec_.max_serialized_size, 58U);
}

TEST_F(Codec_test, serialize_produces_someip_layout) {
    const auto message = MakeMessage();
    std::vector<std::uint8_t> buffer(message_codec_.max_serialized_size);
    std::size_t written = 0;
    ASSERT_TRUE(message_codec_.serialize(&message, buffer.data(), buffer.size(), written));
    buffer.resize(written);
    EXPECT_EQ(buffer, kMessageBytes);
}

TEST_F(Codec_test, deserialize_restores_object) {
    types::Message message{};
    ASSERT_TRUE(message_codec_.deserialize(kMessageBytes.data(), kMessageBytes.size(), &message));
    EXPECT_EQ(message, MakeMessage());
}

TEST_F(Codec_test, serialize_fails_on_short_buffer) {
    const auto message = MakeMessage();
    std::vector<std::uint8_t> buffer(kMessageBytes.size() - 1U);
    std::size_t written = 0;
    EXPECT_FALSE(message_codec_.serialize(&message, buffer.data(), buffer.size(), written));
}

TEST_F(Codec_test, serialize_fails_when_bound_exceeded) {
    auto message = MakeMessage();
    message.bytes.size = 5U;
    std::vector<std::uint8_t> buffer(message_codec_.max_serialized_size);
    std::size_t written = 0;
    EXPECT_FALSE(message_codec_.serialize(&message, buffer.data(), buffer.size(), written));
}

TEST_F(Codec_test, deserialize_fails_on_truncated_payload) {
    types::Message message{};
    EXPECT_FALSE(
        message_codec_.deserialize(kMessageBytes.data(), kMessageBytes.size() - 1U, &message));
}

TEST_F(Codec_test, deserialize_fails_when_bound_exceeded) {
    auto bytes = kMessageBytes;
    bytes[11] = 0x05;  // five elements in an array bounded to four
    bytes.insert(bytes.begin() + 15, 0x06);
    types::Message message{};
    EXPECT_FALSE(message_codec_.deserialize(bytes.data(), bytes.size(), &message));
}

TEST_F(Codec_test, deserialize_rejects_invalid_bool) {
    const std::uint8_t bytes[] = {0x00, 0x05, 0x02, 0x3F, 0x80, 0x00, 0x00};
    types::Inner inner{};
    EXPECT_FALSE(inner_codec_.deserialize(bytes, sizeof(bytes), &inner));
}

TEST_F(Codec_test, serialize_rejects_invalid_bool) {
    types::Inner inner{};
    const std::uint8_t invalid = 2U;
    std::memcpy(&inner.flag, &invalid, sizeof(invalid));
    std::uint8_t buffer[16]{};
    std::size_t written = 0;
    EXPECT_FALSE(inner_codec_.serialize(&inner, buffer, sizeof(buffer), written));
}

TEST_F(Codec_test, deserialize_skips_unknown_trailing_struct_members) {
    const std::uint8_t bytes[] = {0x00, 0x06, 0x01, 0x3F, 0x80, 0x00, 0x00, 0xAA};
    types::Inner inner{};
    ASSERT_TRUE(inner_codec_.deserialize(bytes, sizeof(bytes), &inner));
    EXPECT_TRUE(inner.flag);
    EXPECT_EQ(inner.temperature, 1.0F);
}

TEST_F(Codec_test, deserialize_accepts_string_without_bom) {
    auto bytes = kMessageBytes;
    bytes.erase(bytes.begin() + 19, bytes.begin() + 22);
    bytes[18] = 0x03;
    types::Message message{};
    ASSERT_TRUE(message_codec_.deserialize(bytes.data(), bytes.size(), &message));
    EXPECT_EQ(message.label.size, 2U);
    EXPECT_EQ(std::string_view(message.label.data.data(), message.label.size), "hi");
}

}  // namespace
