//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/wakeup.h>
#include <common/define.h>

#ifdef PLATFORM_LINUX
    #include <sys/eventfd.h>
    #include <unistd.h>
#endif

using namespace Dream;
using namespace Dream::detail;

Wakeup::Wakeup() {
#ifdef PLATFORM_WINDOWS
    reader_.bind(Address(0));
    writer_.bind(Address(0));

    Address readerAddress;
    Address writerAddress;
    if (!Socket::getLocalAddress(reader_.getFd(), readerAddress) ||
        !Socket::getLocalAddress(writer_.getFd(), writerAddress) ||
        reader_.connect(writerAddress) != 0 || writer_.connect(readerAddress) != 0) {
        LOG_FATAL("wakeup socket error: {}", Socket::errorString(Socket::getLastError()));
    }
#else
    fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd_ < 0) {
        LOG_FATAL("eventfd error: {}", std::strerror(errno));
    }
#endif
}

Wakeup::~Wakeup() {
#ifdef PLATFORM_LINUX
    ::close(fd_);
#endif
}

SocketFd Wakeup::getFd() const {
#ifdef PLATFORM_WINDOWS
    return reader_.getFd();
#else
    return fd_;
#endif
}

void Wakeup::notify() const {
#ifdef PLATFORM_WINDOWS
    const char one = 1;
    std::ptrdiff_t result;
    do {
        result = writer_.send(&one, sizeof(one));
    } while (result < 0 && Socket::isInterrupted(Socket::getLastError()));
    if (result < 0 && !Socket::isWouldBlock(Socket::getLastError())) {
        LOG_ERROR("wakeup send error: {}", Socket::errorString(Socket::getLastError()));
    }
#else
    const uint64_t one = 1;
    ssize_t result;
    do {
        result = ::write(fd_, &one, sizeof(one));
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != EAGAIN) {
        LOG_ERROR("wakeup write error: {}", std::strerror(errno));
    }
#endif
}

void Wakeup::consume() const {
#ifdef PLATFORM_WINDOWS
    char buffer[256];
    while (true) {
        if (reader_.receive(buffer, sizeof(buffer)) >= 0) {
            continue;
        }
        const int error = Socket::getLastError();
        if (Socket::isInterrupted(error)) {
            continue;
        }
        if (!Socket::isWouldBlock(error)) {
            LOG_ERROR("wakeup receive error: {}", Socket::errorString(error));
        }
        break;
    }
#else
    uint64_t count = 0;
    ssize_t result;
    do {
        result = ::read(fd_, &count, sizeof(count));
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != EAGAIN) {
        LOG_ERROR("wakeup read error: {}", std::strerror(errno));
    }
#endif
}
