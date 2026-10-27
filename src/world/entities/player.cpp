#include <glm/trigonometric.hpp>
#include <world/entities/entity.hpp>
#include <base/fps_handler.hpp>
#include <base/controls.hpp>
#include <world/world.hpp>
#include "triolib.hpp"

using namespace std;
using namespace triolib;

struct Player : Entity {
    entityClass("minecraft:player", Player);

    double err_margin = 0.1;
    double speed = 2;
    double sensitivity = 0.5;
    double increased_speed = 10;

    nlohmann::json dump(Application& app) const override {
        nlohmann::json j = Entity::dump(app);
        j["err_margin"] = "player";
        j["speed"] = speed;
        j["sensitivity"] = sensitivity;
        return j;
    }

    static void update_controls(Entity& e, Application& app) {
        InputHandler& input_handler = app.getmod<InputHandler>();
        Controls& controls = app.getmod<Controls>();
        double dt = app.getmod<FpsHandler>().dt;

        for (auto& [name, control] : controls.controls) {
            if (PushCtl* push = dynamic_cast<PushCtl*>(&controls[name]))
                e.controls.input[name].update(push->get_pressure(), dt);
            else
                e.controls.input[name].update(*control, dt);
        }
    }

    void tick_controls(Application& app) override {
        Entity::tick_controls(app);
        const auto dt = app.getmod<FpsHandler>().dt;

        if (controls.input["sprint"])
            increased_speed += increased_speed * 2.0 * dt;
        else
            increased_speed = speed;
        double this_speed = speed * (controls.input["sprint"] ? increased_speed : 1);

        pos.y += controls.input["jump"].pressure * dt * this_speed;
        pos.y -= controls.input["sneak"].pressure * dt * this_speed;

        {
            double up = controls.input["look_up"].pressure - controls.input["look_down"].pressure;
            double right = controls.input["look_right"].pressure - controls.input["look_left"].pressure;

            // 0 = forward, -90 = down (-y), 90 = up (+y)
            pitch += up * sensitivity;
            if (pitch > 90 - err_margin) pitch = 90 - err_margin;
            if (pitch < err_margin - 90) pitch = err_margin - 90;

            // 0 = north (+x), 90 = east (+z), +-180 = south (-x), -90 = west (-z)
            yaw += right * sensitivity;
            yaw = std::fmod(yaw + 180.0, 360.0);
            if (yaw < 0) yaw += 360.0;
            yaw -= 180.0;
        }

        {
            double forward = controls.input["forward"].pressure - controls.input["backward"].pressure;
            double right = controls.input["right"].pressure - controls.input["left"].pressure;

            double len = std::hypot(forward, right);
            if (len > 1.0) {
                forward /= len;
                right /= len;
            }

            double rad_yaw = glm::radians(yaw);
            double s = glm::sin(rad_yaw), c = glm::cos(rad_yaw);

            pos.x += (forward * c - right * s) * this_speed * dt;
            pos.z += (forward * s + right * c) * this_speed * dt;
        }
    }

    Player(const Player& other) : Entity(other) {}
    Player(std::string id) : Entity(id, EntityControls({}, update_controls)) {
        pos.y = 5536.0;
        pitch = -30.0;
    }
    Player(const nlohmann::json& data, Application& app) : Entity(data, app) { controls.tick_func = update_controls; }

    double camera_priority(Application&) const override { return 1.0; }
} REGISTER_ENTITY(Player, "minecraft:player");

struct PlayerSpawner : triolib::Module {
    void init() override { parent->getmod<EntityList>().spawn_entity("minecraft:player", "p.MainPlayer"); }
    void loop() override {}
    void quit() override {}
} REGISTER_MOD(PlayerSpawner, World);
