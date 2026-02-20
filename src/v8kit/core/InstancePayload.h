#pragma once
#include "Fwd.h"
#include "MetaInfo.h"
#include "NativeInstance.h"

#include <memory>


namespace v8kit {
struct ClassMeta;
}
namespace v8kit {


/**
 * 实例内部数据载体
 * @note 此结构作为 C++ 侧资源承载体，存储进 v8 InternalField 中
 */
struct InstancePayload final {
private:
    std::unique_ptr<NativeInstance> holder_;

    // internal use only
    ClassMeta const* define_{nullptr};
    Engine const*    engine_{nullptr};
    bool const       constructFromJs_{false};

    friend Engine; // for internal use only


public:
    [[nodiscard]] inline NativeInstance* getHolder() { return holder_.get(); }

    [[nodiscard]] inline ClassMeta const* getDefine() const { return define_; }

    [[nodiscard]] bool isConstructFromJs() const { return constructFromJs_; }

    /**
     * @brief 释放管理的资源
     * @note 适用于需要手动释放资源的场景，如数据库连接句柄等
     * @note 主动释放资源后，v8 GC 并不会感知改资源的内存释放，直到对应 JS 实例释放后，对应内存才会回收
     */
    inline void finalize() {
        if (holder_) {
            holder_.reset();
        }
    }

    template <typename T>
    inline T* unwrap() const {
        if (holder_) {
            return holder_->unwrap<T>();
        }
        return nullptr;
    }

    V8KIT_DISABLE_COPY(InstancePayload);

    explicit InstancePayload() = delete;

    explicit InstancePayload(std::unique_ptr<NativeInstance>&& holder) : holder_(std::move(holder)) {}

    explicit InstancePayload(
        std::unique_ptr<NativeInstance>&& holder,
        ClassMeta const*                  define,
        Engine const*                     engine,
        bool                              constructFromJs
    )
    : holder_(std::move(holder)),
      define_(define),
      engine_(engine),
      constructFromJs_(constructFromJs) {}

    ~InstancePayload() = default;

    template <typename... Args>
        requires std::constructible_from<InstancePayload, Args...>
    static inline std::unique_ptr<InstancePayload> make(Args&&... args) {
        return std::make_unique<InstancePayload>(std::forward<Args>(args)...);
    }
};


} // namespace v8kit