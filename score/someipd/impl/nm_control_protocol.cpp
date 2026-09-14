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

#include "nm_control_protocol.h"

namespace score::someipd {

namespace {

bool IsKnownComState(std::uint8_t value) {
    return (value == static_cast<std::uint8_t>(ComState::kNoCom)) ||
           (value == static_cast<std::uint8_t>(ComState::kFullCom));
}

bool IsKnownNmState(std::uint8_t value) {
    switch (value) {
        case static_cast<std::uint8_t>(NmState::kBusSleep):
        case static_cast<std::uint8_t>(NmState::kPrepareBusSleep):
        case static_cast<std::uint8_t>(NmState::kRepeatMessage):
        case static_cast<std::uint8_t>(NmState::kNormalOperation):
        case static_cast<std::uint8_t>(NmState::kReadySleep):
        case kNmStateUnknown:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool DecodeNmComStateRecord(const std::uint8_t* bytes, std::size_t length, NmComStateRecord& out) {
    if ((bytes == nullptr) || (length != kNmRecordSize)) {
        return false;
    }
    if (bytes[0] != kNmProtocolVersion) {
        return false;
    }
    if (!IsKnownComState(bytes[1]) || !IsKnownNmState(bytes[2])) {
        return false;
    }

    const std::size_t name_length{static_cast<std::size_t>(bytes[3])};
    if (name_length > kNmMaxClusterName) {
        return false;
    }
    // A record naming no cluster tells this daemon nothing it could act on, and
    // is more likely a sender bug than an empty-named cluster.
    if (name_length == 0U) {
        return false;
    }

    out.cluster.assign(reinterpret_cast<const char*>(&bytes[4]), name_length);
    out.com_state = static_cast<ComState>(bytes[1]);
    out.nm_state = static_cast<NmState>(bytes[2]);
    return true;
}

}  // namespace score::someipd
