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
#include <cmath>

using namespace std;

struct WorldClientImpl : WorldClient {
    tImplClass(WorldClient);

    void init() override {
        host = &application->get_subapp<WorldServer>();
        lod_count = host->lod_descriptors.size();
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

        const auto it = chunks.find(index);
        return it != chunks.end() && it->second && it->second->should_render;
    }

    ChunkPos get_camera_pos(LodLevel lod = 0, bool locks = true) override {
        std::scoped_lock lock(host->camera_mutex);
        const auto width = static_cast<double>(host->lod_descriptors.at(lod).width);
        const auto scale = static_cast<double>(host->lod_descriptors.at(lod).scale);
        return {
            static_cast<int>(std::floor(host->camera.x / (width * scale))),
            static_cast<int>(std::floor(host->camera.y / (width * scale))),
            static_cast<int>(std::floor(host->camera.z / (width * scale))),
        };
    }
} REGISTER_SUBAPP(WorldClientImpl);
