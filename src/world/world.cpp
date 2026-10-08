#include "world.hpp"
#include "triolib.hpp"

using namespace std;
using namespace triolib;

struct WorldImpl : World {
    tClass;

    void init() override { SubApp::init(); }
    void loop() override { SubApp::loop(); }
    void quit() override { SubApp::quit(); }
} REGISTER_SUBAPP(WorldImpl);
