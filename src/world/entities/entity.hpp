#pragma once

#include <functional>
#include <glm/common.hpp>
#include <memory>
#include <registry/registry.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <triolib.hpp>
#include <base/input_handler.hpp>

using EntityIDX = std::string;
using EntityIDX_view = std::string_view;

struct Entity;
struct EntityList;

/**
 * @brief Used for things like players (pipe the keyboard directly to this) or Mob AI.
 */
struct EntityControls {
    std::map<std::string, InputState> input;
    std::function<void(Entity&, triolib::Application&)> tick_func = [](Entity&, triolib::Application&) {};

    inline const InputState& operator[](const std::string& name) { return input[name]; }

    private:
    constexpr static InputState null_input_state = {};
};

struct Entity {
    virtual ~Entity() = default;

    Entity(std::string id, EntityControls&& controls);
    Entity(std::string id);
    Entity(const Entity& other);
    Entity(const nlohmann::json& data, triolib::Application&);

    virtual void spawn(triolib::Application& app) {}
    virtual void tick_all(triolib::Application& app) {
        tick_controls(app);
        tick_physics(app);
        tick(app);
    }
    virtual void tick_controls(triolib::Application& app);
    virtual void tick_physics(triolib::Application& app);
    virtual void tick(triolib::Application& app) {}
    virtual void render(triolib::Application&) {}

    /// @brief The entity with the highest camera priority will be followed by the camera.
    /// @note -1 = never follows, 0 = default (can follow but doesn't want to), >0 = wants to be followed.
    virtual double camera_priority(triolib::Application&) const { return 0; }
    /// @brief Called when the camera is following this entity.
    /// @detail Called every frame that the camera is following this entity.
    /// Default camera stuff (like following and looking around) is handled by the engine.
    virtual void camera_on_follow(triolib::Application&) {}

    const std::string id;

    /// @brief Clones an entity from another entity
    /// @note Feel free to assert if the other entity is not
    /// the same type
    virtual std::unique_ptr<Entity> clone(const Entity& other) = 0;
    virtual std::string vname() const { return typeid(*this).name(); }

    /**
     * @brief Called every time another entity despawns.
     * @param other_index The index of the entity that died.
     */
    virtual void on_other_entity_despawn(EntityIDX_view other_index, EntityList& entity_list) {}

    /**
     * @detail JSON contents:
     *
     * - id: string, required (e.g "minecraft:player")
     * - index: string, required (`this->index`)
     * - refs: map<string, string>, optional (Automatically set to entity_index_null upon death)
     */
    virtual nlohmann::json dump(triolib::Application& app) const;

    EntityControls controls;

    /// In blocks
    glm::vec<3, double> pos{0, 0, 0};
    /// In blocks
    glm::vec<3, double> vel{0, 0, 0};
    /// In blocks
    glm::vec<3, double> hitbox{1, 1, 1};

    /// Pitch (-90 to 90): 0 = forward, -90 = looking down, 90 = looking up
    double yaw;
    /// Yaw (-180 to 180): 0 = north, 90 = east, +-180 = south, -90 = west
    double pitch;

    /**
     * @detail If true, this entity will
     * - Not render
     * - Not tick
     * - Not be saved to the world file
     * - Be cleaned and removed from the entity list asap
     * - All unloaded references (marked with `refs` in the json) will be set to `Entity::nullindex` in the world file
     * - All loaded entities will have their `on_other_entity_despawn` function called with this entity's index
     */
    bool wants_to_despawn = false;

    /**
     * @detail If true, this entity will
     * - Not render
     * - Not tick
     * - Be saved to the world file asap
     *
     * Example: A persistent entity that is too far away from the player (i.e villagers or pigs)
     */
    bool wants_to_unload = false;

    /**
     * @detail If this variable is false, the entity
     * will despawn when a player is not nearby
     */
    bool persistent = true;

    EntityIDX index;
    constexpr static EntityIDX nullindex = "";
};

struct EntityRegistry : Registry {
    tClass;

    std::string get_regid() const override { return "minecraft:entity"; }
};

struct EntityRegEntry : RegistryEntry {
    virtual std::unique_ptr<Entity> spawn(EntityIDX index) = 0;
    virtual std::unique_ptr<Entity> spawn(const nlohmann::json& data, triolib::Application& app) = 0;
};

struct EntityList : triolib::Module {
    /**
     * @detail
     * Loaded entities only.
     */
    std::map<EntityIDX, std::unique_ptr<Entity>> loaded_entities{};
    std::map<EntityIDX, nlohmann::json> saved_entities{};

    /// Returns an index that is not reserved and not in use.
    virtual EntityIDX alloc_index() = 0;

    /**
     * @brief Spawns an entity.
     * @return The index of the spawned entity.
     */
    virtual EntityIDX spawn_entity(std::string_view id) = 0;
    /**
     * @brief Spawns an entity with a specific index.
     * @note Assumes the index is not already in use.
     */
    virtual void spawn_entity(std::string_view id, EntityIDX index) = 0;

    /**
     * @brief Loads an entity from the saved_entities map.
     * If the entity is already loaded, this function does nothing.
     * @param index The index of the entity to load.
     * @throws EntityNotFound if the entity is not found in the saved_entities map.
     */
    virtual void load_entity(EntityIDX index) = 0;
    struct Error : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
};

struct EntityNotFound : std::out_of_range {
    using std::out_of_range::out_of_range;
};
struct BadEntityRegistry : EntityList::Error {
    using EntityList::Error::Error;
};

#define entityClass(_id, classname)                                             \
    tClass;                                                                     \
    inline static std::string static_id() { return _id; }                       \
    std::unique_ptr<Entity> clone(const Entity& other) override {               \
        const classname* other_player = dynamic_cast<const classname*>(&other); \
        tassert(other_player);                                                  \
                                                                                \
        return make_unique<classname>(*other_player);                           \
    }

#define defaultEntityClass(_id, classname)       \
    entityClass(_id, classname);                 \
    classname(const classname& other) = default; \
    classname() = default;                       \
    classname(const nlohmann::json& data) {}

#define REGISTER_ENTITY(classname, __resid)                                                             \
    ;                                                                                                   \
    /**                                                                                                 \
     * Needs the class to have three constructors:                                                      \
     * classname(const classname& other)                                                                \
     * classname()                                                                                      \
     * classname(const nlohmann::json& data)                                                            \
     * Or it can use the `defaultEntityClass` macro, which will generate these constructors for you.    \
     */                                                                                                 \
    struct classname##RegEntry : EntityRegEntry {                                                       \
        std::unique_ptr<Entity> spawn(EntityIDX index) override {                                       \
            auto retval = make_unique<classname>(__resid);                                              \
            retval->index = std::move(index);                                                           \
            return retval;                                                                              \
        }                                                                                               \
                                                                                                        \
        std::unique_ptr<Entity> spawn(const nlohmann::json& data, triolib::Application& app) override { \
            auto retval = make_unique<classname>(data, app);                                            \
            retval->index = data.value("index", Entity::nullindex);                                     \
            return retval;                                                                              \
        }                                                                                               \
    } RESENTRY(classname##RegEntry, __resid, "minecraft:entity")
