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

#ifndef IMPL_SOMEIPD_ROUTING
#define IMPL_SOMEIPD_ROUTING

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vsomeip/vsomeip.hpp>

#include "score/config/mw_someip_config_generated.h"
#include "score/result/result.h"
#include "score/someip/types.h"

namespace score::someipd {

using score::someip::EventId;
using score::someip::InstanceId;
using score::someip::ServiceId;

/// Manages the vsomeip application lifecycle and SOME/IP service registrations.
///
/// Owns a vsomeip application instance, registers event subscriptions and
/// service offerings from configuration, and dispatches incoming SOME/IP
/// messages. IPC forwarding to/from gatewayd is handled via SOCom connectors
/// that are wired in externally (see main.cpp).
class Routing {
   public:
    /// @brief Creates a Routing instance from the given configuration.
    ///
    /// Initialises a vsomeip application and registers it with the vsomeip runtime.
    /// Subscriptions and service offerings are applied once Run() is called and the
    /// application has registered with the vsomeip routing daemon.
    ///
    /// @param config SOME/IP gateway configuration describing the services, instances,
    ///               events, and methods to subscribe to or offer on the network.
    /// @return A fully initialised Routing instance, or an error if the vsomeip application
    ///         could not be created or initialised.
    static Result<Routing> Create(std::shared_ptr<const score::mw_someip_config::Root> config);

    ~Routing() = default;

    Routing(const Routing&) = delete;
    Routing& operator=(const Routing&) = delete;
    Routing(Routing&&) noexcept;
    Routing& operator=(Routing&&) noexcept;

    /// Returns the vsomeip application instance.
    std::shared_ptr<vsomeip::application> get_application() const noexcept { return application_; }

    /// Runs the routing loop, blocking until @p shutdown_requested is set to true.
    /// \param on_registered Optional callback invoked once vsomeip reaches ST_REGISTERED.
    ///        Use this to call setup_vsomeip() on RemoteNetworkService instances.
    void Run(std::atomic<bool>& shutdown_requested, std::function<void()> on_registered = {});

    /// Allows or withdraws this node's SOME/IP offerings, as Network Management
    /// decides whether the communication cluster may be awake.
    ///
    /// Withdrawing matters because service discovery does not consult anyone: it
    /// re-offers and re-finds on its own timers, so a cluster NM has released
    /// stays busy and never goes quiet while this daemon keeps offering. This is
    /// where NM's decision reaches the traffic.
    ///
    /// Safe to call from another thread — it is normally called from
    /// NetworkControl's reader thread — and does nothing when the state is
    /// already what is asked for. Nothing is offered before vsomeip has
    /// registered, whatever is allowed here; the two conditions are combined,
    /// so allowing communication early is remembered rather than lost.
    ///
    /// **Communication is allowed until told otherwise.** A daemon that waited
    /// for permission that never came would never offer anything, and a SOME/IP
    /// stack that is silent because nobody spoke to it is a loss of function
    /// with no way back. Offering on a bus NM is about to release is the
    /// recoverable direction: the next record puts it right.
    void SetCommunicationAllowed(bool allowed);

   private:
    explicit Routing(std::shared_ptr<const score::mw_someip_config::Root> config);
    void SetupOfferings();
    void StopOfferings();
    /// Brings the offerings in line with @p gate_. Must be called holding its lock.
    void ApplyOfferingState();
    void ProcessMessages(std::atomic<bool>& shutdown_requested);
    InstanceId LookupInstanceId(ServiceId service_id) const;

    /// Whether this node should currently be offering, and why.
    ///
    /// Held behind a shared_ptr so that Routing stays movable: Create() returns
    /// one by value, and a std::mutex member would make that impossible.
    struct OfferingGate {
        std::mutex lock{};
        /// vsomeip has reached ST_REGISTERED. Nothing can be offered before it.
        bool registered{false};
        /// Network Management permits communication. True until it says not.
        bool allowed{true};
        /// What has actually been offered, so a repeated decision is not a
        /// repeated call into vsomeip.
        bool offering{false};
    };

    std::shared_ptr<const score::mw_someip_config::Root> config_;
    std::shared_ptr<vsomeip::application> application_{};
    std::shared_ptr<vsomeip::payload> payload_{};
    std::shared_ptr<OfferingGate> gate_{std::make_shared<OfferingGate>()};
    std::thread processing_thread_{};
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_ROUTING
