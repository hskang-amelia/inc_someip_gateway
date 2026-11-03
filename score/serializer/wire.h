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
/// SOME/IP payload primitives (AUTOSAR PRS_SOMEIPProtocol) used by generated codecs.

#ifndef SRC_SERIALIZER_WIRE
#define SRC_SERIALIZER_WIRE

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace score::someip_gateway::serializer::wire {

class Writer {
   public:
    Writer(std::uint8_t* buffer, std::size_t size, bool big_endian)
        : buffer_{buffer}, size_{size}, big_endian_{big_endian} {}

    std::size_t Position() const { return pos_; }

    bool PutUint(std::uint64_t value, std::size_t width) {
        if (size_ - pos_ < width) {
            return false;
        }
        for (std::size_t i = 0; i < width; ++i) {
            const std::size_t shift = big_endian_ ? (width - 1U - i) * 8U : i * 8U;
            buffer_[pos_ + i] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        }
        pos_ += width;
        return true;
    }

    bool PutBytes(const void* data, std::size_t length) {
        if (size_ - pos_ < length) {
            return false;
        }
        if (length != 0U) {
            std::memcpy(buffer_ + pos_, data, length);
        }
        pos_ += length;
        return true;
    }

    /// Reserves a length field of @p width bytes; fill it with PatchLength().
    bool Reserve(std::size_t width) {
        if (size_ - pos_ < width) {
            return false;
        }
        pos_ += width;
        return true;
    }

    /// Writes the number of bytes written since the field at @p at into that field.
    void PatchLength(std::size_t at, std::size_t width) {
        const std::size_t end = pos_;
        pos_ = at;
        static_cast<void>(PutUint(end - at - width, width));
        pos_ = end;
    }

   private:
    std::uint8_t* buffer_;
    std::size_t size_;
    std::size_t pos_{0};
    bool big_endian_;
};

class Reader {
   public:
    Reader(const std::uint8_t* buffer, std::size_t size, bool big_endian)
        : buffer_{buffer}, size_{size}, big_endian_{big_endian} {}

    std::size_t Remaining() const { return size_ - pos_; }

    bool GetUint(std::uint64_t& value, std::size_t width) {
        if (Remaining() < width) {
            return false;
        }
        value = 0;
        for (std::size_t i = 0; i < width; ++i) {
            const std::size_t shift = big_endian_ ? (width - 1U - i) * 8U : i * 8U;
            value |= static_cast<std::uint64_t>(buffer_[pos_ + i]) << shift;
        }
        pos_ += width;
        return true;
    }

    /// Reads a length field of @p width bytes and returns a reader over that many bytes.
    bool SubReader(std::size_t width, Reader& sub) {
        std::uint64_t length = 0;
        if (!GetUint(length, width) || length > Remaining()) {
            return false;
        }
        sub = Reader{buffer_ + pos_, static_cast<std::size_t>(length), big_endian_};
        pos_ += static_cast<std::size_t>(length);
        return true;
    }

    const std::uint8_t* Data() const { return buffer_ + pos_; }

   private:
    const std::uint8_t* buffer_;
    std::size_t size_;
    std::size_t pos_{0};
    bool big_endian_;
};

/// Reads the raw byte, as a bool in shared memory may hold any value.
inline bool PutBool(Writer& writer, const bool& value) {
    std::uint8_t byte = 0U;
    std::memcpy(&byte, &value, sizeof(byte));
    return byte <= 1U && writer.PutUint(byte, 1U);
}

inline constexpr std::array<std::uint8_t, 3> kUtf8Bom{0xEFU, 0xBBU, 0xBFU};

/// Dynamic-length UTF-8 string: length field, BOM, content, zero terminator.
inline bool PutString(Writer& writer, const char* data, std::uint32_t length,
                      std::uint32_t capacity, std::size_t length_width) {
    if (length > capacity) {
        return false;
    }
    const std::size_t at = writer.Position();
    if (!writer.Reserve(length_width) || !writer.PutBytes(kUtf8Bom.data(), kUtf8Bom.size()) ||
        !writer.PutBytes(data, length) || !writer.PutUint(0U, 1U)) {
        return false;
    }
    writer.PatchLength(at, length_width);
    return true;
}

/// Accepts a missing BOM and strips trailing zero bytes.
inline bool GetString(Reader& reader, char* data, std::uint32_t& length, std::uint32_t capacity,
                      std::size_t length_width) {
    Reader sub{nullptr, 0U, true};
    if (!reader.SubReader(length_width, sub)) {
        return false;
    }
    const std::uint8_t* begin = sub.Data();
    std::size_t size = sub.Remaining();
    if (size >= kUtf8Bom.size() && std::memcmp(begin, kUtf8Bom.data(), kUtf8Bom.size()) == 0) {
        begin += kUtf8Bom.size();
        size -= kUtf8Bom.size();
    }
    while (size > 0U && begin[size - 1U] == 0U) {
        --size;
    }
    if (size > capacity) {
        return false;
    }
    if (size != 0U) {
        std::memcpy(data, begin, size);
    }
    length = static_cast<std::uint32_t>(size);
    return true;
}

}  // namespace score::someip_gateway::serializer::wire

#endif  // SRC_SERIALIZER_WIRE
