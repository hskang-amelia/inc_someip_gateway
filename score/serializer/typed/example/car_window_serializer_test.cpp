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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "score/serializer/serializer.h"
#include "score/serializer/typed/example/car_window_types.h"

namespace {

const score_com_serializer* GetSerializer(std::string_view service,
                                          score_com_serializer_element_type type,
                                          std::string_view element) {
    const score_com_serializer* serializer = nullptr;
    EXPECT_EQ(score_com_serializer_get(service.data(), service.size(), type, element.data(),
                                       element.size(), &serializer),
              score_com_serializer_result_ok);
    return serializer;
}

const score_com_serializer* WindowCommandSerializer() {
    return GetSerializer("car_window", score_com_serializer_element_type_event, "window_command");
}

const score_com_serializer* WindowStateSerializer() {
    return GetSerializer("car_window", score_com_serializer_element_type_event, "window_state");
}

car_window::WindowState MakeState() {
    car_window::WindowState state{};
    state.timestamp_ms = 0x0000018F2A3B4C5D;
    for (std::uint8_t i = 0; i < state.windows.size(); ++i) {
        state.windows[i].window_id = i;
        state.windows[i].position_percent = 25.0F * static_cast<float>(i);
        state.windows[i].is_moving = (i % 2U) == 1U;
    }
    state.child_lock = true;
    return state;
}

TEST(CarWindowSerializerTest, InitAndDeinitSucceed) {
    EXPECT_EQ(score_com_serializer_init("unused", 6), score_com_serializer_result_ok);
    EXPECT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
}

TEST(CarWindowSerializerTest, ReportsTypeInfoAndWireSize) {
    const auto* serializer = WindowStateSerializer();
    ASSERT_NE(serializer, nullptr);
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer),
              car_window::kWireSizeWindowState);
    EXPECT_EQ(car_window::kWireSizeWindowState, 33U);
    EXPECT_EQ(score_com_serializer_get_sizeof_type(serializer), sizeof(car_window::WindowState));
    EXPECT_EQ(score_com_serializer_get_alignof_type(serializer), alignof(car_window::WindowState));
}

TEST(CarWindowSerializerTest, SerializesExactBigEndianLayout) {
    const auto* serializer = WindowCommandSerializer();
    const car_window::WindowCommand command{7, -300};
    std::array<std::uint8_t, 8> buffer{};
    std::size_t written = 0;
    ASSERT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &command,
                                             &written),
              score_com_serializer_result_ok);
    ASSERT_EQ(written, 3U);
    EXPECT_EQ(buffer[0], 0x07);
    EXPECT_EQ(buffer[1], 0xFE);
    EXPECT_EQ(buffer[2], 0xD4);
}

TEST(CarWindowSerializerTest, NestedArrayLayout) {
    const auto* serializer = WindowStateSerializer();
    const auto state = MakeState();
    std::vector<std::uint8_t> buffer(car_window::kWireSizeWindowState);
    std::size_t written = 0;
    ASSERT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &state,
                                             &written),
              score_com_serializer_result_ok);
    ASSERT_EQ(written, 33U);
    const std::vector<std::uint8_t> head{0x00, 0x00, 0x01, 0x8F, 0x2A, 0x3B, 0x4C, 0x5D};
    EXPECT_TRUE(std::equal(head.begin(), head.end(), buffer.begin()));
    // windows[1]: id 1, 25.0f = 0x41C80000, moving
    const std::vector<std::uint8_t> window1{0x01, 0x41, 0xC8, 0x00, 0x00, 0x01};
    EXPECT_TRUE(std::equal(window1.begin(), window1.end(), buffer.begin() + 8 + 6));
    EXPECT_EQ(buffer.back(), 0x01);
}

TEST(CarWindowSerializerTest, RoundTrip) {
    const auto* serializer = WindowStateSerializer();
    const auto state = MakeState();
    std::vector<std::uint8_t> buffer(car_window::kWireSizeWindowState);
    std::size_t written = 0;
    ASSERT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &state,
                                             &written),
              score_com_serializer_result_ok);

    alignas(car_window::WindowState) std::array<std::uint8_t, sizeof(car_window::WindowState)>
        storage{};
    ASSERT_EQ(score_com_serializer_deserialize(serializer, buffer.data(), written, storage.data()),
              score_com_serializer_result_ok);
    car_window::WindowState decoded{};
    std::memcpy(&decoded, storage.data(), sizeof(decoded));
    EXPECT_EQ(decoded.timestamp_ms, state.timestamp_ms);
    for (std::size_t i = 0; i < state.windows.size(); ++i) {
        EXPECT_EQ(decoded.windows[i].window_id, state.windows[i].window_id);
        EXPECT_EQ(decoded.windows[i].position_percent, state.windows[i].position_percent);
        EXPECT_EQ(decoded.windows[i].is_moving, state.windows[i].is_moving);
    }
    EXPECT_EQ(decoded.child_lock, state.child_lock);
}

TEST(CarWindowSerializerTest, IgnoresTrailingBytes) {
    const auto* serializer = WindowCommandSerializer();
    const std::array<std::uint8_t, 5> payload{0x02, 0x00, 0x32, 0xFF, 0xFF};
    car_window::WindowCommand decoded{};
    ASSERT_EQ(
        score_com_serializer_deserialize(serializer, payload.data(), payload.size(), &decoded),
        score_com_serializer_result_ok);
    EXPECT_EQ(decoded.window_id, 2);
    EXPECT_EQ(decoded.target_percent, 50);
}

TEST(CarWindowSerializerTest, ShortBufferFails) {
    const auto* serializer = WindowCommandSerializer();
    const car_window::WindowCommand command{1, 2};
    std::array<std::uint8_t, 2> buffer{};
    std::size_t written = 0;
    EXPECT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &command,
                                             &written),
              score_com_serializer_result_serialization_failure);

    car_window::WindowCommand decoded{9, 9};
    EXPECT_EQ(score_com_serializer_deserialize(serializer, buffer.data(), buffer.size(), &decoded),
              score_com_serializer_result_deserialization_failure);
    EXPECT_EQ(decoded.window_id, 9);
}

TEST(CarWindowSerializerTest, InvalidBoolFailsDeserialization) {
    const auto* serializer =
        GetSerializer("car_window", score_com_serializer_element_type_field, "window_position");
    const std::array<std::uint8_t, 6> payload{0x01, 0x00, 0x00, 0x00, 0x00, 0x07};
    car_window::WindowPosition decoded{};
    EXPECT_EQ(
        score_com_serializer_deserialize(serializer, payload.data(), payload.size(), &decoded),
        score_com_serializer_result_deserialization_failure);
}

TEST(CarWindowSerializerTest, UnknownElementIsNonexistent) {
    const score_com_serializer* serializer = nullptr;
    constexpr std::string_view kService{"car_window"};
    constexpr std::string_view kEvent{"window_state"};
    constexpr std::string_view kUnknown{"door_state"};
    EXPECT_EQ(score_com_serializer_get(kService.data(), kService.size(),
                                       score_com_serializer_element_type_event, kUnknown.data(),
                                       kUnknown.size(), &serializer),
              score_com_serializer_result_serializer_nonexistent);
    EXPECT_EQ(score_com_serializer_get(kService.data(), kService.size(),
                                       score_com_serializer_element_type_field, kEvent.data(),
                                       kEvent.size(), &serializer),
              score_com_serializer_result_serializer_nonexistent);
}

TEST(CarWindowSerializerTest, NullArgumentsAreGeneralFailure) {
    const auto* serializer = WindowCommandSerializer();
    std::array<std::uint8_t, 4> buffer{};
    EXPECT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), nullptr,
                                             nullptr),
              score_com_serializer_result_general_failure);
    EXPECT_EQ(score_com_serializer_deserialize(nullptr, buffer.data(), buffer.size(), buffer.data()),
              score_com_serializer_result_general_failure);
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(nullptr), 0U);
}

}  // namespace
