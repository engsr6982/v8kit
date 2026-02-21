#include "Engine.h"

#include "Exception.h"
#include "InstancePayload.h"
#include "MetaInfo.h"
#include "Reference.h"
#include "Value.h"
#include "ValueHelper.h"

#include <cassert>
#include <fstream>


V8KIT_WARNING_GUARD_BEGIN
#include "v8-external.h"
#include "v8-function-callback.h"
#include "v8-local-handle.h"
#include "v8-object.h"
#include "v8-primitive.h"
#include "v8-template.h"
#include <v8-context.h>
#include <v8-exception.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <v8-message.h>
#include <v8-persistent-handle.h>
#include <v8-script.h>
#include <v8-value.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {


Engine::Engine() {
    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

    isolate_ = v8::Isolate::New(params);

    v8::Locker         locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope    handle_scope(isolate_);
    context_.Reset(isolate_, v8::Context::New(isolate_));

    constructorSymbol_ = v8::Global<v8::Symbol>(isolate_, v8::Symbol::New(isolate_));
}
Engine::Engine(v8::Isolate* isolate, v8::Local<v8::Context> context)
: isolate_(isolate),
  context_(v8::Global<v8::Context>{isolate, context}),
  isExternalIsolate_(true) {
    constructorSymbol_ = v8::Global<v8::Symbol>(isolate_, v8::Symbol::New(isolate_));
}

Engine::~Engine() {
    if (isDestroying()) return;
    isDestroying_ = true;

    if (userData_) userData_.reset();

    {
        EngineScope scope(this);

        for (auto& [key, value] : managedResources_) {
            value.Reset();
            key->deleter(key->resource);
            delete key;
        }
        for (auto& [_, ctor] : classConstructors_) {
            ctor.Reset();
        }

        constructorSymbol_.Reset();
        classConstructors_.clear();
        registeredClasses_.clear();
        managedResources_.clear();
        context_.Reset();
    }

    if (!isExternalIsolate_) isolate_->Dispose();
}


v8::Isolate*           Engine::isolate() const { return isolate_; }
v8::Local<v8::Context> Engine::context() const { return context_.Get(isolate_); }

void Engine::setData(std::shared_ptr<void> data) { userData_ = std::move(data); }

bool Engine::isDestroying() const { return isDestroying_; }

Local<Value> Engine::eval(Local<String> const& code) { return eval(code, String::newString("<eval>")); }

Local<Value> Engine::eval(Local<String> const& code, Local<String> const& source) {
    v8::TryCatch try_catch(isolate_);

    auto v8Code   = ValueHelper::unwrap(code);
    auto v8Source = ValueHelper::unwrap(source);
    auto ctx      = context_.Get(isolate_);

    auto origin = v8::ScriptOrigin(v8Source);
    auto script = v8::Script::Compile(ctx, v8Code, &origin);
    Exception::rethrow(try_catch);

    auto result = script.ToLocalChecked()->Run(ctx);
    Exception::rethrow(try_catch);
    return ValueHelper::wrap<Value>(result.ToLocalChecked());
}

void Engine::loadFile(std::filesystem::path const& path) {
    if (isDestroying()) return;
    if (!std::filesystem::exists(path)) {
        throw Exception("File not found: " + path.string());
    }
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw Exception("Failed to open file: " + path.string());
    }
    std::string code((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    eval(String::newString(code), String::newString(path.string()));
}

void Engine::gc() const { isolate_->LowMemoryNotification(); }

Local<Object> Engine::globalThis() const { return ValueHelper::wrap<Object>(context_.Get(isolate_)->Global()); }

void Engine::addManagedResource(void* resource, v8::Local<v8::Value> value, std::function<void(void*)>&& deleter) {
    auto managed = std::make_unique<ManagedResource>(this, resource, std::move(deleter));

    v8::Global<v8::Value> weak{isolate_, value};
    weak.SetWeak(
        static_cast<void*>(managed.get()),
        [](v8::WeakCallbackInfo<void> const& data) {
            auto managed = static_cast<ManagedResource*>(data.GetParameter());
            auto runtime = managed->runtime;
            {
                v8::Locker locker(runtime->isolate_); // Since the v8 GC is not on the same thread, locking is required
                auto       iter = runtime->managedResources_.find(managed);
                assert(iter != runtime->managedResources_.end()); // ManagedResource should be in the map
                runtime->managedResources_.erase(iter);

                data.SetSecondPassCallback([](v8::WeakCallbackInfo<void> const& data) {
                    auto       managed = static_cast<ManagedResource*>(data.GetParameter());
                    v8::Locker locker(managed->runtime->isolate_);
                    delete managed;
                });
            }
        },
        v8::WeakCallbackType::kParameter
    );
    managedResources_.emplace(managed.release(), std::move(weak));
}
ClassMeta const* Engine::getClassMeta(std::type_index typeId) const {
    auto iter = typeMapping_.find(typeId);
    if (iter == typeMapping_.end()) return nullptr;
    return iter->second;
}

Local<Function> Engine::registerClass(ClassMeta const& meta) {
    if (registeredClasses_.contains(meta.name_)) {
        throw std::logic_error("Class already registered: " + meta.name_);
    }

    v8::TryCatch vtry(isolate_);

    v8::Local<v8::FunctionTemplate> ctor; // js: new T()

    if (meta.hasConstructor()) {
        ctor = newConstructor(meta);
    } else {
        ctor = v8::FunctionTemplate::New(isolate_, nullptr, {}, {}, 0, v8::ConstructorBehavior::kThrow);
        ctor->RemovePrototype();
    }

    auto scriptClassName = String::newString(meta.name_);
    ctor->SetClassName(ValueHelper::unwrap(scriptClassName));
    setToStringTag(ctor, meta.name_, meta.hasConstructor());

    buildStaticMembers(ctor, meta);
    buildInstanceMembers(ctor, meta);

    if (meta.base_ != nullptr) {
        if (!meta.base_->hasConstructor()) {
            throw Exception("Base class must have a constructor: " + meta.name_);
        }
        auto iter = classConstructors_.find(meta.base_);
        if (iter == classConstructors_.end()) {
            throw Exception("Base class not registered: " + meta.name_);
        }
        auto baseCtor = iter->second.Get(isolate_);
        ctor->Inherit(baseCtor);
    }

    auto function = ctor->GetFunction(context_.Get(isolate_));
    Exception::rethrow(vtry);

    registeredClasses_.emplace(meta.name_, &meta);
    classConstructors_.emplace(&meta, v8::Global<v8::FunctionTemplate>{isolate_, ctor});
    typeMapping_.emplace(meta.typeId_, &meta);

    auto myFunction = ValueHelper::wrap<Function>(function.ToLocalChecked());
    globalThis().set(scriptClassName, myFunction);
    return myFunction;
}

Local<Object> Engine::registerEnum(EnumMeta const& meta) {
    if (registeredEnums_.contains(meta.name_)) {
        throw std::logic_error("Enum already registered: " + meta.name_);
    }

    auto object = Object::newObject();
    for (auto const& [name, value] : meta.entries_) {
        object.set(String::newString(name), Number::newNumber(static_cast<double>(value)));
    }

    (void)object.defineOwnProperty(
        String::newString("$name"),
        String::newString(meta.name_),
        PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly
    );

    auto v8Object = ValueHelper::unwrap(object);
    setToStringTag(v8Object, meta.name_);

    registeredEnums_.emplace(meta.name_, &meta);

    globalThis().set(String::newString(meta.name_), object);
    return object;
}


bool Engine::isInstanceOf(Local<Object> const& obj, ClassMeta const& meta) const {
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        return false;
    }
    auto ctor = iter->second.Get(isolate_);
    return ctor->HasInstance(ValueHelper::unwrap(obj));
}

Local<Object> Engine::newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance) {
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        [[unlikely]] throw std::logic_error{
            "The native class " + meta.name_ + " is not registered, so an instance cannot be constructed."
        };
    }

    v8::TryCatch vtry{isolate_};

    auto ctx  = context_.Get(isolate_);
    auto ctor = iter->second.Get(isolate_)->GetFunction(ctx);
    Exception::rethrow(vtry);

    // (symbol, instance)
    auto args = std::vector<v8::Local<v8::Value>>{
        constructorSymbol_.Get(isolate_).As<v8::Value>(),
        v8::External::New(isolate_, instance.release()) // TODO: 潜在的内存泄漏点(当 Constructor 发生异常时)
    };
    auto val = ctor.ToLocalChecked()->NewInstance(ctx, static_cast<int>(args.size()), args.data());
    Exception::rethrow(vtry);

    return ValueHelper::wrap<Object>(val.ToLocalChecked());
}

InstancePayload* Engine::getInstancePayload(Local<Object> const& obj) const {
    auto v8This = ValueHelper::unwrap(obj);
    if (v8This->InternalFieldCount() < (int)InternalFieldSolt::Count) {
        return nullptr;
    }
    auto payload = v8This->GetAlignedPointerFromInternalField(static_cast<int>(InternalFieldSolt::InstancePayload));
    if (!payload) {
        return nullptr;
    }
    return static_cast<InstancePayload*>(payload);
}
bool Engine::trySetReferenceInternal(Local<Object> const& parentObj, Local<Object> const& subObj) {
    auto v8Parent = ValueHelper::unwrap(parentObj);
    auto v8Child  = ValueHelper::unwrap(subObj);
    if (v8Parent.IsEmpty() || v8Child.IsEmpty()) {
        return false;
    }

    constexpr int count = static_cast<int>(InternalFieldSolt::Count);
    if (v8Parent->InternalFieldCount() < count || v8Child->InternalFieldCount() < count) {
        return false; // 非法对象
    }
    v8Child->SetInternalField(static_cast<int>(InternalFieldSolt::ParentClassThisRef), v8Parent);
    return true;
}


void Engine::setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor) {
    auto symbol = v8::Symbol::GetToStringTag(isolate_);
    auto v8str =
        v8::String::NewFromUtf8(isolate_, name.data(), v8::NewStringType::kNormal, name.size()).ToLocalChecked();
    auto attr = static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum);

    if (hasConstructor) {
        obj->PrototypeTemplate()->Set(symbol, v8str, attr); // for log(new Foo)
    } else {
        obj->Set(symbol, v8str, attr); // for log(Bar)
    }
}

void Engine::setToStringTag(v8::Local<v8::Object>& obj, std::string_view name) {
    auto symbol = v8::Symbol::GetToStringTag(isolate_);
    auto v8str =
        v8::String::NewFromUtf8(isolate_, name.data(), v8::NewStringType::kNormal, name.size()).ToLocalChecked();
    auto attr = static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum);
    obj->DefineOwnProperty(context_.Get(isolate_), symbol, v8str, attr).Check();
}

constexpr int                   kCtorExternal_Payload = 0;
constexpr int                   kCtorExternal_Engine  = 1;
v8::Local<v8::FunctionTemplate> Engine::newConstructor(ClassMeta const& meta) {
    v8::TryCatch          vtry{isolate_};
    v8::Local<v8::Object> data = v8::Object::New(isolate_);
    Exception::rethrow(vtry);

    auto ctx = context_.Get(isolate_);

    (void)data->Set(ctx, kCtorExternal_Payload, v8::External::New(isolate_, const_cast<ClassMeta*>(&meta)));
    Exception::rethrow(vtry);

    (void)data->Set(ctx, kCtorExternal_Engine, v8::External::New(isolate_, this));
    Exception::rethrow(vtry);

    auto ctor = v8::FunctionTemplate::New(
        isolate_,
        [](v8::FunctionCallbackInfo<v8::Value> const& info) {
            auto ctx  = info.GetIsolate()->GetCurrentContext();
            auto data = info.Data().As<v8::Object>();

            auto meta = static_cast<ClassMeta*>(
                data->Get(ctx, kCtorExternal_Payload).ToLocalChecked().As<v8::External>()->Value()
            );
            auto runtime =
                static_cast<Engine*>(data->Get(ctx, kCtorExternal_Engine).ToLocalChecked().As<v8::External>()->Value());

            auto& ctor = meta->instanceMeta_.constructor_;

            try {
                if (!info.IsConstructCall()) {
                    throw Exception{"Native class constructor cannot be called as a function"};
                }

                std::unique_ptr<NativeInstance> instance        = nullptr;
                bool                            constructFromJs = true;
                if (info.Length() == 2 && info[0]->IsSymbol()
                    && info[0]->StrictEquals(runtime->constructorSymbol_.Get(runtime->isolate_))
                    && info[1]->IsExternal()) {
                    // constructor call from native code
                    auto inst       = info[1].As<v8::External>()->Value();
                    instance        = std::unique_ptr<NativeInstance>{static_cast<NativeInstance*>(inst)};
                    constructFromJs = false;
                } else {
                    // constructor call from JS code
                    instance = ctor(Arguments{runtime, info});
                }

                if (instance == nullptr) {
                    if (constructFromJs) {
                        throw Exception{"This native class cannot be constructed."};
                    } else {
                        throw Exception{"This native class cannot be constructed from native code."};
                    }
                }

                auto payload = new InstancePayload{std::move(instance), meta, runtime, constructFromJs};
                info.This()->SetAlignedPointerInInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload),
                    payload
                );

                if (constructFromJs) {
                    runtime->isolate_->AdjustAmountOfExternalAllocatedMemory(
                        static_cast<int64_t>(meta->instanceMeta_.classSize_)
                    );
                }

                runtime->addManagedResource(payload, info.This(), [](void* payload) {
                    auto typed = static_cast<InstancePayload*>(payload);
                    if (typed->constructFromJs_) {
                        typed->engine_->isolate_->AdjustAmountOfExternalAllocatedMemory(
                            -static_cast<int64_t>(typed->define_->instanceMeta_.classSize_)
                        );
                    }
                    delete typed;
                });
            } catch (Exception const& e) {
                e.rethrowToRuntime();
            }
        },
        data
    );
    ctor->InstanceTemplate()->SetInternalFieldCount(static_cast<int>(InternalFieldSolt::Count));
    return ctor;
}

void Engine::buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta) {
    auto const& staticMeta = meta.staticMeta_;

    for (auto& property : staticMeta.property_) {
        auto scriptPropertyName = String::newString(property.name_);

        auto v8Getter = [](v8::Local<v8::Name>, v8::PropertyCallbackInfo<v8::Value> const& info) {
            auto pbin = static_cast<StaticMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
            try {
                auto ret = pbin->getter_();
                info.GetReturnValue().Set(ValueHelper::unwrap(ret));
            } catch (Exception const& e) {
                e.rethrowToRuntime();
            }
        };

        v8::AccessorNameSetterCallback v8Setter = nullptr;
        if (property.setter_) {
            v8Setter = [](v8::Local<v8::Name>, v8::Local<v8::Value> value, v8::PropertyCallbackInfo<void> const& info) {
                auto pbin = static_cast<StaticMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
                try {
                    pbin->setter_(ValueHelper::wrap<Value>(value));
                } catch (Exception const& e) {
                    e.rethrowToRuntime();
                }
            };
        } else {
            v8Setter = [](v8::Local<v8::Name>, v8::Local<v8::Value>, v8::PropertyCallbackInfo<void> const&) {
                Exception("Cannot write to read-only native property", Exception::Type::TypeError).rethrowToRuntime();
            };
        }

        obj->SetNativeDataProperty(
            ValueHelper::unwrap(scriptPropertyName).As<v8::Name>(),
            std::move(v8Getter),
            std::move(v8Setter),
            v8::External::New(isolate_, const_cast<StaticMemberMeta::Property*>(&property)),
            PropertyAttribute::DontDelete
        );
    }

    for (auto& function : staticMeta.functions_) {
        auto scriptFunctionName = String::newString(function.name_);

        auto fn = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto fbin = static_cast<StaticMemberMeta::Function*>(info.Data().As<v8::External>()->Value());

                try {
                    auto ret = (fbin->callback_)(Arguments{EngineScope::currentEngine(), info});
                    info.GetReturnValue().Set(ValueHelper::unwrap(ret));
                } catch (Exception const& e) {
                    e.rethrowToRuntime();
                }
            },
            v8::External::New(isolate_, const_cast<StaticMemberMeta::Function*>(&function)),
            {},
            0,
            v8::ConstructorBehavior::kThrow
        );
        obj->Set(ValueHelper::unwrap(scriptFunctionName).As<v8::Name>(), fn, v8::PropertyAttribute::DontDelete);
    }
}
void Engine::buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta) {
    auto& instanceMeta = meta.instanceMeta_;

    auto prototype = obj->PrototypeTemplate();
    auto signature = v8::Signature::New(isolate_);

    // mount "$equals"
    prototype->Set(
        ValueHelper::unwrap(String::newString("$equals")),
        v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                info.GetReturnValue().SetFalse(); // TODO: impl equals
            },
            v8::External::New(isolate_, const_cast<InstanceMemberMeta*>(&instanceMeta)),
            signature
        ),
        static_cast<PropertyAttribute>(v8::PropertyAttribute::DontDelete | v8::PropertyAttribute::DontEnum)
    );

    for (auto& method : instanceMeta.methods_) {
        auto scriptMethodName = String::newString(method.name_);

        auto fn = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto method  = static_cast<InstanceMemberMeta::Method*>(info.Data().As<v8::External>()->Value());
                auto payload = info.This()->GetAlignedPointerFromInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload)
                );

                auto typed  = static_cast<InstancePayload*>(payload);
                auto engine = const_cast<Engine*>(typed->engine_);
                try {
                    auto val = (method->callback_)(*typed, Arguments{engine, info});
                    info.GetReturnValue().Set(ValueHelper::unwrap(val));
                } catch (Exception const& e) {
                    e.rethrowToRuntime();
                }
            },
            v8::External::New(isolate_, const_cast<InstanceMemberMeta::Method*>(&method)),
            signature
        );
        prototype->Set(ValueHelper::unwrap(scriptMethodName), fn, v8::PropertyAttribute::DontDelete);
    }

    for (auto& prop : instanceMeta.property_) {
        auto scriptPropertyName = String::newString(prop.name_);
        auto data               = v8::External::New(isolate_, const_cast<InstanceMemberMeta::Property*>(&prop));
        v8::Local<v8::FunctionTemplate> v8Getter;
        v8::Local<v8::FunctionTemplate> v8Setter;

        v8Getter = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto prop    = static_cast<InstanceMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
                auto wrapped = info.This()->GetAlignedPointerFromInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload)
                );

                auto typed  = static_cast<InstancePayload*>(wrapped);
                auto engine = const_cast<Engine*>(typed->engine_);
                try {
                    auto val = (prop->getter_)(*typed, Arguments{engine, info});
                    info.GetReturnValue().Set(ValueHelper::unwrap(val));
                } catch (Exception const& e) {
                    e.rethrowToRuntime();
                }
            },
            data,
            signature
        );

        if (prop.setter_) {
            v8Setter = v8::FunctionTemplate::New(
                isolate_,
                [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                    auto prop    = static_cast<InstanceMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
                    auto wrapped = info.This()->GetAlignedPointerFromInternalField(
                        static_cast<int>(InternalFieldSolt::InstancePayload)
                    );

                    auto typed  = static_cast<InstancePayload*>(wrapped);
                    auto engine = const_cast<Engine*>(typed->engine_);
                    try {
                        (prop->setter_)(*typed, Arguments{engine, info});
                    } catch (Exception const& e) {
                        e.rethrowToRuntime();
                    }
                },
                data,
                signature
            );
        }

        prototype->SetAccessorProperty(
            ValueHelper::unwrap(scriptPropertyName).As<v8::Name>(),
            v8Getter,
            v8Setter,
            v8::PropertyAttribute::DontDelete
        );
    }
}


} // namespace v8kit