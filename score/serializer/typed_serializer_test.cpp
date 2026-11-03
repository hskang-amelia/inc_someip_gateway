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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/serializer/serializer.h"
#include "score/serializer/test_codecs_types.h"

namespace {

using score::someip_gateway::serializer::get_size_of_pre_serialized_data;
namespace config = score::mw_someip_config;
namespace types = score::someip_gateway::serializer::test;

const std::string kService = "test_service";
const std::string kTypedEvent = "typed_event";
const std::string kNullEvent = "null_event";
const std::string kUnknownTypeEvent = "unknown_type_event";
const std::string kMethod = "typed_method";
constexpr uint32_t kNullMaxMessageSize = 64;

std::vector<uint8_t> build_test_config() {
    flatbuffers::FlatBufferBuilder fbb;

    auto inner = config::CreateTypedSerializerConfigDirect(
        fbb, "score::someip_gateway::serializer::test::Inner");
    auto unknown = config::CreateTypedSerializerConfigDirect(fbb, "no::such::Type");
    auto null_config = config::CreateNullSerializerConfig(fbb, kNullMaxMessageSize);
    auto mode = config::CreateTypedSerializerConfigDirect(
        fbb, "score::someip_gateway::serializer::test::Mode");

    std::vector<flatbuffers::Offset<config::Event>> events{
        config::CreateEvent(fbb, 1, fbb.CreateString(kTypedEvent),
                            config::SerializationConfig_TypedSerializerConfig, inner.Union()),
        config::CreateEvent(fbb, 2, fbb.CreateString(kNullEvent),
                            config::SerializationConfig_NullSerializerConfig, null_config.Union()),
        config::CreateEvent(fbb, 3, fbb.CreateString(kUnknownTypeEvent),
                            config::SerializationConfig_TypedSerializerConfig, unknown.Union()),
    };
    std::vector<flatbuffers::Offset<config::Method>> methods{config::CreateMethod(
        fbb, 10, fbb.CreateString(kMethod), config::SerializationConfig_TypedSerializerConfig,
        mode.Union(), config::SerializationConfig_TypedSerializerConfig, inner.Union())};

    auto service_type =
        config::CreateServiceTypeDirect(fbb, kService.c_str(), 1, 1, 0, &events, &methods);
    std::vector<flatbuffers::Offset<config::ServiceType>> service_types{service_type};
    fbb.Finish(config::CreateRootDirect(fbb, &service_types));
    return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
}

class TypedSerializer_test : public ::testing::Test {
   protected:
    void SetUp() override {
        const auto data = build_test_config();
        const std::string path = testing::TempDir() + "typed_serializer_test_config.bin";
        std::ofstream file(path, std::ios::binary | std::ios::out);
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        file.close();
        ASSERT_EQ(score_com_serializer_init(path.c_str(), path.size()),
                  score_com_serializer_result_ok);
    }

    void TearDown() override {
        EXPECT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
    }

    static score_com_serializer_result get(const std::string& element,
                                           score_com_serializer_element_type type,
                                           const score_com_serializer** serializer) {
        return score_com_serializer_get(kService.c_str(), kService.size(), type, element.c_str(),
                                        element.size(), serializer);
    }

    static const score_com_serializer* get_event(const std::string& element) {
        const score_com_serializer* serializer = nullptr;
        EXPECT_EQ(get(element, score_com_serializer_element_type_event, &serializer),
                  score_com_serializer_result_ok);
        return serializer;
    }
};

TEST_F(TypedSerializer_test, typed_event_reports_codec_type_info) {
    const auto* serializer = get_event(kTypedEvent);
    ASSERT_NE(serializer, nullptr);
    EXPECT_EQ(score_com_serializer_get_sizeof_type(serializer), sizeof(types::Inner));
    EXPECT_EQ(score_com_serializer_get_alignof_type(serializer), alignof(types::Inner));
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer), 7U);
}

TEST_F(TypedSerializer_test, typed_event_round_trips) {
    const auto* serializer = get_event(kTypedEvent);
    ASSERT_NE(serializer, nullptr);
    const types::Inner inner{true, 1.0F};
    std::vector<uint8_t> buffer(16);
    std::size_t written = 0;
    ASSERT_EQ(
        score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &inner, &written),
        score_com_serializer_result_ok);
    buffer.resize(written);
    EXPECT_EQ(buffer, (std::vector<uint8_t>{0x00, 0x05, 0x01, 0x3F, 0x80, 0x00, 0x00}));

    types::Inner restored{};
    ASSERT_EQ(score_com_serializer_deserialize(serializer, buffer.data(), buffer.size(), &restored),
              score_com_serializer_result_ok);
    EXPECT_TRUE(restored.flag);
    EXPECT_EQ(restored.temperature, 1.0F);
}

TEST_F(TypedSerializer_test, typed_event_reports_codec_failures) {
    const auto* serializer = get_event(kTypedEvent);
    ASSERT_NE(serializer, nullptr);
    const types::Inner inner{true, 1.0F};
    std::vector<uint8_t> buffer(3);
    std::size_t written = 0;
    EXPECT_EQ(
        score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &inner, &written),
        score_com_serializer_result_serialization_failure);
    types::Inner restored{};
    EXPECT_EQ(score_com_serializer_deserialize(serializer, buffer.data(), buffer.size(), &restored),
              score_com_serializer_result_deserialization_failure);
}

TEST_F(TypedSerializer_test, null_event_in_same_config_keeps_null_behaviour) {
    const auto* serializer = get_event(kNullEvent);
    ASSERT_NE(serializer, nullptr);
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer), kNullMaxMessageSize);
    EXPECT_EQ(score_com_serializer_get_sizeof_type(serializer),
              get_size_of_pre_serialized_data(kNullMaxMessageSize));
}

TEST_F(TypedSerializer_test, unknown_type_name_is_nonexistent) {
    const score_com_serializer* serializer = nullptr;
    EXPECT_EQ(get(kUnknownTypeEvent, score_com_serializer_element_type_event, &serializer),
              score_com_serializer_result_serializer_nonexistent);
}

TEST_F(TypedSerializer_test, method_request_and_response_use_own_codecs) {
    const score_com_serializer* request = nullptr;
    const score_com_serializer* response = nullptr;
    ASSERT_EQ(get(kMethod, score_com_serializer_element_type_method_call, &request),
              score_com_serializer_result_ok);
    ASSERT_EQ(get(kMethod, score_com_serializer_element_type_method_response, &response),
              score_com_serializer_result_ok);
    EXPECT_EQ(score_com_serializer_get_sizeof_type(request), sizeof(types::Mode));
    EXPECT_EQ(score_com_serializer_get_sizeof_type(response), sizeof(types::Inner));
}

TEST_F(TypedSerializer_test, serialize_with_null_serializer_fails) {
    const types::Inner inner{};
    std::vector<uint8_t> buffer(16);
    EXPECT_EQ(
        score_com_serializer_serialize(nullptr, buffer.data(), buffer.size(), &inner, nullptr),
        score_com_serializer_result_general_failure);
}

}  // namespace
