#pragma once
#include "Fwd.h"
#include "v8kit/Macro.h"

#include <filesystem>
#include <typeindex>

namespace v8kit {

struct ClassMeta; // forward declaration
struct EnumMeta;
namespace internal {
class V8EscapeScope;
}

class Engine {
public:
    V8KIT_DISABLE_COPY(Engine);

    Engine(Engine&&) noexcept            = default;
    Engine& operator=(Engine&&) noexcept = default;

    ~Engine();

    explicit Engine();

    explicit Engine(v8::Isolate* isolate, v8::Local<v8::Context> context);

    [[nodiscard]] v8::Isolate* isolate() const;

    [[nodiscard]] v8::Local<v8::Context> context() const;

    void setData(std::shared_ptr<void> data);

    template <typename T>
    [[nodiscard]] inline std::shared_ptr<T> getData() const {
        return std::static_pointer_cast<T>(userData_);
    }

    [[nodiscard]] bool isDestroying() const;

    Local<Value> eval(Local<String> const& code);

    Local<Value> eval(Local<String> const& code, Local<String> const& source);

    void loadFile(std::filesystem::path const& path);

    void gc() const;

    [[nodiscard]] Local<Object> globalThis() const;

    /**
     * Add a managed resource to the runtime.
     * The managed resource will be destroyed when the runtime is destroyed.
     * @param resource Resources that need to be managed
     * @param value The v8 object associated with this resource.
     * @param deleter The deleter function to be called when the resource is destroyed.
     */
    void addManagedResource(void* resource, v8::Local<v8::Value> value, std::function<void(void*)>&& deleter);

    /**
     * Register a binding class and mount it to globalThis
     */
    Local<Function> registerClass(ClassMeta const& meta);

    Local<Object> registerEnum(EnumMeta const& meta);

    [[nodiscard]] ClassMeta const* getClassDefine(std::type_index typeId) const;

    Local<Object> newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance);

    [[nodiscard]] bool isInstanceOf(Local<Object> const& obj, ClassMeta const& meta) const;

    // [[nodiscard]] internal::InstancePayload*
    // getInternalInstancePayload(Local<Object> const& obj, ClassMeta const& def) const;

    // [[nodiscard]] internal::SmartHolder*
    // getInternalSmartHolder(Local<Object> const& obj, ClassMeta const& def) const;

private:
    void setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor);
    void setToStringTag(v8::Local<v8::Object>& obj, std::string_view name);

    v8::Local<v8::FunctionTemplate> newConstructor(ClassMeta const& meta);

    void buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);
    void buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);

    friend EngineScope;
    friend ExitEngineScope;
    friend internal::V8EscapeScope;

    template <typename>
    friend class Global;
    template <typename>
    friend class Weak;

    struct ManagedResource {
        Engine*                    runtime;
        void*                      resource;
        std::function<void(void*)> deleter;
    };

    // v8: AlignedPointerInInternalField
    static constexpr int kInternalFieldCount            = 1;
    static constexpr int kInternalField_InstancePayload = 0;

    v8::Isolate*            isolate_{nullptr};
    v8::Global<v8::Context> context_{};
    std::shared_ptr<void>   userData_{nullptr};

    bool       isDestroying_{false};
    bool const isExternalIsolate_{false};

    // This symbol is used to mark the construction of objects from C++ (with special logic).
    v8::Global<v8::Symbol> constructorSymbol_{};

    std::unordered_map<ManagedResource*, v8::Global<v8::Value>>            managedResources_;
    std::unordered_map<std::string, ClassMeta const*>                      registeredClasses_;
    std::unordered_map<ClassMeta const*, v8::Global<v8::FunctionTemplate>> classConstructors_;

    std::unordered_map<std::type_index, ClassMeta const*> typeMapping_;

    std::unordered_map<std::string, EnumMeta const*> registeredEnums_;
};


} // namespace v8kit