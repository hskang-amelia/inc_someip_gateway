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

#include "score/serializer/typed/someip_wire.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace score::someip_gateway::serializer::typed {
namespace {

TEST(SomeipWireTest, WritesIntegersBigEndian) {
    std::array<std::uint8_t, 15> buffer{};
    Writer w{buffer.data(), buffer.size()};
    ASSERT_TRUE(w.Put(std::uint16_t{0x1234}));
    ASSERT_TRUE(w.Put(std::int32_t{-2}));
    ASSERT_TRUE(w.Put(std::uint64_t{0x0102030405060708}));
    ASSERT_TRUE(w.Put(true));
    EXPECT_EQ(w.Position(), 15U);
    const std::array<std::uint8_t, 15> expected{0x12, 0x34, 0xFF, 0xFF, 0xFF, 0xFE, 0x01, 0x02,
                                                0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01};
    EXPECT_EQ(buffer, expected);
}

TEST(SomeipWireTest, WritesFloatsAsIeee754BigEndian) {
    std::array<std::uint8_t, 12> buffer{};
    Writer w{buffer.data(), buffer.size()};
    ASSERT_TRUE(w.Put(1.0F));
    ASSERT_TRUE(w.Put(-2.0));
    const std::array<std::uint8_t, 12> expected{0x3F, 0x80, 0x00, 0x00, 0xC0, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

TEST(SomeipWireTest, RoundTripsAllPrimitives) {
    std::array<std::uint8_t, 64> buffer{};
    Writer w{buffer.data(), buffer.size()};
    ASSERT_TRUE(w.Put(std::int8_t{-5}));
    ASSERT_TRUE(w.Put(std::int16_t{-300}));
    ASSERT_TRUE(w.Put(std::int64_t{-1234567890123}));
    ASSERT_TRUE(w.Put(3.5F));
    ASSERT_TRUE(w.Put(-0.125));
    ASSERT_TRUE(w.Put(false));

    Reader r{buffer.data(), w.Position()};
    std::int8_t i8{};
    std::int16_t i16{};
    std::int64_t i64{};
    float f32{};
    double f64{};
    bool flag{true};
    ASSERT_TRUE(r.Get(i8));
    ASSERT_TRUE(r.Get(i16));
    ASSERT_TRUE(r.Get(i64));
    ASSERT_TRUE(r.Get(f32));
    ASSERT_TRUE(r.Get(f64));
    ASSERT_TRUE(r.Get(flag));
    EXPECT_EQ(i8, -5);
    EXPECT_EQ(i16, -300);
    EXPECT_EQ(i64, -1234567890123);
    EXPECT_EQ(f32, 3.5F);
    EXPECT_EQ(f64, -0.125);
    EXPECT_FALSE(flag);
    EXPECT_EQ(r.Position(), w.Position());
}

TEST(SomeipWireTest, WriterRejectsOverflowWithoutPartialWrite) {
    std::array<std::uint8_t, 3> buffer{0xAA, 0xAA, 0xAA};
    Writer w{buffer.data(), buffer.size()};
    EXPECT_FALSE(w.Put(std::uint32_t{1}));
    EXPECT_EQ(w.Position(), 0U);
    EXPECT_EQ(buffer[0], 0xAA);
}

TEST(SomeipWireTest, ReaderRejectsUnderrun) {
    const std::array<std::uint8_t, 1> buffer{0x01};
    Reader r{buffer.data(), buffer.size()};
    std::uint16_t value{};
    EXPECT_FALSE(r.Get(value));
    EXPECT_EQ(r.Position(), 0U);
}

TEST(SomeipWireTest, ReaderRejectsInvalidBool) {
    const std::array<std::uint8_t, 1> buffer{0x02};
    Reader r{buffer.data(), buffer.size()};
    bool value{};
    EXPECT_FALSE(r.Get(value));
}

}  // namespace
}  // namespace score::someip_gateway::serializer::typed
