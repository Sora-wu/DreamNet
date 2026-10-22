//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/poller.h>
#ifdef PLATFORM_WINDOWS
    #include <tcp/wsaPoller.h>
#else
    #include <tcp/epollPoller.h>
#endif

namespace Dream::detail {

Poller* Poller::getDefaultPoller() {
#ifdef PLATFORM_WINDOWS
    return new WsaPoller;
#else
    return new EpollPoller;
#endif
}

}  // namespace Dream::detail
