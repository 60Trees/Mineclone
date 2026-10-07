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

    bool is_directly_in_directory(
        std::string_view path, std::string_view directory) {
        if (!is_in_directory(path, directory)) return false;

        std::string prefix(directory);
        if (!prefix.empty() && !prefix.ends_with('/')) prefix += '/';
        return path.find('/', prefix.size()) == std::string_view::npos;
    }

    std::vector<std::string> list_type(
        const Resources& resources, std::string_view directory,
        ResourceHandler::ResourceType type, bool recursive) {
        std::vector<std::string> result;
        for (const auto& [path, resource] : resources.get_resources()) {
            const auto source = source_path(path);
            const bool in_directory = recursive
                ? is_in_directory(source, directory)
                : is_directly_in_directory(source, directory);
            if (in_directory && resource.type == type) result.push_back(source);
        }
        std::sort(result.begin(), result.end());
        return result;
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

std::vector<std::string> Resources::list_pngs(
    std::string_view directory, bool recursive) const {
    return list_type(*this, directory, ResourceType::PNG, recursive);
}

std::vector<std::string> Resources::list_jsons(
    std::string_view directory, bool recursive) const {
    return list_type(*this, directory, ResourceType::JSON, recursive);
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

    for (const auto& [name, requested] : atlas_definitions) {
        std::vector<std::string> paths;
        for (const auto& path : requested) {
            const auto key = resource_key(path);
            const auto* resource = try_get(key);
            if (!resource)
                throw std::runtime_error(
                    "Atlas texture is missing: " + path);
            if (resource->type != ResourceType::PNG)
                throw std::runtime_error("Atlas texture is not PNG: " + path);
            paths.push_back(key);
        }

        // Reserve one texel for textures that cannot fit in this atlas.
        std::vector<stbrp_rect> rects;
        rects.push_back({-1, 1, 1, 0, 0, 0});
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const auto& image = get(paths[i]).image();
            rects.push_back({static_cast<int>(i), image.width, image.height,
                0, 0, 0});
        }

        unsigned int size = 1;
        std::vector<stbrp_rect> packed_rects;
        while (true) {
            packed_rects = rects;
            stbrp_context context;
            std::vector<stbrp_node> nodes(size);
            stbrp_init_target(&context, static_cast<int>(size),
                static_cast<int>(size), nodes.data(),
                static_cast<int>(nodes.size()));
            stbrp_pack_rects(&context, packed_rects.data(),
                static_cast<int>(packed_rects.size()));

            const bool all_packed = std::all_of(packed_rects.begin(),
                packed_rects.end(),
                [](const auto& rect) { return rect.was_packed; });
            if (all_packed || size == limit) break;
            size = std::min(limit, size * 2);
        }

        Atlas atlas;
        atlas.name = name;
        unsigned int width = 1;
        unsigned int height = 1;
        const auto& error_rect = packed_rects.front();
        width = std::max(width, static_cast<unsigned int>(
                                  error_rect.x + error_rect.w));
        height = std::max(height, static_cast<unsigned int>(
                                   error_rect.y + error_rect.h));

        for (const auto& rect : packed_rects) {
            if (!rect.was_packed) {
                console.err << "Texture cannot fit in atlas " << name << ": "
                            << paths[rect.id] << "; using error texture.";
                atlas.locations.emplace(paths[rect.id],
                    Rect{{static_cast<unsigned int>(error_rect.x),
                             static_cast<unsigned int>(error_rect.y)},
                        {static_cast<unsigned int>(error_rect.x + 1),
                            static_cast<unsigned int>(error_rect.y + 1)}});
                continue;
            }
            width = std::max(width, static_cast<unsigned int>(rect.x + rect.w));
            height =
                std::max(height, static_cast<unsigned int>(rect.y + rect.h));
        }

        atlas.image.width = static_cast<int>(width);
        atlas.image.height = static_cast<int>(height);
        atlas.image.pixels.assign(static_cast<std::size_t>(width) * height * 4,
            std::byte{0});
        atlas.image.pixels[0] = std::byte{255};
        atlas.image.pixels[1] = std::byte{0};
        atlas.image.pixels[2] = std::byte{255};
        atlas.image.pixels[3] = std::byte{255};

        for (const auto& rect : packed_rects) {
            if (!rect.was_packed || rect.id < 0) continue;
            const auto& path = paths[rect.id];
            const auto& image = get(path).image();
            for (int y = 0; y < rect.h; ++y)
                std::memcpy(atlas.image.pixels.data() +
                        (static_cast<std::size_t>(rect.y + y) * width + rect.x) *
                            4,
                    image.pixels.data() + static_cast<std::size_t>(y) * rect.w *
                                             4,
                    static_cast<std::size_t>(rect.w) * 4);
            atlas.locations.insert_or_assign(path,
                Rect{{static_cast<unsigned int>(rect.x),
                         static_cast<unsigned int>(rect.y)},
                    {static_cast<unsigned int>(rect.x + rect.w),
                        static_cast<unsigned int>(rect.y + rect.h)}});
        }
        atlases.insert_or_assign(name, std::move(atlas));
    }
}

const Atlas* Resources::get_atlas(std::string_view name) const {
    const auto it = atlases.find(std::string(name));
    return it == atlases.end() ? nullptr : &it->second;
}

std::optional<std::pair<const ResourceHandler::Image*, Rect>>
Resources::find_texture(std::string_view path) const {
    const auto key = resource_key(path);
    for (const auto& [name, atlas] : atlases)
        if (atlas.locations.contains(key))
            return std::pair{&atlas.image, atlas.locations.at(key)};
    return std::nullopt;
}
