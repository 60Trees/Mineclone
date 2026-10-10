#include "block.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace assets::models {
    namespace {
        using json = nlohmann::json;

        // Convert a three-number JSON array into a GLM vector.
        glm::vec3 vec3_from_json(const json& value, const glm::vec3& fallback) {
            if (!value.is_array() || value.size() != 3) {
                throw std::runtime_error("Expected a JSON array containing three numbers");
            }

            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }

        // Normalize a resource location to a model JSON resource path.
        std::string model_path_from_id(std::string_view id) {
            if (id.starts_with("assets/")) {
                std::string path(id);

                if (!path.ends_with(".json")) {
                    path += ".json";
                }

                return path;
            }

            std::string resource(id);
            const auto colon = resource.find(':');

            std::string name_space;
            std::string path;

            if (colon == std::string::npos) {
                name_space = "minecraft";
                path = resource;
            } else {
                name_space = resource.substr(0, colon);
                path = resource.substr(colon + 1);
            }

            if (name_space.empty() || path.empty()) {
                throw std::runtime_error("Invalid model resource location: " + resource);
            }

            return "assets/" + name_space + "/models/" + path + ".json";
        }

        std::string parent_path_from_id(std::string_view id) { return model_path_from_id(id); }

        // Deep-merge objects. Arrays and scalar values are replaced.
        void merge_json(json& destination, const json& source) {
            if (!destination.is_object() || !source.is_object()) {
                destination = source;
                return;
            }

            for (auto it = source.begin(); it != source.end(); ++it) {
                const auto key = it.key();

                if (destination.contains(key) && destination[key].is_object() && it.value().is_object()) {
                    merge_json(destination[key], it.value());
                } else {
                    destination[key] = it.value();
                }
            }
        }

        // Resolve a model's parents, merging the JSON before parsing it.
        json resolve_model_json(triolib::Application* app, const std::string& path, std::set<std::string>& visiting) {
            if (!visiting.insert(path).second) {
                throw std::runtime_error("Cyclic block model parent chain at: " + path);
            }

            json child = get_json_from_path(app, path);

            if (!child.is_object()) {
                visiting.erase(path);
                throw std::runtime_error("Block model root must be a JSON object: " + path);
            }

            json result = json::object();

            if (child.contains("parent")) {
                const std::string parent = child.at("parent").get<std::string>();

                // Built-in models are not ordinary resource JSON files.
                // They have no JSON parent to load here.
                if (parent != "builtin/generated" && parent != "builtin/entity") {
                    const std::string parent_path = parent_path_from_id(parent);

                    result = resolve_model_json(app, parent_path, visiting);
                }
            }

            // Keep the child's parent field for inspection/debugging.
            // Its other supplied fields override inherited values.
            merge_json(result, child);

            visiting.erase(path);
            return result;
        }

        Block::Display::Position parse_display_position(const json& value) {
            Block::Display::Position result;

            if (value.contains("rotation")) {
                result.rotation = vec3_from_json(value.at("rotation"), result.rotation);
            }

            if (value.contains("translation")) {
                result.translation = vec3_from_json(value.at("translation"), result.translation);

                for (int i = 0; i < 3; ++i) {
                    result.translation[i] = std::clamp(result.translation[i], -80.0f, 80.0f);
                }
            }

            if (value.contains("scale")) {
                result.scale = vec3_from_json(value.at("scale"), result.scale);

                for (int i = 0; i < 3; ++i) {
                    result.scale[i] = std::min(result.scale[i], 4.0f);
                }
            }

            return result;
        }

        void parse_display_context(const json& display, const char* key, Block::Display::Position& destination) {
            if (display.contains(key)) {
                destination = parse_display_position(display.at(key));
            }
        }

        Block::Display parse_display(const json& value) {
            Block::Display result;

            parse_display_context(value, "thirdperson_righthand", result.thirdperson_righthand);
            parse_display_context(value, "thirdperson_lefthand", result.thirdperson_lefthand);
            parse_display_context(value, "firstperson_righthand", result.firstperson_righthand);
            parse_display_context(value, "firstperson_lefthand", result.firstperson_lefthand);
            parse_display_context(value, "gui", result.gui);
            parse_display_context(value, "head", result.head);
            parse_display_context(value, "ground", result.ground);
            parse_display_context(value, "fixed", result.fixed);
            parse_display_context(value, "on_shelf", result.on_shelf);

            return result;
        }

        Block::Textures parse_textures(const json& value) {
            Block::Textures result;

            for (auto it = value.begin(); it != value.end(); ++it) {
                const std::string& name = it.key();
                const json& texture = it.value();

                if (texture.is_string()) {
                    result.texture_variables[name] = texture.get<std::string>();
                    continue;
                }

                if (texture.is_object()) {
                    Block::Textures::TextureVariable variable;

                    if (!texture.contains("sprite")) {
                        throw std::runtime_error("Texture variable '" + name + "' is missing its sprite property");
                    }

                    variable.sprite = texture.at("sprite").get<std::string>();

                    variable.force_translucent = texture.value("force_translucent", false);

                    result.texture_variables[name] = variable;
                    continue;
                }

                throw std::runtime_error("Invalid texture variable: " + name);
            }

            return result;
        }

        Block::Element::Face parse_face(const json& value) {
            Block::Element::Face result;

            if (value.contains("uv")) {
                const auto& uv = value.at("uv");

                if (!uv.is_array() || uv.size() != 4) {
                    throw std::runtime_error("Face UV must contain four numbers");
                }

                result.uv = glm::mat2x2(glm::vec2(uv[0].get<float>(), uv[1].get<float>()),
                    glm::vec2(uv[2].get<float>(), uv[3].get<float>()));
            }

            result.texture = value.at("texture").get<std::string>();

            if (value.contains("cullface")) {
                result.cullface = value.at("cullface").get<std::string>();
            }

            result.rotation = value.value("rotation", 0);
            result.tintindex = value.value("tintindex", -1);

            return result;
        }

        Block::Element parse_element(const json& value) {
            Block::Element result;

            if (value.contains("from")) {
                result.from = vec3_from_json(value.at("from"), result.from);
            }

            if (value.contains("to")) {
                result.to = vec3_from_json(value.at("to"), result.to);
            }

            if (value.contains("rotation")) {
                const auto& rotation = value.at("rotation");

                if (rotation.contains("origin")) {
                    result.rotation.origin = vec3_from_json(rotation.at("origin"), result.rotation.origin);
                }

                result.rotation.x = rotation.value("x", 0.0f);
                result.rotation.y = rotation.value("y", 0.0f);
                result.rotation.z = rotation.value("z", 0.0f);

                result.rotation.axis = rotation.value("axis", "");
                result.rotation.angle = rotation.value("angle", 0.0f);
                result.rotation.rescale = rotation.value("rescale", false);
            }

            if (value.contains("shade_direction_override")) {
                result.shade_direction_override = value.at("shade_direction_override").get<std::string>();
            }

            const int emission = value.value("light_emission", 0);

            if (emission < 0 || emission > 15) {
                throw std::runtime_error("Element light_emission must be between 0 and 15");
            }

            result.light_emission = static_cast<uint8_t>(emission);

            if (value.contains("faces")) {
                const auto& faces = value.at("faces");

                constexpr std::array<const char*, 6> names = {"down", "up", "north", "south", "west", "east"};

                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (faces.contains(names[i])) {
                        result.faces[i] = parse_face(faces.at(names[i]));
                    }
                }
            }

            return result;
        }

        Block parse_resolved_model(const json& value) {
            Block result;

            result.parent = value.value("parent", std::string{});
            result.ambientocclusion = value.value("ambientocclusion", true);

            if (value.contains("display")) {
                result.display = parse_display(value.at("display"));
            }

            if (value.contains("textures")) {
                result.textures = parse_textures(value.at("textures"));
            }

            if (value.contains("elements")) {
                const auto& elements = value.at("elements");

                if (!elements.is_array()) {
                    throw std::runtime_error("Model elements must be a JSON array");
                }

                result.elements.reserve(elements.size());

                for (const auto& element : elements) {
                    result.elements.push_back(parse_element(element));
                }
            }

            return result;
        }
    }  // namespace

    Block get_block_model_path(triolib::Application* app, std::string_view path) {
        if (app == nullptr) {
            throw std::invalid_argument("get_block_model_path received a null application");
        }

        std::string normalized(path);

        if (!normalized.ends_with(".json")) {
            normalized += ".json";
        }

        std::set<std::string> visiting;
        const json resolved = resolve_model_json(app, normalized, visiting);

        return parse_resolved_model(resolved);
    }

    Block get_block_model_id(triolib::Application* app, std::string_view resid) {
        return get_block_model_path(app, model_path_from_id(resid));
    }

}  // namespace assets::models
