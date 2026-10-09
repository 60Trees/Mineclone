# Ole Triolib semantics

There are a fixed amount of base classes in my triolib. It, ideally, looks about like this:
```cpp
namespace triolib {  
struct BaseClass {
    virtual ~BaseClass() = default;
    virtual void init() = 0;
    virtual void loop() = 0;
    virtual void quit() = 0;
    // Gets overridden for a better name
    inline virtual std::string vname() const noexcept { return typeid(*this).name(); }
};
struct Application;
namespace detail {
void register_appmod(...);
...
template <typename T> struct Orderer {
    // virtual functions that takes a const T* other
    // and returns a bool, whether it wants to go before or after
    // (two functions). both false = ignore (default), both true = wtf error
    // also, if two are disagreeing then there is also a function that
    // returns a `double priority`. Higher priority wins (default = 0.0)
};
}
struct SubApp;
struct Module : BaseClass { tClass;
    Application* parent = nullptr;
    // Same with AppModules, has an order for `init` and `loop`. `quit` order is reverse `init`
};
struct SubApp { tClass;
    Application* parent = nullptr;
    std::vector<std::unique_ptr<Module>> mods;
    // Functions for Modules:
    // templated function to get any single mod (reference) by seeing if it can cast downwards
    // via dynamic_cast and throwing logic_error if not found. `getmod<...>()`
    // also, templated function to get a vector of unowning raw ptrs by seeing which mods can cast downwards
    // via dynamic_cast. `getmods<...>()`
    // Also has a function to create all registered mods and put them into `mods`. `load_all_mods()`
    //
};
namespace detail {std::map<std::string, std::unique_ptr<SubApp>(*)()> subapp_factory;}
struct AppModule { tClass;
    Application* parent = nullptr;
    // Has an order for `init` and `loop`. `quit` order is reverse `init`
};
struct Application : BaseClass { tClass;
    std::vector<std::unique_ptr<AppModule>> mods;
    // Functions for AppMods:
    // templated function to get any single mod (reference) by seeing if it can cast downwards
    // via dynamic_cast and throwing logic_error if not found. `getmod<...>()`
    // also, templated function to get a vector of unowning raw ptrs by seeing which mods can cast downwards
    // via dynamic_cast. `getmods<...>()`
    // Also has a function to create all registered mods and put them into `mods`. `load_all_mods()` 
    // Init, loop and quit default to creating + looping over mods to init, looping over mods to loop,
    // looping over mods to quit respectively. 
    std::map<std::string, std::unique_ptr<SubApp>> subapps;
    // Functions for subapps:
    // Nothing in init, loop or quit relating to subapps.
    // `createsubapp("...")` which is basically just { subapps[subappname] = triolib::detail::subapp_factory[subappname](); subapps[subappname]->parent = nullptr; }
    // `destroysubapp("...")` which is basically just { subapps.clear(subappname); }
};
}
#define REGISTER_APPMOD(classname) \
    ; run_before_main {
        trioilb::detail::register_appmod([]() -> std::unique_ptr<AppModule> { return std::make_unique<classname>(); });
    };
#define REGISTER_SUBAPP(classname) \
    ; run_before_main { \
        triolib::detail::subapp_factory[classname::sname()] = []() -> std::unique_ptr<SubApp> { return std::make_unique<classname>(); }; \
    };  
// ...
```
# Triolib semantics

Triolib has four managed object layers:

- `Application` owns application modules and loaded subapps.
- `AppModule` belongs to an `Application` and participates in application lifecycle dispatch.
- `SubApp` belongs to an `Application` and owns subapp modules.
- `Module` belongs to a `SubApp` and participates in subapp lifecycle dispatch.

Every managed object implements `init()`, `loop()`, and `quit()`. Initialization and loop order can be customized with the ordering virtuals on `AppModule` and `Module`; quit runs in reverse initialization order.

## Names

`tClass` defines names for the class where it is written:

- `Class::sname()` is the static abstraction name.
- `object.vname()` is the concrete runtime name.

Implementation classes use `tImplClass(parent)` when they should keep the parent abstraction's static name while retaining their own virtual name:

```cpp
struct World : triolib::SubApp {
    tClass;
};

struct WorldImpl : World {
    tImplClass(World);
};
```

In this example, `WorldImpl::sname()` is `World`, while `WorldImpl{}.vname()` identifies `WorldImpl`.

## Lookup

Use the canonical typed APIs:

- `getmod<T>()` returns a required module and throws if it is absent.
- `find_mod<T>()` returns a nullable module pointer.
- `get_mods<T>()` returns all matching application modules.
- `load_subapp(name)` and `load_subapp<T>()` create and initialize a registered subapp when needed.
- `get_subapp(name)` and `get_subapp<T>()` return an already-loaded subapp or throw `std::logic_error`.
- `unload_subapp(name)` and `unload_subapp<T>()` quit and remove a loaded subapp.

Modules are created lazily when their owner initializes or performs a lookup. Parent pointers are assigned before module initialization.

## Registration

Registration macros are used only at the end of definitions in `.cpp` files. They must never appear in headers or other files included by multiple translation units.

```cpp
struct WorldImpl : World {
    tImplClass(World);
    // lifecycle methods
} REGISTER_SUBAPP(WorldImpl);
```

Application modules use `REGISTER_MOD(ModuleName)`. Subapp modules use `REGISTER_MOD(ModuleName, ParentSubApp)`. The latter predicate limits the module to instances of its parent subapp.

The application itself is created through `REGISTER_APP(ApplicationType)` and `create_application()`.