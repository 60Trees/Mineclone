#include "main.hpp"
#include <string_view>
#include <triolib.hpp>
#include <world/server/main.hpp>
#include <random>
#include <base/controls.hpp>
#include <mutex>

nlohmann::json any_to_json(const std::any& value) {
    if (!value.has_value()) return nullptr;

    const auto& type = value.type();

    if (type == typeid(std::nullptr_t)) return nullptr;
    if (type == typeid(bool)) return std::any_cast<bool>(value);

    if (type == typeid(int)) return std::any_cast<int>(value);
    if (type == typeid(unsigned int)) return std::any_cast<unsigned int>(value);
    if (type == typeid(std::int64_t)) return std::any_cast<std::int64_t>(value);
    if (type == typeid(std::uint64_t)) return std::any_cast<std::uint64_t>(value);

    if (type == typeid(float)) return std::any_cast<float>(value);
    if (type == typeid(double)) return std::any_cast<double>(value);

    if (type == typeid(std::string)) return std::any_cast<const std::string&>(value);

    if (type == typeid(std::vector<std::any>)) {
        nlohmann::json result = nlohmann::json::array();

        for (const auto& element : std::any_cast<const std::vector<std::any>&>(value)) {
            result.push_back(any_to_json(element));
        }

        return result;
    }

    if (type == typeid(std::map<std::string, std::any>)) {
        nlohmann::json result = nlohmann::json::object();

        for (const auto& [key, element] : std::any_cast<const std::map<std::string, std::any>&>(value)) {
            result[key] = any_to_json(element);
        }

        return result;
    }

    throw std::runtime_error(std::string("Unsupported std::any type: ") + type.name());
}

std::any json_to_any(const nlohmann::json& value) {
    if (value.is_null()) return nullptr;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<std::int64_t>();
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_float()) return value.get<double>();
    if (value.is_string()) return value.get<std::string>();

    if (value.is_array()) {
        std::vector<std::any> result;
        result.reserve(value.size());

        for (const auto& element : value) result.push_back(json_to_any(element));

        return result;
    }

    if (value.is_object()) {
        std::map<std::string, std::any> result;

        for (auto it = value.begin(); it != value.end(); ++it) result[it.key()] = json_to_any(it.value());

        return result;
    }

    throw std::runtime_error("Unsupported JSON value");
}

using namespace std;
using namespace triolib;
using namespace glm;

void Entity::tick_physics(triolib::Application& app) {}
void Entity::tick_controls(triolib::Application& app) { controls.tick_func(*this, app); }

namespace {
    inline std::string extract_id(const nlohmann::json& data) {
        try {
            return data["id"].get<std::string>();
        } catch (std::exception& e) {
            console.err << "Error while loading entity from JSON: " << data.dump() << "\n" << e.what();
            throw;
        };
    }
}  // namespace

Entity::Entity(const nlohmann::json& data, Application& app) : id(extract_id(data)) {
    try {
        if (data.contains("flags")) {
            // this->flags: std::map<std::string, std::any>
            auto flags = data["flags"];
            for (auto& [key, value] : flags.items()) {
                this->flags[key] = json_to_any(value);
            }
        }
        index = data["index"].get<EntityIDX>();
        vel = {data["vel"][0].get<double>(), data["vel"][1].get<double>(), data["vel"][2].get<double>()};
        persistent = data["persistent"].get<bool>();
        {
            const auto& pos_value = data["pos"]["value"];
            pos = {pos_value[0].get<double>(), pos_value[1].get<double>(), pos_value[2].get<double>()};
        }
    } catch (const exception& e) {
        console.err << "Error while loading entity from JSON: " << data.dump() << "\n" << e.what();
        throw;
    }
}
Entity::Entity(std::string id, EntityControls&& controls) : id(id), controls(controls) {}
Entity::Entity(std::string id) : id(id) {}
Entity::Entity(const Entity& other)
    : id(other.id),
      controls(other.controls),
      pos(other.pos),
      vel(other.vel),
      hitbox(other.hitbox),
      yaw(other.yaw),
      pitch(other.pitch),
      wants_to_despawn(other.wants_to_despawn),
      wants_to_unload(other.wants_to_unload),
      persistent(other.persistent),
      index(other.index) {}

EntityRegEntry* get_entityregentry(Application& app, string_view id) {
    auto& entityreg = app.get_subapp<RegistryList>().getmod<EntityRegistry>();
    if (!entityreg.has_entry(id)) throw EntityNotFound("Entity ID " + string(id) + " not found in EntityRegistry.");

    auto* rawentityregentry = &entityreg.get_entry(id);
    if (!rawentityregentry)
        throw BadEntityRegistry(string("EntityRegistry.get_entry(\"" + string(id) + "\") returned null!"));
    auto* entityregentry = dynamic_cast<EntityRegEntry*>(rawentityregentry);

    if (!entityregentry)
        throw BadEntityRegistry(string("EntityRegistry.get_entry(\"" + string(id) + "\") returns a " +
                                       get_dynamic_name<RegistryEntry>(*rawentityregentry) +
                                       ", which cannot be casted to EntityRegEntry!"));

    return entityregentry;
}
nlohmann::json Entity::dump(Application& app) const {
    auto& world = app.get_subapp<WorldServer>();

    nlohmann::json jflags;
    for (const auto& [key, value] : this->flags) jflags[key] = any_to_json(value);

    return {{"id", id}, {"index", index},
        {"pos", {{"offset", {"0", "0", "0"}},
                    {"value", {pos.x, pos.y, pos.z}}}},
        {"vel", {vel.x, vel.y, vel.z}}, {"persistent", persistent}, {"flags", jflags}};
}

struct EntityListImpl : EntityList {
    tImplClass(EntityList);

    virtual void load_entity(EntityIDX index) override {
        if (loaded_entities.contains(index)) return;
        if (!saved_entities.contains(index)) throw out_of_range("Saved entity " + index + " not found.");
        auto& entityjson = saved_entities.at(index);
        if (!entityjson.contains("id")) throw runtime_error("Entity JSON does not contain id: " + entityjson.dump());
        auto id = entityjson["id"].get<string>();
        auto* entityregentry = get_entityregentry(*parent->application, id);
        auto entity = entityregentry->spawn(entityjson, *parent->application);
        if (!entity) throw BadEntityRegistry("While loading " + id + ": registry spawn returned nullptr.");
        entity->index = index;
        entity->spawn(*parent->application);
        loaded_entities.emplace(index, std::move(entity));
        saved_entities.erase(index);
    }

    virtual void remove_entity_refs(nlohmann::json& refs, EntityIDX_view index) {
        if (refs.is_string()) {
            if (refs.get_ref<const string&>() == index) {
                refs = Entity::nullindex;
            }
            return;
        }

        if (!refs.is_structured()) return;

        for (auto& i : refs) {
            remove_entity_refs(i, index);
        }
    }

    virtual void remove_entity_refs(EntityIDX_view index) {
        for (auto& [idx, entityjson] : saved_entities) {
            if (!entityjson.contains("refs")) continue;
            remove_entity_refs(entityjson["refs"], index);
        }
    }

    void init() override {}
    void loop() override {
        vector<EntityIDX> to_remove = {};

        float highest_cam_priority = -1;
        Entity* cam_entity = nullptr;

        for (auto& [idx, entity] : loaded_entities) {
            if (!entity) {
                console.warn << "Mystery entity at index " << idx << " is null, will remove.";
                to_remove.push_back(idx);
                continue;
            }
            if (idx != entity->index) {
                console.warn << "Entity " + entity->id + " has mismatched indexes: (map)" << idx << " != (entity.idx)"
                             << entity->index << ", will remove.";
                to_remove.push_back(idx);
                continue;
            }
            if (idx == Entity::nullindex) {
                console.warn << "Entity " + entity->id + " has null index, will remove.";
                to_remove.push_back(idx);
                continue;
            }

            entity->tick_all(*parent->application);

            if (entity->wants_to_despawn) {
                for (auto& [other_idx, other_entity] : loaded_entities)
                    if (other_idx != idx && other_entity) other_entity->on_other_entity_despawn(idx, *this);

                remove_entity_refs(idx);
                to_remove.push_back(idx);
                continue;
            }
            if (entity->wants_to_unload) {
                saved_entities[idx] = entity->dump(*parent->application);
                to_remove.push_back(idx);
                continue;
            }

            if (entity->camera_priority(*parent->application) > highest_cam_priority) {
                highest_cam_priority = entity->camera_priority(*parent->application);
                cam_entity = entity.get();
            }
        }

        for (auto& idx : to_remove) loaded_entities.erase(idx);
        if (cam_entity) {
            auto* worldptr = dynamic_cast<WorldServer*>(parent);
            tassertmsgerr(worldptr, "EntityList is not a child of World", logic_error);
            auto& world = *worldptr;

            {
                std::unique_lock __(world.camera_mutex);

                world.render_camera.x = cam_entity->pos.x;
                world.render_camera.y = cam_entity->pos.y;
                world.render_camera.z = cam_entity->pos.z;
                world.render_camera.yaw = cam_entity->yaw;
                world.render_camera.pitch = cam_entity->pitch;

                if (!parent->application->getmod<Controls>().get<ToggleCtl>("detach_camera")(0)) {
                    world.camera = world.render_camera;
                }
            }

            cam_entity->camera_on_follow(*parent->application);
        }
    }
    void quit() override {}

    EntityIDX alloc_index() override {
        constexpr static string_view characters = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM1234567890";
        thread_local static uniform_int_distribution<int> distrib(0, characters.size() - 1);
        thread_local static random_device rd;
        thread_local static mt19937 gen(rd());

        EntityIDX retval = "e.";
        while (retval.empty() || loaded_entities.contains(retval) || saved_entities.contains(retval)) {
            retval = retval + characters[distrib(gen)];
        }

        return retval;
    }

    EntityIDX spawn_entity(string_view id) override {
        EntityIDX index = alloc_index();
        spawn_entity(id, index);
        return index;
    }

    void spawn_entity(string_view id, EntityIDX index) override {
        auto& entityreg = parent->application->get_subapp<RegistryList>().getmod<EntityRegistry>();
        if (!entityreg.has_entry(id))
            throw EntityNotFound(
                "Entity ID " + string(id) + " not found in EntityRegistry. (index allocated: " + index + ")");

        auto* rawentityregentry = &entityreg.get_entry(id);
        if (!rawentityregentry)
            throw BadEntityRegistry(string(
                "EntityRegistry.get_entry(\"" + string(id) + "\") returned null! (index allocated: " + index + ")"));
        auto* entityregentry = dynamic_cast<EntityRegEntry*>(rawentityregentry);

        if (!entityregentry)
            throw BadEntityRegistry(
                string("EntityRegistry.get_entry(\"" + string(id) + "\") returns a " +
                       get_dynamic_name<RegistryEntry>(*rawentityregentry) +
                       ", which cannot be casted to EntityRegEntry! (index allocated: " + index + ")"));

        if (index.empty()) throw std::invalid_argument("Cannot spawn entity with an empty index.");
        if (loaded_entities.contains(index) || saved_entities.contains(index))
            throw std::logic_error("Entity index already in use: " + index);

        auto entity = entityregentry->spawn(index);
        if (!entity)
            throw BadEntityRegistry(
                string("While spawning " + string(id) + ": " + get_dynamic_name<RegistryEntry>(*entityregentry) +
                       ".spawn(\"" + index + "\") returned nullptr!"));
        entity->spawn(*parent->application);
        loaded_entities[index] = std::move(entity);
    }
} REGISTER_MOD(EntityListImpl, WorldServer);

struct EntityRegistryImpl : EntityRegistry {
    registryClass("minecraft:entity");
} REGISTRY(EntityRegistryImpl);
