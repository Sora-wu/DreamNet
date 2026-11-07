//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <DreamNet/socketTypes.h>

#include <memory>

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
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
