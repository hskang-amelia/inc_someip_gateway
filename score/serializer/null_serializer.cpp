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

/// @file
/// This file provides a "serializer" which actually doesn't serialize and just copies the memory.
/// Elements configured with TypedSerializerConfig are dispatched to the codecs from GetCodecs().

#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string_view>

#include "score/config/mw_someip_config_generated.h"
#include "score/mw/log/logging.h"
#include "score/serializer/codec.h"
#include "score/serializer/pre_serialized_data.h"
#include "score/serializer/serializer.h"

using score::someip_gateway::serializer::Codec;
using score::someip_gateway::serializer::get_size_of_pre_serialized_data;
using score::someip_gateway::serializer::PreSerializedData;

// We don't know the actual size of the PreSerializedData struct at compile time because it depends
// on the max_message_size specified in the config, so we use a view with zero-sized array and
// calculate the size at runtime.
using PreSerializedDataView = PreSerializedData<0>;

// Exactly one of null_config and typed_config is set. codec is null if no codec matches
// typed_config->type_name().
struct score_com_serializer {
    const score::mw_someip_config::NullSerializerConfig* null_config;
    const score::mw_someip_config::TypedSerializerConfig* typed_config;
    const Codec* codec;
};

namespace {

/// Convert from opaque score_com_serializer pointer to NullSerializerConfig
const score::mw_someip_config::NullSerializerConfig* to_null_config(
    const struct score_com_serializer* serializer) {
    return serializer->null_config;
}

std::shared_ptr<const score::mw_someip_config::Root>& get_config() {
    static std::shared_ptr<const score::mw_someip_config::Root> config;
    return config;
}

/// Handles keyed by the serialization config table they were created for.
/// Built once in score_com_serializer_init, so score_com_serializer_get does not allocate.
std::map<const void*, score_com_serializer>& get_handles() {
    static std::map<const void*, score_com_serializer> handles;
    return handles;
}

void add_handle(score::mw_someip_config::SerializationConfig type, const void* table) {
    if (table == nullptr) {
        return;
    }
    if (type == score::mw_someip_config::SerializationConfig_NullSerializerConfig) {
        get_handles().emplace(
            table, score_com_serializer{
                       static_cast<const score::mw_someip_config::NullSerializerConfig*>(table),
                       nullptr, nullptr});
    } else if (type == score::mw_someip_config::SerializationConfig_TypedSerializerConfig) {
        const auto* typed =
            static_cast<const score::mw_someip_config::TypedSerializerConfig*>(table);
        const Codec* codec = nullptr;
        for (const auto& candidate : score::someip_gateway::serializer::GetCodecs()) {
            if (typed->type_name() != nullptr &&
                candidate.type_name == typed->type_name()->string_view()) {
                codec = &candidate;
                break;
            }
        }
        get_handles().emplace(table, score_com_serializer{nullptr, typed, codec});
    }
}

void build_handles(const score::mw_someip_config::Root& config) {
    get_handles().clear();
    if (config.service_types() == nullptr) {
        return;
    }
    for (const auto* service_type : *config.service_types()) {
        if (service_type->events() != nullptr) {
            for (const auto* event : *service_type->events()) {
                add_handle(event->serialization_config_type(), event->serialization_config());
            }
        }
        if (service_type->methods() != nullptr) {
            for (const auto* method : *service_type->methods()) {
                add_handle(method->request_serialization_config_type(),
                           method->request_serialization_config());
                add_handle(method->response_serialization_config_type(),
                           method->response_serialization_config());
            }
        }
    }
}

};  // anonymous namespace

score_com_serializer_result score_com_serializer_serialize(
    const struct score_com_serializer* serializer, uint8_t* buffer, size_t buffer_size,
    const void* object, size_t* written_bytes) {
    if (serializer == nullptr || buffer == nullptr || object == nullptr) {
        return score_com_serializer_result_general_failure;
    }
    if (serializer->codec != nullptr) {
        std::size_t written = 0;
        if (!serializer->codec->serialize(object, buffer, buffer_size, written)) {
            return score_com_serializer_result_serialization_failure;
        }
        if (written_bytes != nullptr) {
            *written_bytes = written;
        }
        return score_com_serializer_result_ok;
    }
    const auto* pre_serialized_data = static_cast<const PreSerializedDataView*>(object);
    std::size_t message_size = pre_serialized_data->size;
    if (message_size > buffer_size) {
        return score_com_serializer_result_serialization_failure;
    }
    std::memcpy(buffer, pre_serialized_data->data, message_size);
    if (written_bytes != nullptr) {
        *written_bytes = message_size;
    }
    return score_com_serializer_result_ok;
}

score_com_serializer_result score_com_serializer_deserialize(
    const struct score_com_serializer* serializer, const uint8_t* buffer, size_t buffer_size,
    void* object) {
    if (serializer == nullptr || buffer == nullptr || object == nullptr) {
        return score_com_serializer_result_general_failure;
    }
    if (serializer->codec != nullptr) {
        return serializer->codec->deserialize(buffer, buffer_size, object)
                   ? score_com_serializer_result_ok
                   : score_com_serializer_result_deserialization_failure;
    }
    auto* pre_serialized_data = static_cast<PreSerializedDataView*>(object);
    if (buffer_size > to_null_config(serializer)->max_message_size()) {
        return score_com_serializer_result_deserialization_failure;
    }
    std::memcpy(pre_serialized_data->data, buffer, buffer_size);
    pre_serialized_data->size = buffer_size;
    return score_com_serializer_result_ok;
}

std::size_t score_com_serializer_get_max_serialized_size(
    const struct score_com_serializer* serializer) {
    if (serializer == nullptr) {
        return 0;
    }
    if (serializer->codec != nullptr) {
        return serializer->codec->max_serialized_size;
    }
    return to_null_config(serializer)->max_message_size();
}

std::size_t score_com_serializer_get_sizeof_type(const struct score_com_serializer* serializer) {
    if (serializer == nullptr) {
        return 0;
    }
    if (serializer->codec != nullptr) {
        return serializer->codec->size_of;
    }
    return get_size_of_pre_serialized_data(to_null_config(serializer)->max_message_size());
}

std::size_t score_com_serializer_get_alignof_type(const struct score_com_serializer* serializer) {
    if (serializer != nullptr && serializer->codec != nullptr) {
        return serializer->codec->align_of;
    }
    return alignof(PreSerializedDataView);
}

score_com_serializer_result score_com_serializer_init(const char* serializer_identifier,
                                                      size_t serializer_identifier_size) {
    std::string_view serializer_id(serializer_identifier, serializer_identifier_size);

    // Read config data
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open(std::string(serializer_id), std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        score::mw::log::LogError() << "Error: Could not open config file " << serializer_id;
        return score_com_serializer_result_serializer_nonexistent;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        score::mw::log::LogError()
            << "Error: Invalid config file size: " << static_cast<std::size_t>(length);
        config_file.close();
        return score_com_serializer_result_serializer_nonexistent;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char[]>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    get_config() = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));
    build_handles(*get_config());

    return score_com_serializer_result_ok;
}

score_com_serializer_result score_com_serializer_deinit() {
    get_handles().clear();
    get_config().reset();
    return score_com_serializer_result_ok;
}

namespace {

const void* lookup_serialization_config(std::string_view service_type_name,
                                        score_com_serializer_element_type element_type,
                                        std::string_view element_name) {
    const auto config = get_config();
    if (config == nullptr || config->service_types() == nullptr) {
        return nullptr;
    }

    for (const auto* service_type : *config->service_types()) {
        if (service_type->service_type_name() == nullptr ||
            service_type->service_type_name()->string_view() != service_type_name) {
            continue;
        }

        if (element_type == score_com_serializer_element_type_event) {
            if (service_type->events() == nullptr) {
                return nullptr;
            }
            for (const auto* event : *service_type->events()) {
                if (event->event_name() != nullptr &&
                    event->event_name()->string_view() == element_name) {
                    return event->serialization_config();
                }
            }
        } else if (element_type == score_com_serializer_element_type_method_call) {
            if (service_type->methods() == nullptr) {
                return nullptr;
            }
            for (const auto* method : *service_type->methods()) {
                if (method->method_name() != nullptr &&
                    method->method_name()->string_view() == element_name) {
                    return method->request_serialization_config();
                }
            }
        } else if (element_type == score_com_serializer_element_type_method_response) {
            if (service_type->methods() == nullptr) {
                return nullptr;
            }
            for (const auto* method : *service_type->methods()) {
                if (method->method_name() != nullptr &&
                    method->method_name()->string_view() == element_name) {
                    return method->response_serialization_config();
                }
            }
        }

        return nullptr;
    }

    return nullptr;
}

}  // namespace

score_com_serializer_result score_com_serializer_get(
    const char* service_type, size_t service_type_size,
    enum score_com_serializer_element_type element_type, const char* element_name,
    size_t element_name_size, const struct score_com_serializer** serializer) {
    if (serializer == nullptr) {
        return score_com_serializer_result_general_failure;
    }

    std::string_view service_type_name(service_type, service_type_size);
    std::string_view element_name_view(element_name, element_name_size);

    const auto* serializer_config =
        lookup_serialization_config(service_type_name, element_type, element_name_view);
    const auto handle = get_handles().find(serializer_config);
    if (serializer_config == nullptr || handle == get_handles().cend()) {
        score::mw::log::LogError()
            << "Error: No serialization config found for service_type=" << service_type_name
            << " element=" << element_name_view;
        return score_com_serializer_result_serializer_nonexistent;
    }
    if (handle->second.typed_config != nullptr && handle->second.codec == nullptr) {
        score::mw::log::LogError()
            << "Error: No codec found for type_name="
            << (handle->second.typed_config->type_name() != nullptr
                    ? handle->second.typed_config->type_name()->string_view()
                    : std::string_view{})
            << " (service_type=" << service_type_name << " element=" << element_name_view << ")";
        return score_com_serializer_result_serializer_nonexistent;
    }

    *serializer = &handle->second;

    return score_com_serializer_result_ok;
}
