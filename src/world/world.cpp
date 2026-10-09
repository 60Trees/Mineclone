#include "world.hpp"
#include <base/poller.hpp>
#include <base/fps_handler.hpp>
#include <glm/common.hpp>
#include "triolib.hpp"

using namespace std;
using namespace triolib;

struct WorldImpl : World, Poller {
    tImplClass(World);
    WorldImpl() : Poller(5) {}

    void init() override { SubApp::init(); }
    void loop() override {
        SubApp::loop();
        Poller::trypoll(this);
    }
    void poll() override {
        glm::vec<3, double> distance;
        if (camera.x > snap_back_threshold) distance.x -= snap_back_threshold;
        if (camera.x < -snap_back_threshold) distance.x += snap_back_threshold;
        if (camera.y > snap_back_threshold) distance.y -= snap_back_threshold;
        if (camera.y < -snap_back_threshold) distance.y += snap_back_threshold;
        if (camera.z > snap_back_threshold) distance.z -= snap_back_threshold;
        if (camera.z < -snap_back_threshold) distance.z += snap_back_threshold;

        camera.x += distance.x;
        camera.y += distance.y;
        camera.z += distance.z;

        for (auto& [_, callback] : snap_back_callback) callback(distance);
    }
    void quit() override { SubApp::quit(); }
} REGISTER_SUBAPP(WorldImpl);
