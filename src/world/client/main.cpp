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

    std::vector<ChunkIndex> get_overlapping_big(ChunkIndex index, bool locks = true) override {
        if (locks) {
            std::shared_lock lock(chunks_mutex);
            return get_overlapping_big(index, false);
        }

        std::vector<ChunkIndex> result;
        for (const auto& [candidate, chunk] : chunks) {
            if (!chunk || candidate.lod <= index.lod) continue;
            const auto scale = 1u << (candidate.lod - index.lod);
            const auto contains = [](int value, int start, unsigned width) {
                return value >= start && value < start + static_cast<int>(width);
            };
            const int start_x = candidate.pos.x * static_cast<int>(scale);
            const int start_y = candidate.pos.y * static_cast<int>(scale);
            const int start_z = candidate.pos.z * static_cast<int>(scale);
            if (contains(index.pos.x, start_x, scale) && contains(index.pos.y, start_y, scale) &&
                contains(index.pos.z, start_z, scale))
                result.push_back(candidate);
        }
        return result;
    }

    std::vector<ChunkIndex> get_overlapping_small(ChunkIndex index, bool locks = true) override {
        if (locks) {
            std::shared_lock lock(chunks_mutex);
            return get_overlapping_small(index, false);
        }

        std::vector<ChunkIndex> result;
        for (const auto& [candidate, chunk] : chunks) {
            if (!chunk || candidate.lod >= index.lod) continue;
            const auto scale = 1u << (index.lod - candidate.lod);
            const auto in_range = [](int value, int start, unsigned width) {
                return value >= start && value < start + static_cast<int>(width);
            };
            const int start_x = index.pos.x * static_cast<int>(scale);
            const int start_y = index.pos.y * static_cast<int>(scale);
            const int start_z = index.pos.z * static_cast<int>(scale);
            if (in_range(candidate.pos.x, start_x, scale) && in_range(candidate.pos.y, start_y, scale) &&
                in_range(candidate.pos.z, start_z, scale))
                result.push_back(candidate);
        }
        return result;
    }

    std::vector<ChunkIndex> get_overlapping_neighbors(ChunkIndex index, bool locks = true) override {
        if (locks) {
            std::shared_lock lock(chunks_mutex);
            return get_overlapping_neighbors(index, false);
        }

        std::vector<ChunkIndex> result;
        constexpr ChunkPos offsets[] = {
            {0, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
        for (const auto offset : offsets) {
            const ChunkIndex candidate{index.pos + offset, index.lod};
            if (chunks.contains(candidate) && chunks.at(candidate)) result.push_back(candidate);
        }
        return result;
    }

    bool should_render_chunk(ChunkIndex index, bool locks = true) override {
        if (locks) {
            shared_lock __(chunks_mutex);
            return should_render_chunk(index, false);
        }

        if (!chunks.contains(index)) return false;
        if (!chunks.at(index)) return false;
        if (!chunks.at(index)->should_render) return false;
        return true;
    }

    ChunkPos get_camera_pos(LodLevel lod = 0, bool locks = true) override {
        std::scoped_lock lock(host->camera_mutex);
        const auto width = static_cast<double>(host->lod_descriptors.at(lod).width);
        const auto scale = static_cast<double>(host->lod_descriptors.at(lod).size);
        return {
            static_cast<int>(std::floor(host->camera.x / (width * scale))),
            static_cast<int>(std::floor(host->camera.y / (width * scale))),
            static_cast<int>(std::floor(host->camera.z / (width * scale))),
        };
    }
} REGISTER_SUBAPP(WorldClientImpl);
