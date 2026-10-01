#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
using uint = unsigned int;  // emscripten doesn't have `uint`
#endif

extern bool before_main;
std::string_view load_asset(const std::string& path);

enum BuildType : unsigned char {
    Debug = BUILD_TYPE_DEBUG,
    RelWithDebInfo = BUILD_TYPE_RELWITHDEBINFO,
    MinSizeRel = BUILD_TYPE_MINSIZEREL,
    Release = BUILD_TYPE_RELEASE,
};
constexpr static BuildType build_type = static_cast<BuildType>(BUILD_TYPE);

namespace triolib {
    struct BaseClass {
        virtual ~BaseClass() = default;
        virtual void init() = 0;
        virtual void quit() = 0;
    };

    struct SubApp;
    struct Application;

    struct Module : BaseClass {
        SubApp* parent = nullptr;
        virtual void loop() {}
    };

    struct AppModule : BaseClass {
        Application* parent = nullptr;
        virtual void loop() {}

        virtual bool does_go_before_init(const AppModule&) const {
            return false;
        }
        virtual bool does_go_after_init(const AppModule&) const {
            return false;
        }
        virtual float get_priority_init() const { return 0.0f; }
        virtual bool does_go_before_loop(const AppModule&) const {
            return false;
        }
        virtual bool does_go_after_loop(const AppModule&) const {
            return false;
        }
        virtual float get_priority_loop() const { return 0.0f; }
    };

    using AppModFactory = std::unique_ptr<AppModule> (*)();
    using ModFactory = std::unique_ptr<Module> (*)();
    using ModPredicate = bool (*)(SubApp*);
    using SubAppFactory = std::unique_ptr<SubApp> (*)();
    using ApplicationFactory = std::unique_ptr<Application> (*)();

    void register_application(ApplicationFactory);
    void register_appmod(AppModFactory);
    void register_subapp(SubAppFactory);
    void register_mod(ModFactory, ModPredicate);
    std::unique_ptr<Application> create_application();

    template <typename T>
        requires std::is_base_of_v<Application, T>
    void register_application() {
        register_application([]() -> std::unique_ptr<Application> {
            return std::make_unique<T>();
        });
    }

    template <typename T>
        requires std::is_base_of_v<AppModule, T>
    void register_appmod() {
        register_appmod([]() -> std::unique_ptr<AppModule> {
            return std::make_unique<T>();
        });
    }

    template <typename T>
        requires std::is_base_of_v<SubApp, T>
    void register_subapp() {
        register_subapp(
            []() -> std::unique_ptr<SubApp> { return std::make_unique<T>(); });
    }

    template <typename T, typename Parent>
        requires std::is_base_of_v<Module, T> &&
                 std::is_base_of_v<SubApp, Parent>
    void register_mod() {
        register_mod(
            []() -> std::unique_ptr<Module> { return std::make_unique<T>(); },
            [](SubApp* candidate) {
                return dynamic_cast<Parent*>(candidate) != nullptr;
            });
    }
    struct SubApp : BaseClass {
        Application* application = nullptr;
        std::vector<std::unique_ptr<Module>> mods;
        bool added_all_mods = false;

        void init() override {
            create_all_mods();
            for (auto& mod : mods) mod->init();
        }
        virtual void loop() {
            for (auto& mod : mods) mod->loop();
        }
        void quit() override {
            for (auto it = mods.rbegin(); it != mods.rend(); ++it)
                (*it)->quit();
        }

        void create_all_mods();
        void add_all_mods() { create_all_mods(); }

        template <typename T>
        bool hasmod() {
            return find_mod<T>() != nullptr;
        }
        template <typename T>
        T& getmod() {
            if (auto* result = find_mod<T>()) return *result;
            throw std::logic_error("SubApp module does not exist: " +
                                   std::string(typeid(T).name()));
        }
        template <typename T>
        T* find_mod() {
            static_assert(
                std::is_base_of_v<Module, T>, "T must derive from Module");
            create_all_mods();
            for (auto& mod : mods)
                if (auto* result = dynamic_cast<T*>(mod.get())) return result;
            return nullptr;
        }
        template <typename T>
        T& get() {
            return getmod<T>();
        }
    };

    struct Application : BaseClass {
        std::string name;
        bool done = false;
        std::vector<std::unique_ptr<AppModule>> mods;
        std::vector<std::unique_ptr<SubApp>> subapps;
        bool added_all_mods = false;
        bool added_all_subapps = false;
        std::vector<AppModule*> loop_order;

        void init() override;
        void loop();
        void quit() override;

        void create_all_mods();
        void add_all_mods() { create_all_mods(); }
        void subapps_add_all();
        void add_all_subapps() { subapps_add_all(); }
        void clear_subapps() {
            subapps.clear();
            added_all_subapps = false;
        }

        template <typename T>
        bool hasmod() {
            return find_mod<T>() != nullptr;
        }
        template <typename T>
        T& getmod() {
            if (auto* result = find_mod<T>()) return *result;
            throw std::logic_error("Application module does not exist: " +
                                   std::string(typeid(T).name()));
        }
        template <typename T>
        T* find_mod() {
            static_assert(std::is_base_of_v<AppModule, T>,
                "T must derive from AppModule");
            create_all_mods();
            for (auto& mod : mods)
                if (auto* result = dynamic_cast<T*>(mod.get())) return result;
            return nullptr;
        }
        template <typename T>
        bool hassubapp() {
            return find_subapp<T>() != nullptr;
        }
        template <typename T>
        bool has_subapp() {
            return hassubapp<T>();
        }
        template <typename T>
        T& getsubapp() {
            if (auto* result = find_subapp<T>()) return *result;
            throw std::logic_error("Application subapp does not exist: " +
                                   std::string(typeid(T).name()));
        }
        template <typename T>
        T& get_subapp() {
            return getsubapp<T>();
        }
        template <typename T>
        T* find_subapp() {
            static_assert(
                std::is_base_of_v<SubApp, T>, "T must derive from SubApp");
            subapps_add_all();
            for (auto& subapp : subapps)
                if (auto* result = dynamic_cast<T*>(subapp.get()))
                    return result;
            return nullptr;
        }
        template <typename T>
        T& create_get_subapp() {
            if (auto* result = find_subapp<T>()) return *result;
            static_assert(!std::is_abstract_v<T>, "T must not be abstract");
            auto result = std::make_unique<T>();
            auto& reference = *result;
            result->application = this;
            subapps.push_back(std::move(result));
            return reference;
        }

        template <typename T>
        T& get(bool exact = false) {
            if (exact) {
                create_all_mods();
                for (auto& mod : mods)
                    if (typeid(*mod) == typeid(T))
                        return *static_cast<T*>(mod.get());
                throw std::logic_error("Application module does not exist: " +
                                       std::string(typeid(T).name()));
            }
            return getmod<T>();
        }
        template <typename T>
        std::vector<T*> get_mods() {
            create_all_mods();
            std::vector<T*> result;
            for (auto& mod : mods)
                if (auto* value = dynamic_cast<T*>(mod.get()))
                    result.push_back(value);
            return result;
        }
        template <typename T>
        T& operator()() {
            return getmod<T>();
        }
    };

    namespace detail {
        struct RunBeforeMain {
            RunBeforeMain(void (*func)()) { func(); }
        };
        struct FuncAdd {
            friend void (*operator+(FuncAdd, void (*func)()))() { return func; }
        };
    }  // namespace detail
}  // namespace triolib

#define __concat(x, y) x##y
#define _concat(x, y) __concat(x, y)
#define run_before_main                              \
    static ::triolib::detail::RunBeforeMain _concat( \
        __RUN_BEFORE_MAIN, __COUNTER__) = ::triolib::detail::FuncAdd{} + []
#define REGISTER_APP(classname) \
    ;                           \
    run_before_main { ::triolib::register_application<classname>(); }
#define REGISTER_APPMOD(classname) \
    ;                              \
    run_before_main { ::triolib::register_appmod<classname>(); }
#define REGISTER_SUBAPP(classname) \
    ;                              \
    run_before_main { ::triolib::register_subapp<classname>(); }
#define TRIOLIB_REGISTER_MOD_1(classname) REGISTER_APPMOD(classname)
#define TRIOLIB_REGISTER_MOD_2(classname, parent) \
    ;                                             \
    run_before_main { ::triolib::register_mod<classname, parent>(); }
#define TRIOLIB_REGISTER_MOD_SELECT(_1, _2, NAME, ...) NAME
#define REGISTER_MOD(...)                                            \
    TRIOLIB_REGISTER_MOD_SELECT(__VA_ARGS__, TRIOLIB_REGISTER_MOD_2, \
        TRIOLIB_REGISTER_MOD_1)(__VA_ARGS__)

#define tassertmsg(to_check, msg)                                          \
    do {                                                                   \
        if (!(to_check))                                                   \
            throw std::runtime_error(                                      \
                "Assert `" #to_check "` has failed: " + std::string(msg)); \
    } while (0)
#define tassert(to_check)                                                   \
    do {                                                                    \
        if (!(to_check))                                                    \
            throw std::runtime_error("Assert `" #to_check "` has failed."); \
    } while (0)
