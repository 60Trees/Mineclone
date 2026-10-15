#include "entity.hpp"
#include <string_view>
#include <triolib.hpp>
#include <world/world.hpp>
#include <random>

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
        index = data["index"].get<EntityIDX>();
        vel = {data["vel"][0].get<double>(), data["vel"][1].get<double>(), data["vel"][2].get<double>()};
        persistent = data["persistent"].get<bool>();
        {
            const BigPos& world_offset = app.get_subapp<World>().offset;
            // [x, y, z] strings
            const auto& offset = data["pos"]["offset"];
            // [x, y, z] doubles
            const auto& pos_value = data["pos"]["value"];
            BigPos entity_offset = {
                BigInt(offset[0].get<string>()),
                BigInt(offset[1].get<string>()),
                BigInt(offset[2].get<string>()),
            };
            pos = {
                pos_value[0].get<double>() + (entity_offset.x - world_offset.x).convert_to<double>(),
                pos_value[1].get<double>() + (entity_offset.y - world_offset.y).convert_to<double>(),
                pos_value[2].get<double>() + (entity_offset.z - world_offset.z).convert_to<double>(),
            };
        }
    } catch (const exception& e) {
        console.err << "Error while loading entity from JSON: " << data.dump() << "\n" << e.what();
        throw;
    }
}
Entity::Entity(std::string id, EntityControls&& controls) : id(id), controls(controls) {}
Entity::Entity(std::string id) : id(id) {}
Entity::Entity(const Entity& other) : controls(other.controls) {}

EntityRegEntry* get_entityregentry(Application& app, string_view id) {
    auto& entityreg = app.get_subapp<RegistryList>().getmod<EntityRegistry>();
    if (!entityreg.has_entry(id)) throw EntityNotFound("Entity ID " + string(id) + " not found in EntityRegistry.");

    auto* rawentityregentry = &entityreg.get_entry(id);
    if (!rawentityregentry)
        throw BadEntityRegistry(string("EntityRegistry.get_entry(\"" + string(id) + "\") returned null!"));
    auto* entityregentry = dynamic_cast<EntityRegEntry*>(rawentityregentry);

    if (!entityregentry)
        throw BadEntityRegistry(
            string("EntityRegistry.get_entry(\"" + string(id) + "\") returns a " +
                   get_dynamic_name<RegistryEntry>(*entityregentry) + ", which cannot be casted to EntityRegEntry!"));

    return entityregentry;
}
nlohmann::json Entity::dump(Application& app) const {
    auto& world = app.get_subapp<World>();

    return {
        {"id", id},
        {"index", index},
        {"pos", {{"offset", {world.offset.x.str(), world.offset.y.str(), world.offset.z.str()}},
                    {"value", {pos.x, pos.y, pos.z}}}},
        {"vel", {vel.x, vel.y, vel.z}},
        {"persistent", persistent},
    };
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
        spawn_entity(id, index);
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
                    if (other_idx != idx) other_entity->on_other_entity_despawn(idx, *this);

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
            auto* worldptr = dynamic_cast<World*>(parent);
            tassertmsgerr(worldptr, "EntityList is not a child of World", logic_error);
            auto& world = *worldptr;

            world.camera.x = cam_entity->pos.x;
            world.camera.y = cam_entity->pos.y;
            world.camera.z = cam_entity->pos.z;
            world.camera.yaw = cam_entity->yaw;
            world.camera.pitch = cam_entity->pitch;

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
                       get_dynamic_name<RegistryEntry>(*entityregentry) +
                       ", which cannot be casted to EntityRegEntry! (index allocated: " + index + ")"));

        auto entity = entityregentry->spawn(index);
        if (!entity)
            throw BadEntityRegistry(
                string("While spawning " + string(id) + ": " + get_dynamic_name<RegistryEntry>(*entityregentry) +
                       ".spawn(\"" + index + "\") returned nullptr!"));
        loaded_entities[index] = std::move(entity);
    }
} REGISTER_MOD(EntityListImpl, World);
