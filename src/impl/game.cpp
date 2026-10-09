/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <triolib.hpp>

#include <base/input_handler.hpp>
#include <base/resources.hpp>
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;

struct Game : Application {
    tClass;
    Game() { name = "Mineclone"; }
    void init() override {
        Application::init();

        load_subapp("MainGlRenderer");
        load_subapp("World");
    }
} REGISTER_APP(Game);

struct RandomShi : AppModule {
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
} REGISTER_MOD(RandomShi);
