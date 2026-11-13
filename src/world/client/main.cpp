/**
 * @file world/client/main.cpp
 * @brief The client implementation of the world.
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * The client side of the world is responsible
 * for handling its own chunks, deciding what
 * chunks to render, etc.
 */

#include <world/client/main.hpp>
#include <triolib.hpp>

using namespace std;

struct WorldClientImpl : WorldClient {
    tImplClass(WorldClient);
    // TODO: Implement get_overlapping

    void init() override {
        host = &application->get_subapp<WorldServer>();
        load_all_mods();
        for (auto* mod : init_mod_order) {
            if (auto* worldmod = dynamic_cast<WorldClient::Module*>(mod)) {
                worldmod->client = this;
                worldmod->host = host;
            }
            mod->init();
        }
    }

    bool should_render_chunk(ChunkIndex index, bool locks = true) override {
        if (locks) {
            shared_lock __(chunks_mutex);
            return should_render_chunk(index, false);
        }

        if (!chunks.contains(index)) return false;
        if (!chunks.at(index)) return false;
        if (!chunks.at(index)->should_render) return false;
    }
} REGISTER_SUBAPP(WorldClientImpl);
