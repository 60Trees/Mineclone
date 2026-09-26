/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#pragma once

#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>
#include <utility>
#include <algorithm>

extern bool before_main;

std::string_view load_asset(const std::string& path);

enum BuildType : unsigned char {
    Debug = BUILD_TYPE_DEBUG,
    RelWithDebInfo = BUILD_TYPE_RELWITHDEBINFO,
    MinSizeRel = BUILD_TYPE_MINSIZEREL,
    Release = BUILD_TYPE_RELEASE,
};

constexpr static BuildType build_type = (BuildType)BUILD_TYPE;

namespace triolib {
    struct BaseClass {
        virtual ~BaseClass() = default;

        virtual void init() = 0;
        virtual void loop() = 0;
        virtual void quit() = 0;
    };

    struct Application;

    /** @note When constructing, parent is nullptr!
     *
     *  There are two independent orderings a mod can influence:
     *    - The init/quit order: mods init() in this order, and quit() in the
     *      *reverse* of this order (last to init is first to quit).
     *    - The loop order: mods loop() in this order.
     *
     *  Each ordering has its own before/after/priority hooks so a mod can,
     *  for example, insist on looping last while not caring at all about
     *  init/quit order (or vice versa).
     *
     *  Rules for the constraints:
     *    1. A mod cannot claim to go both before AND after the same other mod
     *       in the same ordering - that is a contradiction and throws.
     *    2. If two mods mutually disagree (A wants to go before B, B wants to
     *       go before A - via any combination of before/after calls), the mod
     *       with the higher get_priority() for that ordering wins. If the
     *       priorities are equal, the outcome is unspecified (but
     *      deterministic for a given mod list/order).
     */
    struct AppModule : BaseClass {
        Application* parent = nullptr;

        virtual ~AppModule() = default;

        // --- Init / quit ordering ---
        // quit() order is the reverse of the init() order, so "goes before"
        // here means "inits earlier / quits later".
        virtual bool does_go_before_init(const AppModule& other) const { return false; }
        virtual bool does_go_after_init(const AppModule& other) const { return false; }
        virtual float get_priority_init() const { return 0.0f; }

        virtual bool does_go_before_loop(const AppModule& other) const { return false; }
        virtual bool does_go_after_loop(const AppModule& other) const { return false; }
        virtual float get_priority_loop() const { return 0.0f; }
    };

    std::vector<std::unique_ptr<AppModule>> get_app_mods();

    struct Application : BaseClass {
        void init() override {
            rebuild_mod_orders();

            for (auto* mod : init_order) mod->init();
        }

        void loop() override {
            for (auto* mod : loop_order) mod->loop();
        }

        void quit() override {
            for (auto it = init_order.rbegin(); it != init_order.rend(); ++it) (*it)->quit();
        }

        std::string name{};
        bool done = false;

        std::vector<std::unique_ptr<AppModule>> mods{};
        bool added_all_mods = false;

        private:
        std::vector<AppModule*> init_order{};
        std::vector<AppModule*> loop_order{};

        template <typename BeforeFn, typename AfterFn, typename PriorityFn>
        static std::vector<AppModule*> build_order(
            const std::vector<AppModule*>& in, BeforeFn does_go_before, AfterFn does_go_after, PriorityFn get_priority) {
            const std::size_t n = in.size();

            // self-contradiction check: mod i claims both before and after mod j
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < n; ++j) {
                    if (i == j) continue;

                    auto& in_i = *in[i];
                    auto& in_j = *in[j];

                    if (does_go_before(in_i, in_j) && does_go_after(in_i, in_j)) {
                        throw std::logic_error("Application module wants to go both before and after the same module: " +
                                               std::string(typeid(in_i).name()) + " vs " + std::string(typeid(in_j).name()));
                    }
                }
            }

            std::vector<std::vector<std::size_t>> edges(n);
            std::vector<std::size_t> in_degree(n, 0);

            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = i + 1; j < n; ++j) {
                    auto& in_i = *in[i];
                    auto& in_j = *in[j];

                    const bool i_before_j = does_go_before(in_i, in_j) || does_go_after(in_j, in_i);
                    const bool j_before_i = does_go_before(in_j, in_i) || does_go_after(in_i, in_j);

                    if (i_before_j && j_before_i) {
                        // Mutual conflict - higher priority mod's wish wins.
                        // Equal priority: unspecified, defaults to i before j.
                        const float pi = get_priority(in_i);
                        const float pj = get_priority(in_j);

                        if (pj > pi) {
                            edges[j].push_back(i);
                            ++in_degree[i];
                        } else {
                            edges[i].push_back(j);
                            ++in_degree[j];
                        }
                    } else if (i_before_j) {
                        edges[i].push_back(j);
                        ++in_degree[j];
                    } else if (j_before_i) {
                        edges[j].push_back(i);
                        ++in_degree[i];
                    }
                }
            }

            std::vector<std::size_t> available;
            for (std::size_t i = 0; i < n; ++i) {
                if (in_degree[i] == 0) available.push_back(i);
            }

            std::vector<AppModule*> result;
            result.reserve(n);

            while (!available.empty()) {
                std::size_t pick_pos = 0;
                for (std::size_t k = 1; k < available.size(); ++k) {
                    if (available[k] < available[pick_pos]) pick_pos = k;
                }
                const std::size_t node = available[pick_pos];
                available.erase(available.begin() + pick_pos);

                result.push_back(in[node]);

                for (std::size_t next : edges[node]) {
                    if (--in_degree[next] == 0) available.push_back(next);
                }
            }

            if (result.size() != n) {
                throw std::logic_error("Application module ordering has a cycle that priority could not resolve");
            }

            return result;
        }

        void rebuild_mod_orders() {
            add_all_mods();

            std::vector<AppModule*> all;
            all.reserve(mods.size());
            for (auto& mod : mods) all.push_back(mod.get());

            init_order = build_order(
                all, [](const AppModule& a, const AppModule& b) { return a.does_go_before_init(b); },
                [](const AppModule& a, const AppModule& b) { return a.does_go_after_init(b); },
                [](const AppModule& a) { return a.get_priority_init(); });

            loop_order = build_order(
                all, [](const AppModule& a, const AppModule& b) { return a.does_go_before_loop(b); },
                [](const AppModule& a, const AppModule& b) { return a.does_go_after_loop(b); },
                [](const AppModule& a) { return a.get_priority_loop(); });
        }

        public:
        template <typename T, typename Factory = std::nullptr_t>
        void depend(Factory factory = nullptr, bool exact = false) {
            add_all_mods();
            static_assert(std::is_base_of_v<AppModule, T>, "T must derive from AppModule");

            for (const auto& _mod : mods) {
                auto* mod = _mod.get();

                if (exact && typeid(*mod) == typeid(T)) return;
                if (!exact && dynamic_cast<T*>(mod)) return;
            }

            std::unique_ptr<T> mod;

            if constexpr (std::is_same_v<Factory, std::nullptr_t>) {
                mod = std::make_unique<T>();
            } else {
                mod = factory();
            }

            mod->parent = this;
            mods.emplace_back(std::move(mod));

            rebuild_mod_orders();
        }
        template <typename T>
        std::vector<T*> get_mods() {
            add_all_mods();
            static_assert(std::is_base_of_v<AppModule, T>, "T must derive from AppModule");

            std::vector<T*> result;

            for (auto& _mod : mods) {
                if (auto* mod = dynamic_cast<T*>(_mod.get())) {
                    result.push_back(mod);
                }
            }

            return result;
        }

        void add_all_mods() {
            if (added_all_mods) return;
            if (before_main) throw std::logic_error("Cannot add mods before main!");
            std::cout << "[APPLICATION] Initializing all mods\n";
            auto newmods = get_app_mods();
            for (auto& newmod : newmods) {
                newmod->parent = this;
                mods.push_back(std::move(newmod));
            }
            added_all_mods = true;
        }

        template <typename T>
        T& find_mod() {
            add_all_mods();
            for (auto& mod : mods) {
                T* base_mod = dynamic_cast<T*>(mod);
                if (base_mod) return *base_mod;
            }
            throw std::logic_error("Application module does not exist: " + std::string(typeid(T).name()));
        }

        template <typename T, typename Factory = std::nullptr_t>
        T& depend_get(Factory factory = nullptr, bool exact = false) {
            add_all_mods();
            depend<T>(factory, exact);
            return get<T>(exact);
        }

        template <typename T>
        T& get(bool exact = false) {
            add_all_mods();
            static_assert(std::is_base_of_v<AppModule, T>, "T must derive from AppModule");

            for (auto& _mod : mods) {
                auto* mod = _mod.get();

                if (exact && typeid(*mod) == typeid(T)) return *static_cast<T*>(mod);
                if (!exact && dynamic_cast<T*>(mod)) return *dynamic_cast<T*>(mod);
            }

            throw std::logic_error("Application module does not exist: " + std::string(typeid(T).name()));
        }
        template <typename T>
        inline T& operator()() {
            return get<T>(false);
        }
    };
    namespace detail {

        struct RunBeforeMain {
            RunBeforeMain(void (*func)()) { func(); }
        };

        struct FuncAdd {
            friend void (*operator+(FuncAdd, void (*func)()))() { return func; }
        };

        template <typename F>
        struct FinallyGuard {
            F func;

            FinallyGuard(F func) : func(std::move(func)) {}
            ~FinallyGuard() { func(); }

            FinallyGuard(const FinallyGuard&) = delete;
            FinallyGuard& operator=(const FinallyGuard&) = delete;
        };

        struct FinallyAdd {
            template <typename F>
            FinallyGuard<F> operator+(F func) const {
                return FinallyGuard<F>(std::move(func));
            }
        };
    }  // namespace detail

    std::unique_ptr<Application> create_application();
    void register_application(std::unique_ptr<Application> (*)());
    template <typename T>
        requires std::is_base_of_v<Application, T>
    void register_application() {
        register_application([] -> std::unique_ptr<Application> { return std::make_unique<T>(); });
    }
    void register_mod(std::unique_ptr<AppModule> (*factory)());
    template <typename T>
        requires std::is_base_of_v<AppModule, T>
    void register_mod() {
        register_mod([] -> std::unique_ptr<AppModule> { return std::make_unique<T>(); });
    }
    std::vector<std::unique_ptr<AppModule>> create_mods();
}  // namespace triolib

#define REGISTER_MOD(classname) \
    ;                           \
    run_before_main { ::triolib::register_mod<classname>(); }
#define REGISTER_APP(classname) \
    ;                           \
    run_before_main { ::triolib::register_application<classname>(); }

#define __concat(x, y) x##y
#define _concat(x, y) __concat(x, y)

#define run_before_main static ::triolib::detail::RunBeforeMain _concat(__RUN_BEFORE_MAIN, __COUNTER__) = ::triolib::detail::FuncAdd{} + []

#define _REGISTER(appname)                                               \
    run_before_main {                                                    \
        /* If appname extends off Application, then register application \
         * else register mod*/                                           \
    }

#define run_on_exit auto _concat(__FINALLY_GUARD, __COUNTER__) = ::triolib::detail::FinallyAdd{} + [&]()

#define tassertmsg(to_check, msg)                                                                                \
    do {                                                                                                         \
        if (!(to_check)) throw ::std::runtime_error("Assert `" #to_check "` has failed: " + ::std::string(msg)); \
    } while (0)

#define tassert(to_check)                                                                  \
    do {                                                                                   \
        if (!(to_check)) throw ::std::runtime_error("Assert `" #to_check "` has failed."); \
    } while (0)
