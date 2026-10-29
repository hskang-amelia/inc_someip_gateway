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
/// Bounds-checked SOME/IP wire encoding for fixed-size data: big-endian integers and
/// IEEE 754 floats, one-byte booleans, fixed-size arrays and structs without length fields.
/// Strings and dynamic-length arrays are not supported.

#ifndef SCORE_SERIALIZER_TYPED_SOMEIP_WIRE_H
#define SCORE_SERIALIZER_TYPED_SOMEIP_WIRE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace score::someip_gateway::serializer::typed {

template <typename T>
inline constexpr bool kIsWirePrimitive = std::is_same_v<T, bool> || std::is_integral_v<T> ||
                                         std::is_same_v<T, float> || std::is_same_v<T, double>;

template <typename T>
constexpr std::size_t WireSizeOf() {
    static_assert(kIsWirePrimitive<T>);
    return std::is_same_v<T, bool> ? 1U : sizeof(T);
}

class Writer {
   public:
    Writer(std::uint8_t* buffer, std::size_t size) : buffer_{buffer}, size_{size} {}

    template <typename T>
    bool Put(T value) {
        static_assert(kIsWirePrimitive<T>);
        if constexpr (std::is_same_v<T, bool>) {
            return PutUnsigned<std::uint8_t>(value ? 1U : 0U);
        } else if constexpr (std::is_same_v<T, float>) {
            std::uint32_t bits{};
            std::memcpy(&bits, &value, sizeof(bits));
            return PutUnsigned(bits);
        } else if constexpr (std::is_same_v<T, double>) {
            std::uint64_t bits{};
            std::memcpy(&bits, &value, sizeof(bits));
            return PutUnsigned(bits);
        } else {
            return PutUnsigned(static_cast<std::make_unsigned_t<T>>(value));
        }
    }

    std::size_t Position() const { return pos_; }

   private:
    template <typename U>
    bool PutUnsigned(U value) {
        if (size_ - pos_ < sizeof(U)) {
            return false;
        }
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            buffer_[pos_ + i] = static_cast<std::uint8_t>(value >> (8U * (sizeof(U) - 1U - i)));
        }
        pos_ += sizeof(U);
        return true;
    }

    std::uint8_t* buffer_;
    std::size_t size_;
    std::size_t pos_{0};
};

class Reader {
   public:
    Reader(const std::uint8_t* buffer, std::size_t size) : buffer_{buffer}, size_{size} {}

    /// Returns false on buffer underrun or on a boolean byte other than 0 or 1.
    template <typename T>
    bool Get(T& value) {
        static_assert(kIsWirePrimitive<T>);
        if constexpr (std::is_same_v<T, bool>) {
            std::uint8_t raw{};
            if (!GetUnsigned(raw) || raw > 1U) {
                return false;
            }
            value = raw == 1U;
            return true;
        } else if constexpr (std::is_same_v<T, float>) {
            std::uint32_t bits{};
            if (!GetUnsigned(bits)) {
                return false;
            }
            std::memcpy(&value, &bits, sizeof(bits));
            return true;
        } else if constexpr (std::is_same_v<T, double>) {
            std::uint64_t bits{};
            if (!GetUnsigned(bits)) {
                return false;
            }
            std::memcpy(&value, &bits, sizeof(bits));
            return true;
        } else {
            std::make_unsigned_t<T> raw{};
            if (!GetUnsigned(raw)) {
                return false;
            }
            value = static_cast<T>(raw);
            return true;
        }
    }

    std::size_t Position() const { return pos_; }

   private:
    template <typename U>
    bool GetUnsigned(U& value) {
        if (size_ - pos_ < sizeof(U)) {
            return false;
        }
        U result{0};
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            result = static_cast<U>((result << 8U) | buffer_[pos_ + i]);
        }
        value = result;
        pos_ += sizeof(U);
        return true;
    }

    const std::uint8_t* buffer_;
    std::size_t size_;
    std::size_t pos_{0};
};

}  // namespace score::someip_gateway::serializer::typed

#endif  // SCORE_SERIALIZER_TYPED_SOMEIP_WIRE_H
