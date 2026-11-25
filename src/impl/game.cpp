/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <thread>
#include <triolib.hpp>

#include <base/input_handler.hpp>
#include <base/resources.hpp>
#include <cmrc/cmrc.hpp>
#include <renderer/renderer.hpp>
#include <base/fps_handler.hpp>
#include <world/client/main.hpp>
#include <world/server/main.hpp>
#include <threads/task.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;

struct Game : Application {
    tClass;
    Game() { name = "Mineclone"; }
    /// Needs GlRenderer to be loaded first
    void load_resources() {
        auto& rh = getmod<ResourceHandler>();
        auto& renderer = get_subapp<MainRenderer>();
        auto& fps = getmod<FpsHandler>();

        rh.whitelist.clear();
        rh.whitelist.insert("data");
        rh.whitelist.insert("assets");

        rh.resource_origins = {{std::string(mc_client_location()), ResourceHandler::ResourceOriginType::ZipFromFs}};
        // rh.start_loading();

        rh.run_after_loading.emplace_back([&](ResourceHandler& rh) {
            auto& resources = rh.resources;

            std::map<std::string, std::vector<std::string>> atlas_definitions;

            for (auto& fullns : resources.list_dirs("assets/")) {
                for (auto& texturepath : resources.list_dirs(fullns + "/textures/")) {
                    auto texturetype = get_filename(texturepath);  // e.g. "block"
                    console.log << "Found texture type: " << texturetype << " at " << texturepath;
                    auto to_add = resources.list_pngs(texturepath, true);

                    auto& dest = atlas_definitions[texturetype];
                    dest.insert(
                        dest.end(), std::make_move_iterator(to_add.begin()), std::make_move_iterator(to_add.end()));
                }
            }

            for (auto& [texturetype, paths] : atlas_definitions) {
                std::sort(paths.begin(), paths.end());  // optional: deterministic order
                resources.define_atlas("assets/textures/" + texturetype, paths);
            }

            resources.load_all_atlases();
        });
        std::thread loading_thread([&rh] { rh.start_loading(); });
        while (!rh.has_loaded) {
            fps.loop();
            renderer.loop();
        }
        loading_thread.join();
    }

    inline std::string get_filename(const std::string& path) {
        size_t pos = path.find_last_of('/');
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    void init() override {
        Application::init();

        load_subapp<MainRenderer>();
        load_subapp<RegistryList>();

        load_resources();

        load_subapp<WorldServer>();
        load_subapp<ThreadPool>();
        load_subapp<WorldClient>();
    }

    bool has_finished_loading = false;
    void loop() override { Application::loop(); }
} REGISTER_APP(Game);
