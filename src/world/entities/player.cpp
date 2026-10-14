#include <world/entities/entity.hpp>
#include <base/fps_handler.hpp>
#include <base/controls.hpp>

using namespace std;
using namespace triolib;

struct Player : Entity {
    entityClass("minecraft:player", Player);

    static void update_controls(Entity& e, Application& app) {
        InputHandler& input_handler = app.getmod<InputHandler>();
        Controls& controls = app.getmod<Controls>();
        double dt = app.getmod<FpsHandler>().dt;

        for (auto& [name, control] : e.controls.input) {
            control.update(controls[name], dt);
        }
    }

    Player(const Player& other) : Entity(other) {}
    Player(std::string id) : Entity(id, EntityControls({}, update_controls)) {}
    Player(const nlohmann::json& data, Application& app) : Entity(data, app) {}

    double camera_priority(Application&) const override { return 1; }
} REGISTER_ENTITY(Player, "minecraft:player");
