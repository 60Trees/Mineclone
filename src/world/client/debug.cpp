/**
 * @file world/client/debug.cpp
 * @brief Displays chunk pipeline benchmarks on the debug screen
 * @detail
 * Bottom left: chunk throughputs (per second), queue depths and rolling averages.
 * Top left: the five most recently meshed chunks, newest first.
 */

#include <ui/debug_screen.hpp>
#include <world/client/main.hpp>
#include <world/client/mesh/main.hpp>

#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>

using namespace std;

namespace {
    string format_seconds(double seconds) {
        char buffer[64];
        if (seconds < 0.001)
            snprintf(buffer, sizeof(buffer), "%.1f us", seconds * 1e6);
        else
            snprintf(buffer, sizeof(buffer), "%.3f ms", seconds * 1e3);
        return buffer;
    }

    string format_average(const RollingAverage::Snapshot& snapshot) {
        if (snapshot.count == 0) return "n/a";
        return format_seconds(snapshot.average) + " (" + to_string(snapshot.count) + ")";
    }

    string format_rate(const RateMeter& meter) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.1f/s", meter.per_second());
        return buffer;
    }
}  // namespace

struct WorldClientDebugImpl : WorldClient::Module {
    tClass;

    DebugScreen* debug_screen = nullptr;

    void init() override { WorldClient::Module::init(); }

    void loop() override {
        // The debug screen is created by the UI renderer module, which can be activated
        // after the world client, so look it up lazily instead of caching it in init().
        if (!debug_screen) debug_screen = parent->application->get_subapp_ptr<DebugScreen>();
        if (!debug_screen) return;

        const auto& stats = client->stats;

        size_t chunk_count = 0;
        {
            shared_lock lock(client->chunks_mutex);
            chunk_count = client->chunks.size();
        }

        size_t mesh_count = 0;
        if (auto* meshes = client->find_mod<WorldMeshHandler>()) {
            shared_lock lock(meshes->chunk_meshes_mutex);
            mesh_count = meshes->chunk_meshes.size();
        }

        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Meshed", format_rate(stats.meshed_per_second));
        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Generated", format_rate(stats.generated_per_second));
        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Loaded", format_rate(stats.loaded_per_second));
        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Picked", format_rate(stats.picked_per_second));
        debug_screen->show_at(
            DebugScreen::Corner::BottomLeft, "Mesh queue", to_string(stats.mesh_queue.load()));
        debug_screen->show_at(
            DebugScreen::Corner::BottomLeft, "Gen queue", to_string(stats.generation_queue.load()));
        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Chunks", to_string(chunk_count));
        debug_screen->show_at(DebugScreen::Corner::BottomLeft, "Meshes", to_string(mesh_count));
        debug_screen->show_at(
            DebugScreen::Corner::BottomLeft, "Avg mesh", format_average(stats.mesh_time.snapshot()));
        debug_screen->show_at(
            DebugScreen::Corner::BottomLeft, "Avg generate", format_average(stats.generation_time.snapshot()));
        debug_screen->show_at(
            DebugScreen::Corner::BottomLeft, "Avg pick", format_average(stats.pick_time.snapshot()));

        const auto recent = stats.recent();
        for (size_t i = 0; i < ChunkLoadStats::recent_limit; ++i) {
            const string key = "Recent " + to_string(i + 1);
            if (i >= recent.size()) {
                debug_screen->show_at(DebugScreen::Corner::TopLeft, key, "");
                continue;
            }

            const auto& mesh = recent[i];
            const string value = "L" + to_string(mesh.index.lod) + " (" + to_string(mesh.index.pos.x) + ", " +
                                 to_string(mesh.index.pos.y) + ", " + to_string(mesh.index.pos.z) + ") " +
                                 format_seconds(mesh.seconds);
            debug_screen->show_at(DebugScreen::Corner::TopLeft, key, value);
        }
    }
} REGISTER_MOD(WorldClientDebugImpl, WorldClient);
