#include <base/resources.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

#include <glad/gl.h>
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

using Resources = ResourceHandler::Resources;
using Atlas = Resources::Atlas;
using Rect = Resources::AtlasRect;

namespace {
    std::string resource_key(std::string_view path) {
        for (const auto prefix : {"assets/", "data/"})
            if (path.starts_with(prefix))
                return std::string(
                    path.substr(std::char_traits<char>::length(prefix)));
        return std::string(path);
    }

    std::string source_path(std::string_view key) {
        return "assets/" + std::string(key);
    }

    bool is_in_directory(std::string_view path, std::string_view directory) {
        std::string prefix(directory);
        if (!prefix.empty() && !prefix.ends_with('/')) prefix += '/';
        return path.starts_with(prefix);
    }
}  // namespace

const Rect& Resources::Atlas::operator[](std::string_view path) const {
    const auto it = locations.find(resource_key(path));
    if (it == locations.end())
        throw std::out_of_range(
            "Texture is not in atlas: " + std::string(path));
    return it->second;
}

std::vector<std::string> Resources::list_files(
    std::string_view directory) const {
    std::vector<std::string> result;
    for (const auto& [path, resource] : resources) {
        if (is_in_directory(source_path(path), directory))
            result.push_back(source_path(path));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> Resources::list_dirs(
    std::string_view directory) const {
    std::set<std::string> result;
    for (const auto& path : list_files(directory)) {
        auto remainder = path.substr(std::string(directory).size());
        if (!remainder.empty() && remainder.front() == '/')
            remainder.erase(0, 1);
        const auto slash = remainder.find('/');
        if (slash != std::string::npos)
            result.insert(std::string(directory) + remainder.substr(0, slash));
    }
    return {result.begin(), result.end()};
}

void Resources::define_atlas(std::string name, std::vector<std::string> paths) {
    atlas_definitions.emplace_back(std::move(name), std::move(paths));
}

void Resources::load_all_atlases() {
    GLint queried_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &queried_limit);
    const auto limit =
        queried_limit > 0 ? static_cast<unsigned int>(queried_limit) : 4096u;

    atlases.clear();
    std::set<std::string> assigned;

    auto pack = [&](const std::string& name,
                    const std::vector<std::string>& requested,
                    bool include_unassigned) {
        std::vector<std::string> pending;
        if (include_unassigned) {
            for (const auto& [path, resource] : resources)
                if (resource.type == ResourceType::PNG &&
                    !assigned.contains(path))
                    pending.push_back(path);
        } else {
            for (const auto& path : requested) {
                const auto key = resource_key(path);
                const auto* resource = try_get(key);
                if (!resource)
                    throw std::runtime_error(
                        "Atlas texture is missing or not PNG: " + path);
                if (resource->type != ResourceType::PNG) continue;
                pending.push_back(key);
            }
        }

        auto& output = atlases[name];
        while (!pending.empty()) {
            std::vector<stbrp_rect> rects;
            rects.reserve(pending.size());
            for (std::size_t i = 0; i < pending.size(); ++i) {
                const auto& image = get(pending[i]).image();
                if (image.width > static_cast<int>(limit) ||
                    image.height > static_cast<int>(limit))
                    throw std::runtime_error(
                        "Texture exceeds maximum atlas size: " + pending[i]);
                rects.push_back(
                    {static_cast<int>(i), image.width, image.height, 0, 0, 0});
            }

            stbrp_context context;
            std::vector<stbrp_node> nodes(limit);
            stbrp_init_target(&context, static_cast<int>(limit),
                static_cast<int>(limit), nodes.data(),
                static_cast<int>(nodes.size()));
            stbrp_pack_rects(
                &context, rects.data(), static_cast<int>(rects.size()));

            unsigned int width = 0;
            unsigned int height = 0;
            std::vector<std::string> packed;
            for (const auto& rect : rects) {
                if (!rect.was_packed) continue;
                packed.push_back(pending[rect.id]);
                width =
                    std::max(width, static_cast<unsigned int>(rect.x + rect.w));
                height = std::max(
                    height, static_cast<unsigned int>(rect.y + rect.h));
            }
            if (packed.empty())
                throw std::runtime_error("Failed to pack atlas textures.");

            Atlas atlas;
            atlas.name = name;
            atlas.image.width = static_cast<int>(width);
            atlas.image.height = static_cast<int>(height);
            atlas.image.pixels.resize(
                static_cast<std::size_t>(width) * height * 4);

            for (const auto& rect : rects) {
                if (!rect.was_packed) continue;
                const auto& path = pending[rect.id];
                const auto& image = get(path).image();
                for (int y = 0; y < rect.h; ++y)
                    std::memcpy(
                        atlas.image.pixels.data() +
                            (static_cast<std::size_t>(rect.y + y) * width +
                                rect.x) *
                                4,
                        image.pixels.data() +
                            static_cast<std::size_t>(y) * rect.w * 4,
                        static_cast<std::size_t>(rect.w) * 4);
                atlas.locations.emplace(
                    path, Rect{{static_cast<unsigned int>(rect.x),
                                   static_cast<unsigned int>(rect.y)},
                              {static_cast<unsigned int>(rect.x + rect.w),
                                  static_cast<unsigned int>(rect.y + rect.h)}});
                assigned.insert(path);
            }
            output.push_back(std::move(atlas));
            pending.erase(
                std::remove_if(pending.begin(), pending.end(),
                    [&](const auto& path) { return assigned.contains(path); }),
                pending.end());
        }
    };

    for (const auto& [name, paths] : atlas_definitions)
        pack(name, paths, false);
    pack("spare_atlas", {}, true);
}

const std::vector<Atlas>& Resources::get_atlases(std::string_view name) const {
    static const std::vector<Atlas> empty;
    const auto it = atlases.find(std::string(name));
    return it == atlases.end() ? empty : it->second;
}

std::optional<std::pair<const ResourceHandler::Image*, Rect>>
Resources::find_texture(std::string_view path) const {
    const auto key = resource_key(path);
    for (const auto& [name, values] : atlases)
        for (const auto& atlas : values)
            if (atlas.locations.contains(key))
                return std::pair{&atlas.image, atlas.locations.at(key)};
    return std::nullopt;
}
