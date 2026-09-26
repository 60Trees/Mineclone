/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <triolib.hpp>

#include <base/renderer.hpp>
#include <base/input_handler.hpp>
#include <base/resources.hpp>
CMRC_DECLARE(assets);

extern "C" const char* mc_client_location();
extern "C" const char* mc_client_version();

using namespace triolib;
using namespace std;
using namespace Mineclone;

struct Game : Application {
    Renderer& r = get<Renderer>();

    Resources resources;

    Game() { name = "Mineclone"; }
} REGISTER_APP(Game);

struct RandomShi : AppModule {
    ResourceHandler* rh;
    bool has_finished_loading = false;
    void init() override {
        rh = &parent->get<ResourceHandler>();
        assert(rh);
        rh->whitelist.insert("data");
        rh->whitelist.insert("assets");
        rh->resource_origins.push_back({std::string(mc_client_location()), ResourceHandler::ResourceOriginType::ZipFromFs});
    }
    void loop() override {
        if (rh->has_loaded && !has_finished_loading) {
            has_finished_loading = true;
            cout << "Loaded " << rh->resources.size() << " resources." << endl;
        }
    }
    void quit() override {}
} REGISTER_MOD(RandomShi);
