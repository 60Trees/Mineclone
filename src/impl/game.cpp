/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <thread>
#include <triolib.hpp>

#include <base/input_handler.hpp>
#include <base/resources.hpp>
#include <cmrc/cmrc.hpp>
#include <misc/loading_screen.hpp>
#include <world/worldrenderer.hpp>
#include <renderer/renderer.hpp>
#include <base/fps_handler.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;

struct RendererGame : Application {
    tClass;
    std::string renderer_name = "MainGlRenderer";
};

struct Game : RendererGame {
    tClass;
    Game() { name = "Mineclone"; }
    /// Needs GlRenderer to be loaded first
    void load_resources() {
        auto& rh = getmod<ResourceHandler>();
        auto& renderer = get_subapp<MainRenderer>(renderer_name);
        auto& fps = getmod<FpsHandler>();
        auto& loading_screen = renderer.getmod<LoadingScreen>();

        rh.whitelist.clear();
        rh.whitelist.insert("data");
        rh.whitelist.insert("assets");

        rh.resource_origins = {{std::string(mc_client_location()), ResourceHandler::ResourceOriginType::ZipFromFs}};
        rh.start_loading();

        // loading_screen.progress = &rh.progress;
        // std::thread loading_thread([&rh] { rh.start_loading(); });
        // while (!rh.has_loaded.load()) {
        //     fps.loop();
        //     renderer.loop();
        // }
        // loading_thread.join();
        // loading_screen.progress = nullptr;
    }

    void init() override {
        Application::init();

        load_subapp(renderer_name);

        load_resources();

        load_subapp("World");
        load_subapp<MainWorldRenderer>();
    }

    bool has_finished_loading = false;
    void loop() override { Application::loop(); }
} REGISTER_APP(Game);

struct GameResourceLoader : AppModule {
    tClass;
    ResourceHandler* rh;
    bool has_finished_loading = false;
    void init() override {
        rh = &parent->getmod<ResourceHandler>();
        assert(rh);
        rh->whitelist.insert("data");
        rh->whitelist.insert("assets");
        rh->resource_origins.push_back(
            {std::string(mc_client_location()), ResourceHandler::ResourceOriginType::ZipFromFs});
    }
    void loop() override {
        if (rh->has_loaded && !has_finished_loading) {
            has_finished_loading = true;
            console.debug << "Loaded " << rh->resources.size() << " resources.";
            const auto* resource = rh->resources.try_get("minecraft/gpu_warnlist.json");
            if (!resource)
                throw runtime_error(
                    "Loaded resource is missing: "
                    "assets/minecraft/gpu_warnlist.json");
            console.debug << "Read assets/minecraft/gpu_warnlist.json: " << resource->json().dump(2);
        }
    }
    void quit() override {}
} REGISTER_MOD(GameResourceLoader);
