#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SLG_NAVIGATION_SHARED)
    #if defined(SLG_NAVIGATION_BUILD_SHARED)
      #define SLG_NAVIGATION_API __declspec(dllexport)
    #else
      #define SLG_NAVIGATION_API __declspec(dllimport)
    #endif
  #else
    #define SLG_NAVIGATION_API
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SLG_NAVIGATION_API __attribute__((visibility("default")))
#else
  #define SLG_NAVIGATION_API
#endif
