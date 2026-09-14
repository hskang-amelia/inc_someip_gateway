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

#ifndef IMPL_SOMEIPD_NM_CONTROL_PROTOCOL
#define IMPL_SOMEIPD_NM_CONTROL_PROTOCOL

#include <cstddef>
#include <cstdint>
#include <string>

namespace score::someipd {

/// The records Network Management sends this daemon, and how to read one.
///
/// Network Management decides when a communication cluster may be awake. SOME/IP
/// service discovery does not consult it: offers and finds repeat on their own
/// timers, so a cluster NM has released stays busy and never goes quiet unless
/// the stack that owns the traffic is told to stop. These records are NM telling
/// it. They travel one way — NM is not asking permission, and nothing is sent
/// back.
///
/// The layout is fixed and byte-granular on purpose. Every field is one byte or
/// a run of bytes, so there is no byte order to agree on and no parser to keep
/// in step with the sender:
///
/// ```text
/// byte 0      protocol version           (kNmProtocolVersion)
/// byte 1      communication state        (ComState)
/// byte 2      NM state                   (NmState, or kNmStateUnknown)
/// byte 3      cluster name length, n     (n <= kNmMaxClusterName)
/// byte 4..36  cluster name, NUL-padded
/// ```
///
/// The producing end is the `nm-someip` crate of the `nm` repository
/// (`score/nm_someip/src/gateway.rs`), which documents the same layout from the
/// other side.

/// Version byte every record carries.
///
/// A reader that meets a version it does not know must refuse the record rather
/// than guess at it: the field it would be guessing about decides whether a bus
/// may sleep.
inline constexpr std::uint8_t kNmProtocolVersion{1U};

/// Longest cluster name a record can carry, in bytes.
inline constexpr std::size_t kNmMaxClusterName{32U};

/// Bytes in one record.
inline constexpr std::size_t kNmRecordSize{4U + kNmMaxClusterName};

/// NM state byte meaning "NM has reported no state for this cluster".
///
/// Sent when only the two-value communication state is known, which is a
/// legitimate configuration on the sending side rather than a degraded one.
inline constexpr std::uint8_t kNmStateUnknown{0xFFU};

/// Whether a cluster may communicate. The two-value answer, as AUTOSAR Adaptive
/// has it.
enum class ComState : std::uint8_t {
    kNoCom = 0U,
    kFullCom = 1U,
};

/// Where the cluster's NM state machine is.
///
/// Finer than ComState, and the difference is worth having: a cluster in
/// kPrepareBusSleep is on its way down but can still be pulled back up without a
/// wake-up, so a gateway may reasonably stop offering on it while keeping
/// connections that a return to kNormalOperation would need again.
enum class NmState : std::uint8_t {
    kBusSleep = 0U,
    kPrepareBusSleep = 1U,
    kRepeatMessage = 2U,
    kNormalOperation = 3U,
    kReadySleep = 4U,
    /// Not a state NM can be in — the absence of a report. See kNmStateUnknown.
    kUnknown = kNmStateUnknown,
};

/// One decoded record.
struct NmComStateRecord {
    std::string cluster{};
    ComState com_state{ComState::kNoCom};
    NmState nm_state{NmState::kUnknown};
};

/// Reads one record out of exactly kNmRecordSize bytes.
///
/// Returns false, leaving @p out untouched, when the record cannot be trusted:
/// a length that is not exactly kNmRecordSize, an unknown protocol version, a
/// name length that does not fit, a communication state that is neither value,
/// or an NM state that is neither a known state nor kNmStateUnknown.
///
/// Every one of those is refused rather than interpreted generously, because
/// each would otherwise become a guess about whether a network may sleep.
bool DecodeNmComStateRecord(const std::uint8_t* bytes, std::size_t length, NmComStateRecord& out);

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_NM_CONTROL_PROTOCOL
