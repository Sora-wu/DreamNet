//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/socket.h>

namespace Dream::detail {
    class Wakeup {
    public:
        Wakeup();
        ~Wakeup();
        Wakeup(const Wakeup&) = delete;
        Wakeup& operator=(const Wakeup&) = delete;

        [[nodiscard]] SocketFd getFd() const;
        void notify() const;
        void consume() const;

    private:
#ifdef PLATFORM_WINDOWS
        Socket reader_{SOCK_DGRAM};
        Socket writer_{SOCK_DGRAM};
#else
        int fd_ = -1;
#endif
    };
}
