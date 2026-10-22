//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#ifdef PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <netinet/in.h>
#endif

namespace Dream {
#ifdef PLATFORM_WINDOWS
    using SocketFd = SOCKET;
    using SocketLength = int;
    inline constexpr SocketFd INVALID_SOCKET_FD = INVALID_SOCKET;
#else
    using SocketFd = int;
    using SocketLength = socklen_t;
    inline constexpr SocketFd INVALID_SOCKET_FD = -1;
#endif
}
