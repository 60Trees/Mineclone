#pragma once

#include <map>
#include <chrono>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
#include <logger.hpp>

#include <string>
#include <typeinfo>
#include <cstdlib>
#include <nlohmann/json.hpp>

#if defined(__GNUC__) || defined(__clang__)
#    include <cxxabi.h>
#endif

template <typename T>
inline std::string get_dynamic_name(const T& e) {
    const char* name = typeid(e).name();

#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);

    if (status == 0 && demangled) {
        std::string result(demangled);
        std::free(demangled);
        return result;
    }
#endif

    return name;
}

#ifdef __EMSCRIPTEN__
using uint = unsigned int;  // emscripten doesn't have `uint`
#endif

extern bool before_main;
extern std::vector<std::string> args;
std::string_view load_asset(const std::string& path);

enum BuildType : unsigned char {
    Debug = BUILD_TYPE_DEBUG,
    RelWithDebInfo = BUILD_TYPE_RELWITHDEBINFO,
    MinSizeRel = BUILD_TYPE_MINSIZEREL,
    Release = BUILD_TYPE_RELEASE,
};
constexpr static BuildType build_type = static_cast<BuildType>(BUILD_TYPE);

std::string prettify_json(
    const nlohmann::json& j, int indent = 4, int line_limit = -1, int chars_for_single_line_object = 30);

#define debug_only if constexpr (::build_type == ::Debug)

#define __tClass                                                 \
    /**                                                          \
     * Static name                                               \
     */                                                          \
    inline static std::string sname() noexcept {                 \
        static std::string s = __PRETTY_FUNCTION__;              \
        constexpr size_t front = 19, back = 9;                   \
        return s.substr(front, s.size() - front - back);         \
    }                                                            \
    /**                                                          \
     * Virtual name                                              \
     */                                                          \
    inline virtual std::string vname() const noexcept override { \
        static std::string s = __PRETTY_FUNCTION__;              \
        constexpr size_t front = 20, back = 15;                  \
        return s.substr(20, s.size() - front - back);            \
    }

#define __tImplClass(parent) \
    inline static std::string sname() noexcept { return parent::sname(); }

#define tClass \
    public:    \
    __tClass

#define tImplClass(parent) __tImplClass(parent) public:

namespace triolib {
    struct BaseClass {
        virtual ~BaseClass() = default;
        virtual void init() = 0;
        virtual void loop() = 0;
        virtual void quit() = 0;
        /**
         * Virtual name
         */
        inline virtual std::string vname() const noexcept { return get_dynamic_name<BaseClass>(*this); }
    };

    struct SubApp;
    struct Application;

    template <typename T>
    struct Ordered {
        virtual bool order_init_does_go_before(const T* other) const { return false; }
        virtual bool order_init_does_go_after(const T* other) const { return false; }
        virtual bool order_loop_does_go_before(const T* other) const { return false; }
        virtual bool order_loop_does_go_after(const T* other) const { return false; }
        virtual double init_order_priority() const { return 0.0; }
        virtual double loop_order_priority() const { return 0.0; }
    };

    struct Module : BaseClass, Ordered<Module> {
        tClass;
        SubApp* parent = nullptr;
    };

    struct AppModule : BaseClass, Ordered<AppModule> {
        tClass;
        Application* parent = nullptr;
    };

    using AppModFactory = std::unique_ptr<AppModule> (*)();
    using ModFactory = std::unique_ptr<Module> (*)();
    using ModPredicate = bool (*)(SubApp*);
    using SubAppFactory = std::unique_ptr<SubApp> (*)();
    using NamedSubAppFactory = std::pair<std::string, SubAppFactory>;
    using ApplicationFactory = std::unique_ptr<Application> (*)();

    void register_application(ApplicationFactory);
    void register_appmod(AppModFactory);
    void register_subapp(std::string, SubAppFactory);
    void register_mod(ModFactory, ModPredicate);
    std::unique_ptr<Application> create_application();

    template <typename T>
        requires std::is_base_of_v<Application, T>
    void register_application() {
        register_application([]() -> std::unique_ptr<Application> { return std::make_unique<T>(); });
    }

    template <typename T>
        requires std::is_base_of_v<AppModule, T>
    void register_appmod() {
        register_appmod([]() -> std::unique_ptr<AppModule> { return std::make_unique<T>(); });
    }

    template <typename T>
        requires std::is_base_of_v<SubApp, T>
    void register_subapp() {
        register_subapp(T::sname(), []() -> std::unique_ptr<SubApp> { return std::make_unique<T>(); });
    }

    template <typename T, typename Parent>
        requires std::is_base_of_v<Module, T> && std::is_base_of_v<SubApp, Parent>
    void register_mod() {
        register_mod([]() -> std::unique_ptr<Module> { return std::make_unique<T>(); },
            [](SubApp* candidate) { return dynamic_cast<Parent*>(candidate) != nullptr; });
    }

    /// Running `SubApp::init()`, `SubApp::loop()` or `SubApp::quit()` will handle all SubApp mods.
    struct SubApp : BaseClass {
        tClass;
        Application* application = nullptr;
        std::vector<std::unique_ptr<Module>> mods;
        std::vector<Module*> init_mod_order;
        std::vector<Module*> loop_mod_order;
        bool added_all_mods = false;

        void init() override {
            load_all_mods();
            for (auto* mod : init_mod_order) mod->init();
        }
        void loop() override {
            for (auto* mod : loop_mod_order) mod->loop();
        }
        void quit() override {
            for (auto* mod : init_mod_order | std::views::reverse) mod->quit();
        }

        void create_all_mods();
        void load_all_mods();
        void add_all_mods() { load_all_mods(); }

        template <typename T>
        T& getmod() {
            if (auto* result = find_mod<T>()) return *result;
            throw std::logic_error("SubApp module does not exist: " + std::string(typeid(T).name()));
        }
        template <typename T>
        T* find_mod() {
            static_assert(std::is_base_of_v<Module, T>, "T must derive from Module");
            create_all_mods();
            for (auto& mod : mods)
                if (auto* result = dynamic_cast<T*>(mod.get())) return result;
            return nullptr;
        }
    };

    /// Running `Application::init()`, `Application::loop()` or `Application::quit()` will handle all Application mods.
    struct Application : BaseClass {
        tClass;

        std::string name;
        bool done = false;
        std::vector<std::unique_ptr<AppModule>> mods;
        std::map<std::string, std::unique_ptr<SubApp>> subapps;
        bool added_all_mods = false;
        std::vector<AppModule*> init_mod_order;
        std::vector<AppModule*> loop_mod_order;

        void init() override;
        void loop() override;
        void quit() override;

        void create_all_mods();
        void load_all_mods();
        void add_all_mods() { load_all_mods(); }
        SubApp& load_subapp(std::string_view name);
        template <typename T>
        T& load_subapp() {
            return static_cast<T&>(load_subapp(T::sname()));
        }
        SubApp& get_subapp(std::string_view name);
        template <typename T>
        T& get_subapp() {
            return static_cast<T&>(get_subapp(T::sname()));
        }
        template <typename T>
        T* get_subapp_ptr() {
            SubApp* subapp = subapps[T::sname()].get();
            if (!subapp) return nullptr;
            return dynamic_cast<T*>(subapp);
        }
        template <typename T>
        T* get_subapp_ptr(const std::string& subappname) {
            SubApp* subapp = subapps[subappname].get();
            if (!subapp) return nullptr;
            return dynamic_cast<T*>(subapp);
        }
        template <typename T>
        T& get_subapp(const std::string& subappname) {
            SubApp* subapp = get_subapp_ptr<T>(subappname);
            if (!subapp) throw std::logic_error("Application subapp does not exist: " + subappname);
            T* retval = dynamic_cast<T*>(subapp);
            if (!retval)
                throw std::logic_error("Application subapp " + subappname + " (of type " + subapp->vname() +
                                       ") is not of type " + T::sname());
            return *retval;
        }

        void unload_subapp(std::string_view name);
        template <typename T>
        void unload_subapp() {
            unload_subapp(T::sname());
        }

        template <typename T>
        T& getmod() {
            if (auto* result = find_mod<T>()) return *result;
            throw std::logic_error("Application module does not exist: " + std::string(typeid(T).name()));
        }
        template <typename T>
        T* find_mod() {
            static_assert(std::is_base_of_v<AppModule, T>, "T must derive from AppModule");
            create_all_mods();
            for (auto& mod : mods)
                if (auto* result = dynamic_cast<T*>(mod.get())) return result;
            return nullptr;
        }
        template <typename T>
        std::vector<T*> get_mods() {
            create_all_mods();
            std::vector<T*> result;
            for (auto& mod : mods)
                if (auto* value = dynamic_cast<T*>(mod.get())) result.push_back(value);
            return result;
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
#define run_before_main \
    static ::triolib::detail::RunBeforeMain _concat(__RUN_BEFORE_MAIN, __COUNTER__) = ::triolib::detail::FuncAdd{} + []
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
#define REGISTER_MOD(...) \
    TRIOLIB_REGISTER_MOD_SELECT(__VA_ARGS__, TRIOLIB_REGISTER_MOD_2, TRIOLIB_REGISTER_MOD_1)(__VA_ARGS__)

#define tassertmsg(to_check, msg)                                                                            \
    do {                                                                                                     \
        if (!(to_check)) throw std::runtime_error("Assert `" #to_check "` has failed: " + std::string(msg)); \
    } while (0)
#define tassertmsgerr(to_check, msg, err)                                                     \
    do {                                                                                      \
        using namespace std;                                                                  \
        if (!(to_check)) throw err("Assert `" #to_check "` has failed: " + std::string(msg)); \
    } while (0)
#define tassert(to_check)                                                                \
    do {                                                                                 \
        if (!(to_check)) throw std::runtime_error("Assert `" #to_check "` has failed."); \
    } while (0)

#if BUILD_TYPE <= BUILD_TYPE_RELWITHDEBINFO
#    define tassume(to_check, msg)     \
        do {                           \
            tassertmsg(to_check, msg); \
            [[assume(to_check)]];      \
        } while (0)
#else
#    define tassume(to_check, msg) [[assume(to_check)]]
#endif

/// Only runs if the code has ran less than `n` times.
#define run_times(n)                         \
    if ([] {                                 \
            static int _TRIOLIB_has_run = 0; \
            if (_TRIOLIB_has_run < n) {      \
                _TRIOLIB_has_run++;          \
                return true;                 \
            }                                \
            return false;                    \
        }())

/// Only runs if the code has not been run before.
#define run_once run_times(1)

/// Runs the code if the time since the last run is greater than `time` seconds.
#define run_limited(time)                                                                                           \
    if ([] {                                                                                                        \
            static auto _TRIOLIB_last_run = std::chrono::steady_clock::now() - std::chrono::duration<double>(time); \
            auto now = std::chrono::steady_clock::now();                                                            \
            if (now - _TRIOLIB_last_run > std::chrono::duration<double>(time)) {                                    \
                _TRIOLIB_last_run = now;                                                                            \
                return true;                                                                                        \
            }                                                                                                       \
            return false;                                                                                           \
        }())

extern struct Console {
    bool enabled;
    logger::Logger log, info, warn, err, debug;

    static logger::Logger title(std::string_view title, const logger::Logger& extends_off);
    static logger::Logger title(std::string_view title);
    static logger::Logger generate_from_ostream(std::ostream& ostream);
} console;

