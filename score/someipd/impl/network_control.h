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

#ifndef IMPL_SOMEIPD_NETWORK_CONTROL
#define IMPL_SOMEIPD_NETWORK_CONTROL

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "nm_control_protocol.h"

namespace score::someipd {

/// Listens for Network Management's communication-state records and hands each
/// one to a handler.
///
/// Owns a Unix domain socket that NM connects to, and a thread that reads it.
/// One client at a time: there is one Network Management on a node, and a
/// second connection claiming to be it would be a second opinion about whether
/// a bus may sleep.
///
/// # Why this reports through a handler and logs nothing
///
/// So that it can be built and tested without the daemon around it. Everything
/// here is POSIX and the standard library, with no vsomeip, no configuration and
/// no logging framework, which is what lets the record handling be exercised
/// directly rather than only through a running gateway. What to do about a
/// record, and what to say about one, both belong to the caller — which knows
/// what is being offered and has the logger.
///
/// # Lifetime
///
/// Create() starts the thread; the destructor stops it and removes the socket
/// file. A handler is called on the reader thread, never re-entrantly, and must
/// not block for long: nothing else reads the socket while it runs, so a slow
/// handler is backpressure on Network Management.
class NetworkControl {
   public:
    /// Called once per accepted record, on the reader thread.
    using Handler = std::function<void(const NmComStateRecord&)>;

    /// What the listener has seen. Counters rather than a verdict; the daemon
    /// decides what a number here is worth.
    struct Observations {
        /// Whether Network Management is connected right now.
        bool connected{false};
        /// Connections accepted since start. More than one means NM restarted.
        std::uint64_t connections{0U};
        /// Records accepted and handed to the handler.
        std::uint64_t records{0U};
        /// Records refused by DecodeNmComStateRecord().
        std::uint64_t malformed{0U};
        /// Reads that returned less than a whole record, which a stream socket
        /// is entitled to do and which the reader carries over rather than
        /// discarding.
        std::uint64_t partial_reads{0U};
    };

    /// Binds @p socket_path and starts reading.
    ///
    /// Returns nullptr when the socket cannot be bound — a path that is too
    /// long for a Unix socket address, a directory that does not exist, or a
    /// permission the daemon has not got. An existing file at the path is
    /// removed first, since a socket left behind by a previous run would
    /// otherwise make every restart fail.
    static std::unique_ptr<NetworkControl> Create(const std::string& socket_path, Handler handler);

    ~NetworkControl();

    NetworkControl(const NetworkControl&) = delete;
    NetworkControl& operator=(const NetworkControl&) = delete;
    NetworkControl(NetworkControl&&) = delete;
    NetworkControl& operator=(NetworkControl&&) = delete;

    /// Stops the reader thread and waits for it. Idempotent.
    void Stop();

    Observations GetObservations() const;

   private:
    NetworkControl(int listen_fd, std::string socket_path, Handler handler);

    void Run();
    /// Reads records from one connection until it closes or the listener stops.
    void ServeClient(int client_fd);

    int listen_fd_;
    std::string socket_path_;
    Handler handler_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> connections_{0U};
    std::atomic<std::uint64_t> records_{0U};
    std::atomic<std::uint64_t> malformed_{0U};
    std::atomic<std::uint64_t> partial_reads_{0U};
    std::thread reader_{};
};

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_NETWORK_CONTROL
