#include "triolib.hpp"
#include "logger.hpp"

namespace triolib {
    namespace {
        std::vector<AppModFactory>& appmod_factories() {
            static std::vector<AppModFactory> value;
            return value;
        }
        std::vector<std::pair<ModPredicate, ModFactory>>& mod_factories() {
            static std::vector<std::pair<ModPredicate, ModFactory>> value;
            return value;
        }
        std::vector<NamedSubAppFactory>& subapp_factories() {
            static std::vector<NamedSubAppFactory> value;
            return value;
        }
        ApplicationFactory& application_factory() {
            static ApplicationFactory value = nullptr;
            return value;
        }

        template <typename T>
        bool contains_type(const std::vector<std::unique_ptr<T>>& values,
            const std::type_info& type) {
            return std::any_of(values.begin(), values.end(),
                [&](const auto& value) { return typeid(*value) == type; });
        }
    }  // namespace

    void register_application(ApplicationFactory factory) {
        if (!factory)
            throw std::runtime_error(
                "Cannot register a null application factory.");
        if (application_factory())
            throw std::runtime_error(
                "An application has already been registered.");
        application_factory() = factory;
    }
    void register_appmod(AppModFactory factory) {
        if (!factory)
            throw std::runtime_error(
                "Cannot register a null application module factory.");
        appmod_factories().push_back(factory);
    }
    void register_subapp(SubAppFactory factory) {
        register_subapp({}, factory);
    }
    void register_subapp(std::string name, SubAppFactory factory) {
        if (!factory)
            throw std::runtime_error("Cannot register a null subapp factory.");
        subapp_factories().emplace_back(std::move(name), factory);
    }
    void register_mod(ModFactory factory, ModPredicate predicate) {
        if (!factory || !predicate)
            throw std::runtime_error(
                "Cannot register a null module factory or predicate.");
        mod_factories().emplace_back(predicate, factory);
    }

    void SubApp::create_all_mods() {
        if (added_all_mods) return;
        if (before_main)
            throw std::logic_error("Cannot add modules before main.");
        for (auto [predicate, factory] : mod_factories()) {
            if (!predicate(this)) continue;
            auto mod = factory();
            if (!mod)
                throw std::runtime_error(
                    "Registered module factory returned nullptr.");
            if (!contains_type(mods, typeid(*mod))) {
                mod->parent = this;
                mods.push_back(std::move(mod));
            }
        }
        added_all_mods = true;
    }

    void SubApp::load_all_mods() {
        create_all_mods();
        init_mod_order.clear();
        loop_mod_order.clear();
        for (auto& mod : mods) {
            init_mod_order.push_back(mod.get());
            loop_mod_order.push_back(mod.get());
        }
        auto init_order = [](Module* left, Module* right) {
            if (left->order_init_does_go_before(right) || right->order_init_does_go_after(left))
                return true;
            if (left->order_init_does_go_after(right) || right->order_init_does_go_before(left))
                return false;
            return left->init_order_priority() > right->init_order_priority();
        };
        auto loop_order = [](Module* left, Module* right) {
            if (left->order_loop_does_go_before(right) || right->order_loop_does_go_after(left))
                return true;
            if (left->order_loop_does_go_after(right) || right->order_loop_does_go_before(left))
                return false;
            return left->loop_order_priority() > right->loop_order_priority();
        };
        std::stable_sort(init_mod_order.begin(), init_mod_order.end(), init_order);
        std::stable_sort(loop_mod_order.begin(), loop_mod_order.end(), loop_order);
    }

    void Application::create_all_mods() {
        if (added_all_mods) return;
        if (before_main)
            throw std::logic_error("Cannot add modules before main.");
        for (auto factory : appmod_factories()) {
            auto mod = factory();
            if (!mod)
                throw std::runtime_error(
                    "Registered application module factory returned nullptr.");
            if (!contains_type(mods, typeid(*mod))) {
                mod->parent = this;
                mods.push_back(std::move(mod));
            }
        }
        added_all_mods = true;
    }

    void Application::subapps_add_all() {
        if (added_all_subapps) return;
        if (before_main)
            throw std::logic_error("Cannot add subapps before main.");
        for (const auto& [name, factory] : subapp_factories()) {
            auto subapp = factory();
            if (!subapp)
                throw std::runtime_error(
                    "Registered subapp factory returned nullptr.");
            const auto key = name.empty() ? subapp->vname() : name;
            if (!subapps.contains(key)) {
                subapp->application = this;
                subapps.emplace(key, std::move(subapp));
            }
        }
        added_all_subapps = true;
    }

    void Application::init() {
        load_all_mods();
        for (auto* mod : init_mod_order) mod->init();
    }
    void Application::loop() {
        for (auto* mod : loop_mod_order) mod->loop();
        for (auto& [_, subapp] : subapps) subapp->loop();
    }
    void Application::quit() {
        for (auto it = subapps.rbegin(); it != subapps.rend(); ++it)
            it->second->quit();
        for (auto* mod : init_mod_order | std::views::reverse) mod->quit();
    }

    void Application::load_all_mods() {
        create_all_mods();
        init_mod_order.clear();
        loop_mod_order.clear();
        for (auto& mod : mods) {
            init_mod_order.push_back(mod.get());
            loop_mod_order.push_back(mod.get());
        }
        auto init_order = [](AppModule* left, AppModule* right) {
            if (left->order_init_does_go_before(right) || right->order_init_does_go_after(left))
                return true;
            if (left->order_init_does_go_after(right) || right->order_init_does_go_before(left))
                return false;
            return left->init_order_priority() > right->init_order_priority();
        };
        auto loop_order = [](AppModule* left, AppModule* right) {
            if (left->order_loop_does_go_before(right) || right->order_loop_does_go_after(left))
                return true;
            if (left->order_loop_does_go_after(right) || right->order_loop_does_go_before(left))
                return false;
            return left->loop_order_priority() > right->loop_order_priority();
        };
        std::stable_sort(init_mod_order.begin(), init_mod_order.end(), init_order);
        std::stable_sort(loop_mod_order.begin(), loop_mod_order.end(), loop_order);
        this->loop_order = loop_mod_order;
    }

    bool Application::is_subapp_initialized(std::string_view name) const {
        return subapps.contains(std::string{name});
    }

    SubApp& Application::load_subapp(std::string_view name) {
        const auto key = std::string{name};
        if (auto it = subapps.find(key); it != subapps.end()) return *it->second;
        for (const auto& [registered_name, factory] : subapp_factories()) {
            if (registered_name != key) continue;
            auto subapp = factory();
            if (!subapp)
                throw std::runtime_error("Registered subapp factory returned nullptr.");
            subapp->application = this;
            auto& result = *subapp;
            subapps.emplace(key, std::move(subapp));
            result.init();
            return result;
        }
        throw std::out_of_range("Subapp is not registered: " + key);
    }

    void Application::unload_subapp(std::string_view name) {
        auto it = subapps.find(std::string{name});
        if (it == subapps.end()) return;
        it->second->quit();
        subapps.erase(it);
    }

    std::unique_ptr<Application> create_application() {
        if (!application_factory())
            throw std::runtime_error("No application has been registered.");
        auto result = application_factory()();
        if (!result)
            throw std::runtime_error(
                "Registered application factory returned nullptr.");
        return result;
    }
}  // namespace triolib

static constexpr std::string_view newline = "\n";
static constexpr std::string_view midnewl = GREY " ├ " RESET;
static constexpr std::string_view endnewl = GREY " └ " RESET;

#define __mkpref(coloured_text) \
    (std::string(GREY) + TIMESTAMP + " " RESET coloured_text RESET ": ")

Console console{
    .enabled = true,

    .log = {logger::string_source{__mkpref("   LOG")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, std::cout},

    .info = {logger::string_source{__mkpref(ITALIC "  INFO")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, std::cout},

    .warn = {logger::string_source{__mkpref(YELLOW "  WARN")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, std::cerr},

    .err = {logger::string_source{__mkpref(RED " ERROR")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, std::cerr},

    .debug = {logger::string_source{__mkpref(GREEN DIM ITALIC " DEBUG")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, std::cout},
};

logger::Logger Console::generate_from_ostream(std::ostream& ostream) {
    return {logger::string_source{__mkpref("NULL_LOGGER")},
        logger::string_source{std::string{newline}}, true, false,
        logger::string_source{std::string{midnewl}},
        logger::string_source{std::string{endnewl}}, ostream};
}

logger::Logger Console::title(std::string_view _title) {
    return title(_title, console.log);
}
logger::Logger Console::title(
    std::string_view _title, const logger::Logger& extends_off) {
    logger::Logger retval = extends_off;

    retval.prefix = std::string(GREY) + TIMESTAMP + " " RESET +
                    std::string(_title) + RESET + ": ";

    return retval;
}
