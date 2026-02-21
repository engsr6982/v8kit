#pragma once
#include <functional>


namespace v8kit::binding::traits {


// Primary template: redirect to operator()
template <typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

// 普通函数 / 函数指针
template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using ReturnType                  = R;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = false; // always false
};

template <typename R, typename... Args>
struct FunctionTraits<R(Args...)> : FunctionTraits<R (*)(Args...)> {};

// std::function
template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> : FunctionTraits<R (*)(Args...)> {};


// 成员函数指针（包括 const / noexcept 等）
template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
    using ReturnType                  = R;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = false; // always false
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
    using ReturnType                  = R;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = true; // const modifier
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const noexcept> : FunctionTraits<R (C::*)(Args...) const> {};


template <typename T>
constexpr size_t ArgsCount_v = FunctionTraits<std::remove_cvref_t<T>>::ArgsCount;

template <typename T, size_t N>
using ArgumentType_t = std::tuple_element_t<N, typename FunctionTraits<std::remove_cvref_t<T>>::ArgsTuple>;


} // namespace v8kit::binding::traits