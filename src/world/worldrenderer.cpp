#include "worldrenderer.hpp"

using namespace triolib;

struct MainWorldRendererImpl : MainWorldRenderer {
    tImplClass(MainWorldRenderer);
    void init() override {
        load_all_mods();
        for (auto* mod : init_mod_order) {
            if (Renderer* renderer_mod = dynamic_cast<Renderer*>(mod)) {
                renderer_mod->parent = this;
            }
            mod->init();
        }
    }
} REGISTER_SUBAPP(MainWorldRendererImpl);
