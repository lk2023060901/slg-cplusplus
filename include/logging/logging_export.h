#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SLG_LOGGING_SHARED)
    #if defined(SLG_LOGGING_BUILD_SHARED)
      #define SLG_LOGGING_API __declspec(dllexport)
    #else
      #define SLG_LOGGING_API __declspec(dllimport)
    #endif
  #else
    #define SLG_LOGGING_API
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define SLG_LOGGING_API __attribute__((visibility("default")))
#else
  #define SLG_LOGGING_API
#endif
