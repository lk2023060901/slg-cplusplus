#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SLG_CITUS_SHARED)
    #if defined(SLG_CITUS_BUILD_SHARED)
      #define SLG_CITUS_API __declspec(dllexport)
    #else
      #define SLG_CITUS_API __declspec(dllimport)
    #endif
  #else
    #define SLG_CITUS_API
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SLG_CITUS_API __attribute__((visibility("default")))
#else
  #define SLG_CITUS_API
#endif
