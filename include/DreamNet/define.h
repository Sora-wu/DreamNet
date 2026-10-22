//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#ifdef PLATFORM_WINDOWS
    #ifdef DreamNet_EXPORTS
        #define DREAM_NET_API __declspec(dllexport)
    #else
        #define DREAM_NET_API __declspec(dllimport)
    #endif
#else
    #define DREAM_NET_API
#endif
