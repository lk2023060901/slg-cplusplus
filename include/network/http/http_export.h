#pragma once

#if defined(_WIN32)
    #if defined(SLG_HTTP_BUILD_SHARED)
        #define SLG_HTTP_API __declspec(dllexport)
    #elif defined(SLG_HTTP_SHARED)
        #define SLG_HTTP_API __declspec(dllimport)
    #else
        #define SLG_HTTP_API
    #endif
#else
    #define SLG_HTTP_API
#endif

