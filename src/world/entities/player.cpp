#include <world/entities/entity.hpp>
#include <base/fps_handler.hpp>
#include <base/controls.hpp>
#include "triolib.hpp"

using namespace std;
using namespace triolib;

struct Player : Entity {
    entityClass("minecraft:player", Player);

    static void update_controls(Entity& e, Application& app) {
        InputHandler& input_handler = app.getmod<InputHandler>();
        Controls& controls = app.getmod<Controls>();
        double dt = app.getmod<FpsHandler>().dt;

        for (auto& [name, control] : e.controls.input) {
            if (PushCtl* push = dynamic_cast<PushCtl*>(&controls[name])) {
                control.update(push->pressure, dt);
            } else {
                control.update(controls[name], dt);
            }
        }
    }

    void tick_controls(Application& app) override {
        Entity::tick_controls(app);
        const auto dt = app.getmod<FpsHandler>().dt;

        pos.y += controls.input["jump"].pressure * dt;
        pos.y -= controls.input["sneak"].pressure * dt;

        double forward = controls.input["forward"].pressure - controls.input["backward"].pressure;
        double right = controls.input["right"].pressure - controls.input["left"].pressure;
    }

    Player(const Player& other) : Entity(other) {}
    Player(std::string id) : Entity(id, EntityControls({}, update_controls)) {}
    Player(const nlohmann::json& data, Application& app) : Entity(data, app) {}

    double camera_priority(Application&) const override { return 1; }
} REGISTER_ENTITY(Player, "minecraft:player");
