#pragma once
#include <cstdint>

#include <type_traits>

namespace v8kit {

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
     * 引用，父对象就不会被垃圾回收。这是通过 property 等创建的属性获取器（property getter）的默认策略。
     */
    kReferenceInternal = 5,
};


template <typename T>
struct is_policy : std::is_same<std::decay_t<T>, ReturnValuePolicy> {};


} // namespace v8kit
