#pragma once

#include <array>
#include <base/resources.hpp>
#include <cstdint>
#include <glm/ext/matrix_float2x2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <map>
#include <vector>
#include "triolib.hpp"

namespace assets::models {
    struct Block {
        std::string parent;
        bool ambientocclusion = true;

        struct Display {
            struct Position {
                glm::vec3 rotation{0.0f};
                glm::vec3 translation{0.0f};
                glm::vec3 scale{1.0f};
            };

            Position thirdperson_righthand;
            Position thirdperson_lefthand;
            Position firstperson_righthand;
            Position firstperson_lefthand;
            Position gui;
            Position head;
            Position ground;
            Position fixed;
            Position on_shelf;
        } display;

        struct Textures {
            struct TextureVariable {
                std::string sprite;
                bool force_translucent = false;
            };

            std::map<
                std::string,
                std::variant<std::string, TextureVariable>
            > texture_variables;
        } textures;

        struct Element {
            glm::vec3 from{0.0f};
            glm::vec3 to{16.0f};

            struct Rotation {
                glm::vec3 origin{8.0f};
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                std::string axis;
                float angle = 0.0f;
                bool rescale = false;
            } rotation;

            std::optional<std::string> shade_direction_override;
            uint8_t light_emission = 0;

            struct Face {
                std::optional<glm::mat2x2> uv;
                std::string texture;
                std::optional<std::string> cullface;
                int rotation = 0;
                int tintindex = -1;
            };

            // Order: down, up, north, south, west, east.
            std::array<Face, 6> faces{};
        };

        std::vector<Element> elements;
    };

    inline nlohmann::json get_json_from_path(
        triolib::Application* app,
        std::string_view path
    ) {
        return app->getmod<ResourceHandler>().resources.get(path).json();
    }

    // e.g. minecraft:block/stone
    Block get_block_model_id(
        triolib::Application* app,
        std::string_view resid
    );

    // e.g. assets/minecraft/models/block/stone.json
    Block get_block_model_path(
        triolib::Application* app,
        std::string_view path
    );
}
