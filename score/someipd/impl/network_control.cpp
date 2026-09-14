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

#include "network_control.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

namespace score::someipd {

namespace {

/// How long a blocking wait sits before looking at the stop flag.
///
/// The cost of a larger value is only how long Stop() takes; the cost of a
/// smaller one is wakeups for the life of the daemon on a node whose standstill
/// power is the reason Network Management exists at all.
constexpr int kPollIntervalMs{250};

/// One backlog entry. A second Network Management is not expected, and one
/// waiting to be accepted while the first is being read is not wanted.
constexpr int kListenBacklog{1};

}  // namespace

std::unique_ptr<NetworkControl> NetworkControl::Create(const std::string& socket_path,
                                                       Handler handler) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    // The address has a fixed-size path, and one byte of it is the terminator.
    // A path that does not fit cannot be bound, and truncating it would bind a
    // different socket than the one the caller named.
    if (socket_path.empty() || (socket_path.size() >= sizeof(address.sun_path))) {
        return nullptr;
    }
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size());

    const int listen_fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (listen_fd < 0) {
        return nullptr;
    }

    // A socket file left behind by a previous run is not in use and would
    // otherwise make every restart of this daemon fail to bind.
    ::unlink(socket_path.c_str());

    if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(listen_fd);
        return nullptr;
    }
    if (::listen(listen_fd, kListenBacklog) != 0) {
        ::close(listen_fd);
        ::unlink(socket_path.c_str());
        return nullptr;
    }

    // Not std::make_unique: the constructor is private, and widening it to
    // public so a factory helper can reach it would let a caller build one of
    // these around a descriptor that was never bound.
    std::unique_ptr<NetworkControl> control{
        new NetworkControl(listen_fd, socket_path, std::move(handler))};
    control->reader_ = std::thread([raw = control.get()]() { raw->Run(); });
    return control;
}

NetworkControl::NetworkControl(int listen_fd, std::string socket_path, Handler handler)
    : listen_fd_(listen_fd), socket_path_(std::move(socket_path)), handler_(std::move(handler)) {}

NetworkControl::~NetworkControl() { Stop(); }

void NetworkControl::Stop() {
    // exchange rather than store, so a second Stop() — the destructor after an
    // explicit one — neither joins a thread that is already joined nor closes a
    // descriptor twice.
    if (stopping_.exchange(true)) {
        return;
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

NetworkControl::Observations NetworkControl::GetObservations() const {
    Observations seen{};
    seen.connected = connected_.load();
    seen.connections = connections_.load();
    seen.records = records_.load();
    seen.malformed = malformed_.load();
    seen.partial_reads = partial_reads_.load();
    return seen;
}

void NetworkControl::Run() {
    while (!stopping_.load()) {
        pollfd waiting{};
        waiting.fd = listen_fd_;
        waiting.events = POLLIN;

        const int ready{::poll(&waiting, 1, kPollIntervalMs)};
        if (ready == 0) {
            // Timed out, which is how this loop reaches the stop flag.
            continue;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        const int client_fd{::accept(listen_fd_, nullptr, nullptr)};
        if (client_fd < 0) {
            continue;
        }

        connections_.fetch_add(1U);
        connected_.store(true);
        ServeClient(client_fd);
        connected_.store(false);
        ::close(client_fd);
    }
}

void NetworkControl::ServeClient(int client_fd) {
    std::array<std::uint8_t, kNmRecordSize> record{};
    std::size_t filled{0U};

    while (!stopping_.load()) {
        pollfd waiting{};
        waiting.fd = client_fd;
        waiting.events = POLLIN;

        const int ready{::poll(&waiting, 1, kPollIntervalMs)};
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        const ssize_t received{::read(client_fd, record.data() + filled, kNmRecordSize - filled)};
        if (received < 0) {
            if ((errno == EINTR) || (errno == EAGAIN)) {
                continue;
            }
            return;
        }
        if (received == 0) {
            // Network Management closed the connection. Run() goes back to
            // waiting for it to come back, and whatever it last said stands
            // until it does — a gateway that re-opened every network because NM
            // restarted would defeat the shutdown NM had just ordered.
            return;
        }

        filled += static_cast<std::size_t>(received);
        if (filled < kNmRecordSize) {
            // A stream socket may split a record, so a short read is carried
            // over rather than discarded. Discarding it would desynchronise
            // every record that followed.
            partial_reads_.fetch_add(1U);
            continue;
        }
        filled = 0U;

        NmComStateRecord decoded{};
        if (!DecodeNmComStateRecord(record.data(), record.size(), decoded)) {
            malformed_.fetch_add(1U);
            continue;
        }

        records_.fetch_add(1U);
        if (handler_) {
            handler_(decoded);
        }
    }
}

}  // namespace score::someipd
