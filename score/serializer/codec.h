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

#ifndef SRC_SERIALIZER_CODEC
#define SRC_SERIALIZER_CODEC

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace score::someip_gateway::serializer {

/// Converts one IPC sample type to and from its SOME/IP payload.
/// Selected by TypedSerializerConfig::type_name.
struct Codec {
    std::string_view type_name;
    std::size_t max_serialized_size;
    std::size_t size_of;
    std::size_t align_of;
    bool (*serialize)(const void* object, std::uint8_t* buffer, std::size_t buffer_size,
                      std::size_t& written_bytes);
    bool (*deserialize)(const std::uint8_t* buffer, std::size_t buffer_size, void* object);
};

/// Codecs linked into the serializer library. Defined exactly once per library,
/// either by generated code or by :no_codecs.
std::span<const Codec> GetCodecs();

}  // namespace score::someip_gateway::serializer

#endif  // SRC_SERIALIZER_CODEC
