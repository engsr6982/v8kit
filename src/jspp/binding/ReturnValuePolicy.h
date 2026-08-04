#pragma once
#include <cstdint>

#include <type_traits>

namespace jspp::binding {

enum class ReturnValuePolicy : uint8_t {
    /**
     * 当返回值为指针时，回退到 ReturnValuePolicy::kTakeOwnership；
     * 对于右值引用和左值引用，则分别使用 ReturnValuePolicy::kMove 和 ReturnValuePolicy::kCopy。
     * 各策略的具体行为见下文说明。这是默认策略。
     */
    kAutomatic = 0,

    /**
     * @brief 创建返回对象的新副本，该副本归 Js 所有。
     * 此策略相对安全，因为两个实例的生命周期相互解耦。
     */
    kCopy = 1,

    /**
     * @brief 使用 `std::move` 将返回值的内容移动到新实例中，新实例归 JS 所有。
     * 此策略相对安全，因为源实例（被移动方）和目标实例（接收方）的生命周期相互解耦。
     */
    kMove = 2,

    /**
     * @brief 引用现有对象，但不取得其所有权。对象的生命周期管理及不再使用时的内存释放由 C++ 侧负责。
     * @note 警告：若 C++ 侧销毁了仍被 JS 引用和使用的对象，将导致未定义行为。
     * @note 当存在 TransientObjectScope 时，此策略创建的资源会按"溯源规则"被跟踪：
     *       仅当包装是瞬态根（parent 为空，如回调参数）或从瞬态根派生（parent 已在作用域跟踪集合中）
     *       时，才会在 TransientObjectScope 退出时被 invalidate（仅使 JS 包装失效，不销毁 C++ 对象）。
     *       回调内访问长期对象成员所创建的包装不会被跟踪。
     */
    kReference = 3,

    /**
     * @brief 引用现有对象（即不创建新副本）并取得其所有权。
     * 当对象的引用计数归零时，Js 会调用析构函数和 delete 运算符。
     * 若 C++ 侧也执行同样的销毁操作，或数据并非动态分配，将导致未定义行为。
     */
    kTakeOwnership = 4,

    /**
     * 若返回值是左值引用或指针，父对象（被调用方法 / 属性的 this 参数）会至少保持存活至返回值的生命周期结束
     * 否则此策略会回退到 ReturnValuePolicy::kMove。
     * 其内部实现与 ReturnValuePolicy::kReference 一致，但额外添加了 Global<T>，确保只要返回值还被 JS
     * 引用，父对象就不会被垃圾回收。
     * @note 与 kReference 一样受 TransientObjectScope 的溯源式跟踪约束（见 kReference 注释）。
     */
    kReferenceInternal = 5,

    /**
     * 此策略和 kReference 大致相同，唯一的不同是此策略创建的资源不受 TransientObjectScope 的影响。
     * 若对象明确长期有效（如全局单例、常驻容器的成员），请显式使用此策略，以避免在回调内
     * 被瞬态作用域误伤（即使其 parent 恰为瞬态根，例如 `evt.getGlobalThing()`）。
     */
    kReferencePersistent = 6,

    /**
     * 此策略和 kReferenceInternal 大致相同，唯一的不同是此策略创建的资源不受 TransientObjectScope 的影响。
     */
    kReferenceInternalPersistent = 7,
};


namespace traits {

template <typename T>
struct is_policy : std::is_same<std::decay_t<T>, ReturnValuePolicy> {};

} // namespace traits

namespace detail {

template <typename T>
ReturnValuePolicy resolveAutomaticPolicy(ReturnValuePolicy policy) {
    if (policy == ReturnValuePolicy::kAutomatic) {
        if constexpr (std::is_pointer_v<T>) {
            return ReturnValuePolicy::kTakeOwnership;
        } else if constexpr (std::is_lvalue_reference_v<T>) {
            return ReturnValuePolicy::kCopy;
        } else if constexpr (std::is_rvalue_reference_v<T>) {
            return ReturnValuePolicy::kMove;
        }
    }
    return policy;
}

} // namespace detail

} // namespace jspp::binding
