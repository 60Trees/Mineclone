#include "triolib.hpp"

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
        std::vector<SubAppFactory>& subapp_factories() {
            static std::vector<SubAppFactory> value;
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
        if (!factory)
            throw std::runtime_error("Cannot register a null subapp factory.");
        subapp_factories().push_back(factory);
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
        for (auto factory : subapp_factories()) {
            auto subapp = factory();
            if (!subapp)
                throw std::runtime_error(
                    "Registered subapp factory returned nullptr.");
            if (!contains_type(subapps, typeid(*subapp))) {
                subapp->application = this;
                subapps.push_back(std::move(subapp));
            }
        }
        added_all_subapps = true;
    }

    void Application::init() {
        create_all_mods();
        subapps_add_all();
        loop_order.clear();
        loop_order.reserve(mods.size());
        for (auto& mod : mods) loop_order.push_back(mod.get());
        for (auto& mod : mods) mod->init();
        for (auto& subapp : subapps) subapp->init();
    }
    void Application::loop() {
        for (auto* mod : loop_order) mod->loop();
        for (auto& subapp : subapps) subapp->loop();
    }
    void Application::quit() {
        for (auto it = subapps.rbegin(); it != subapps.rend(); ++it)
            (*it)->quit();
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) (*it)->quit();
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
