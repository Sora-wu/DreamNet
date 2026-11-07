//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <common/wakeup.h>
#include <common/log.hpp>

#include <cerrno>
#include <cstdint>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace Dream;
using namespace Dream::detail;

class Wakeup::Impl {
public:
    Impl() : fd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
        if (fd < 0) {
            LOG_FATAL("wakeup eventfd error: {}", std::strerror(errno));
        }
    }

    ~Impl() {
        ::close(fd);
    }

    int fd;
};

Wakeup::Wakeup() : impl_(std::make_unique<Impl>()) {}

Wakeup::~Wakeup() = default;

SocketFd Wakeup::getFd() const {
    return impl_->fd;
}

void Wakeup::notify() const {
    const uint64_t one = 1;
    ssize_t result;
    do {
        result = ::write(impl_->fd, &one, sizeof(one));
    } while (result < 0 && errno == EINTR);
    // 计数器已满时仍然可读，循环已有尚未消费的唤醒。
    if (result < 0 && errno != EAGAIN) {
        LOG_FATAL("wakeup eventfd write error: {}", std::strerror(errno));
    }
}

void Wakeup::consume() const {
    uint64_t count = 0;
    ssize_t result;
    do {
        result = ::read(impl_->fd, &count, sizeof(count));
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != EAGAIN) {
        LOG_FATAL("wakeup eventfd read error: {}", std::strerror(errno));
    }
}
