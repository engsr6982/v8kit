#pragma once
#include <cstdint>


namespace v8kit {

enum class ReturnValuePolicy : uint8_t {
    /**
     * @brief 自动推导 (默认)
     * - 指针 (T*) -> Reference (JS 引用 C++ 指针，不管理生命周期)
     * - 左值引用 (T&) -> Copy (JS 拷贝一份新的 C++ 对象)
     * - 右值 (T&&) -> Move (JS 接管 C++ 对象)
     */
    kAutomatic = 0,

    /**
     * @brief 强制拷贝
     * - 无论是指针还是引用，都在 JS 侧 new 一个新对象并拷贝数据。
     */
    kCopy = 1,

    /**
     * @brief 强制移动
     * - 移动构造新对象，原 C++ 对象失效。
     */
    kMove = 2,

    /**
     * @brief 引用 (危险视图)
     * - JS 仅持有 C++ 指针。JS 不负责销毁，且不保活父对象。
     * - 仅当你确定 C++ 对象是全局单例或生命周期长于 JS 环境时使用。
     */
    kReference = 3,

    /**
     * @brief 接管所有权
     * - C++ 返回一个指针，JS 接管它，JS 对象销毁时 delete C++ 指针。
     */
    kTakeOwnership = 4,

    /**
     * @brief 内部引用 (保活父对象)
     * - JS 持有 C++ 指针 (View)。
     * - 同时，JS 子对象会强引用 JS 父对象 (this)。
     * - 防止父对象被 GC 导致子对象悬空。
     */
    kReferenceInternal = 5,
};

}
