/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <triolib.hpp>

#include <base/input_handler.hpp>
#include <base/renderer.hpp>
#include <base/resources.hpp>
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;

struct Game : Application {
    Game() { name = "Mineclone"; }
    void init() override {
        Application::init();
        ResourceHandler::ZipSource jar{
            std::filesystem::path(mc_client_location())};
        const auto manifest = jar.read("META-INF/MANIFEST.MF");
        std::cout << "[DEBUG] Read JAR META-INF/MANIFEST.MF ("
                  << manifest.size() << " bytes)\n";

        ResourceHandler::FilesystemSource files(SOURCE_DIR);
        const auto shader = files.read("assets/vert.glsl");
        std::cout << "[DEBUG] Read assets/vert.glsl (" << shader.size()
                  << " bytes)\n";
    }
} REGISTER_APP(Game);

struct RandomShi : AppModule {
    ResourceHandler* rh;
    bool has_finished_loading = false;
    void init() override {
        rh = &parent->get<ResourceHandler>();
        assert(rh);
        rh->whitelist.insert("data");
        rh->whitelist.insert("assets");
        rh->resource_origins.push_back({std::string(mc_client_location()),
            ResourceHandler::ResourceOriginType::ZipFromFs});
    }
    void loop() override {
        if (rh->has_loaded && !has_finished_loading) {
            has_finished_loading = true;
            cout << "Loaded " << rh->resources.size() << " resources." << endl;
            const auto* resource =
                rh->resources.try_get("minecraft/gpu_warnlist.json");
            if (!resource)
                throw runtime_error(
                    "Loaded resource is missing: "
                    "assets/minecraft/gpu_warnlist.json");
            cout << "Read assets/minecraft/gpu_warnlist.json: "
                 << resource->json().dump(2) << '\n';
        }
    }
    void quit() override {}
} REGISTER_MOD(RandomShi);
