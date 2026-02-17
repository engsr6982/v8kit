#pragma once

#define V8KIT_DISABLE_COPY(T)                                                                                          \
    T(const T&)            = delete;                                                                                   \
    T& operator=(const T&) = delete;

#define V8KIT_DISABLE_MOVE(T)                                                                                          \
    T(T&&)            = delete;                                                                                        \
    T& operator=(T&&) = delete;

#define V8KIT_DISABLE_COPY_MOVE(T)                                                                                     \
    V8KIT_DISABLE_COPY(T);                                                                                             \
    V8KIT_DISABLE_MOVE(T);

#define V8KIT_DISABLE_NEW()                                                                                            \
    static void* operator new(std::size_t)                          = delete;                                          \
    static void* operator new(std::size_t, const std::nothrow_t&)   = delete;                                          \
    static void* operator new[](std::size_t)                        = delete;                                          \
    static void* operator new[](std::size_t, const std::nothrow_t&) = delete;

#if defined(_MSC_VER)
#define V8KIT_WARNING_GUARD_BEGIN                                                                                      \
    __pragma(warning(push)) __pragma(warning(disable : 4100)) // unreferenced formal parameter
#define V8KIT_WARNING_GUARD_END __pragma(warning(pop))
#else
#define V8KIT_WARNING_GUARD_BEGIN
#define V8KIT_WARNING_GUARD_END
#endif
