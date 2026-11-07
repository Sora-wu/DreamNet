//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/wakeup.h>
#include <common/socket.h>
#include <common/log.hpp>

#include <chrono>

using namespace Dream;
using namespace Dream::detail;

namespace {
    using Clock = std::chrono::steady_clock;

    void waitForSocket(SocketFd fd, short events, Clock::time_point deadline) {
        while (true) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0) {
                LOG_FATAL("wakeup TCP socket setup timed out");
            }

            WSAPOLLFD descriptor{fd, events, 0};
            const int result = WSAPoll(&descriptor, 1, static_cast<int>(remaining));
            if (result > 0) {
                if (descriptor.revents & POLLNVAL) {
                    LOG_FATAL("wakeup TCP socket is invalid");
                }
                const int error = Socket::getError(fd);
                if (error != 0) {
                    LOG_FATAL("wakeup TCP socket setup error: {}", Socket::errorString(error));
                }
                if (descriptor.revents & events) {
                    return;
                }
                LOG_FATAL("wakeup TCP socket closed during setup");
            }
            if (result == 0) {
                LOG_FATAL("wakeup TCP socket setup timed out");
            }
            const int error = Socket::getLastError();
            if (!Socket::isInterrupted(error)) {
                LOG_FATAL("wakeup TCP socket poll error: {}", Socket::errorString(error));
            }
        }
    }
}

class Wakeup::Impl {
public:
    Impl() : writer(SOCK_STREAM) {
        Socket listener(SOCK_STREAM);
        listener.bind(Address(0));
        listener.listen();

        Address listenerAddress;
        if (!Socket::getLocalAddress(listener.getFd(), listenerAddress)) {
            LOG_FATAL("wakeup TCP listener address error: {}", Socket::errorString(Socket::getLastError()));
        }

        // 整个连接对的建立共用一个截止时间，不改变运行时的非阻塞模式。
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        if (writer.connect(listenerAddress) != 0) {
            const int error = Socket::getLastError();
            if (!Socket::isConnectInProgress(error)) {
                LOG_FATAL("wakeup TCP connect error: {}", Socket::errorString(error));
            }
            waitForSocket(writer.getFd(), POLLWRNORM, deadline);
        }

        Address peerAddress;
        while (reader.getFd() == INVALID_SOCKET_FD) {
            waitForSocket(listener.getFd(), POLLRDNORM, deadline);
            reader.setFd(listener.accept(peerAddress));
            if (reader.getFd() == INVALID_SOCKET_FD) {
                const int error = Socket::getLastError();
                if (!Socket::isInterrupted(error) && !Socket::isWouldBlock(error)) {
                    LOG_FATAL("wakeup TCP accept error: {}", Socket::errorString(error));
                }
            }
        }

        reader.setTcpNoDelay(true);
        writer.setTcpNoDelay(true);
        // listener 在构造结束时关闭，仅保留连接对。
    }

    Socket reader;
    Socket writer;
};

Wakeup::Wakeup() : impl_(std::make_unique<Impl>()) {}

Wakeup::~Wakeup() = default;

SocketFd Wakeup::getFd() const {
    return impl_->reader.getFd();
}

void Wakeup::notify() const {
    const char one = 1;
    std::ptrdiff_t result;
    do {
        result = impl_->writer.send(&one, sizeof(one));
    } while (result < 0 && Socket::isInterrupted(Socket::getLastError()));
    if (result < 0 && !Socket::isWouldBlock(Socket::getLastError())) {
        LOG_FATAL("wakeup TCP send error: {}", Socket::errorString(Socket::getLastError()));
    }
}

void Wakeup::consume() const {
    char buffer[256];
    while (true) {
        const auto result = impl_->reader.receive(buffer, sizeof(buffer));
        if (result > 0) {
            continue;
        }
        if (result == 0) {
            LOG_FATAL("wakeup TCP connection closed");
        }
        const int error = Socket::getLastError();
        if (Socket::isInterrupted(error)) {
            continue;
        }
        if (!Socket::isWouldBlock(error)) {
            LOG_FATAL("wakeup TCP receive error: {}", Socket::errorString(error));
        }
        break;
    }
}
