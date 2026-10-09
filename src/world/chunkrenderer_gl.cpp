#include "world.hpp"
#include <renderer/glrenderer.hpp>

#include <vector>

using namespace std;

struct ChunkRenderer : GlRenderer {
    tClass;

    vector<BlockVertex> vertices;
    World* world = nullptr;

    void init() override {}
    void loop() override {
        world = &parent->application->get_subapp<World>();
    }
    void quit() override {}
} REGISTER_MOD(ChunkRenderer, MainGlRenderer);
