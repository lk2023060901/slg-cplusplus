#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SLG_COROUTINE_SHARED)
    #if defined(SLG_COROUTINE_BUILD_SHARED)
      #define SLG_COROUTINE_API __declspec(dllexport)
    #else
      #define SLG_COROUTINE_API __declspec(dllimport)
    #endif
  #else
    #define SLG_COROUTINE_API
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SLG_COROUTINE_API __attribute__((visibility("default")))
#else
  #define SLG_COROUTINE_API
#endif
