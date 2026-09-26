/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <triolib.hpp>

#include <base/renderer.hpp>
#include <base/input_handler.hpp>
#include <impl/resource_pack.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;
using namespace Mineclone;

struct Game : Application {
    Renderer& r = get<Renderer>();
    vector<InputHandler*> input_handlers{};

    ResourcePack resource_pack;

    Game() { name = "Mineclone"; }

    void init() override {
        Application::init();
        input_handlers = get_mods<InputHandler>();

        {
            ZipSource zip_source(mc_client_location());
            resource_pack.load(zip_source);
            cout << "Minecraft JAR resources: " << resource_pack.size() << '\n';
        }

        if (const auto* some_json = resource_pack.try_get("minecraft/models/block/acacia_button.json")) {
            const auto& json = some_json->json();

            cout << "some rando json contains " << json.size() << " entries\n";
        }
    }

    void loop() override {
        Application::loop();

        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT) done = true;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) done = true;

            for (auto* mod : input_handlers) mod->digest_event(event);
        }
    }
} REGISTER_APP(Game);
