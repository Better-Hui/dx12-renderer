#pragma once

//Modify Begin:2026-07-27 by BestHui

#define FRAMEWORK_DEPRECATED(Message) [[deprecated(Message)]]

#if defined(_MSC_VER)
#define FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_BEGIN __pragma(warning(push)) __pragma(warning(disable: 4996))
#define FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_END __pragma(warning(pop))
#else
#define FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_BEGIN
#define FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_END
#endif

//Modify End
