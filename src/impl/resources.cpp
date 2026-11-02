// <AI>
#include <base/resources.hpp>
#include <cmrc/cmrc.hpp>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <triolib.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

CMRC_DECLARE(assets);

using namespace std;

struct ResourceHandlerImpl : ResourceHandler {
    tImplClass(ResourceHandler);

    virtual void start_loading() override {
        console.debug << "Starting to load resources...";

        auto start = chrono::high_resolution_clock::now();
        has_started_loading = true;
        has_loaded = false;
        resources.whitelist.clear();

        for (const auto& folder : this->whitelist) {
            resources.whitelist.push_back(folder);
        }

        for (const auto& [origin, type] : resource_origins) {
            switch (type) {
                case ResourceOriginType::ZipFromFs:
                    resources.load(Resources::ZipFromFs{origin});
                    break;
                case ResourceOriginType::FolderFromFs:
                    resources.load(Resources::DirFromFs{origin});
                    break;
                case ResourceOriginType::ZipFromCmrc:
                    resources.load(Resources::ZipFromEmbed{origin});
                    break;
                case ResourceOriginType::FolderFromCmrc:
                    resources.load(Resources::DirFromEmbed{origin});
                    break;
            }
        }

        auto end = chrono::high_resolution_clock::now();

        for (auto& callback : run_after_loading) {
            try {
                callback(*this);
            } catch (const exception& e) {
                console.err << "Error " + get_dynamic_name(e) + " in resource loading callback: " << e.what();
            } catch (...) {
                console.err << "Unknown error in resource loading callback";
            }
        }

        console.debug << "Finished loading resources, time="
                      << chrono::duration_cast<chrono::milliseconds>(end - start).count() << "ms";
        has_loaded = true;
    }
} REGISTER_MOD(ResourceHandlerImpl);

span<const byte> ResourceHandlerImpl::Resource::bytes() const {
    if (auto* data = get_if<vector<byte>>(&this->data)) return *data;

    if (auto* text = get_if<string>(&this->data)) {
        return {reinterpret_cast<const byte*>(text->data()), text->size()};
    }

    if (auto* image = get_if<Image>(&this->data)) return image->bytes();

    throw runtime_error("Resource does not contain raw byte data");
}
string_view ResourceHandlerImpl::Resource::text() const {
    auto* text = get_if<string>(&this->data);

    if (!text) throw runtime_error("Resource is not a text resource");

    return *text;
}
const ResourceHandlerImpl::Image& ResourceHandlerImpl::Resource::image() const {
    auto* image = get_if<Image>(&this->data);

    if (!image) throw runtime_error("Resource is not an image resource");

    return *image;
}
const nlohmann::json& ResourceHandlerImpl::Resource::json() const {
    auto* json = get_if<nlohmann::json>(&this->data);

    if (!json) throw runtime_error("Resource is not a JSON resource");

    return *json;
}

ResourceHandlerImpl::FilesystemSource::FilesystemSource(filesystem::path root) : root(std::move(root)) {
    if (!filesystem::exists(this->root))
        throw runtime_error("Resource directory does not exist: " + this->root.string());

    if (!filesystem::is_directory(this->root))
        throw runtime_error("Resource source is not a directory: " + this->root.string());
}
bool ResourceHandlerImpl::FilesystemSource::exists(string_view path) const {
    return filesystem::is_regular_file(root / filesystem::path(path));
}
vector<byte> ResourceHandlerImpl::FilesystemSource::read(string_view path) const {
    const auto full_path = root / filesystem::path(path);

    ifstream file(full_path, ios::binary | ios::ate);

    if (!file) throw runtime_error("Failed to open resource: " + full_path.string());

    const auto size = static_cast<size_t>(file.tellg());

    file.seekg(0);

    vector<byte> data(size);

    if (size > 0) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<streamsize>(size));
    }

    return data;
}
void ResourceHandlerImpl::FilesystemSource::enumerate(const function<void(string_view)>& callback) const {
    for (const auto& entry : filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;

        auto relative = filesystem::relative(entry.path(), root);

        auto path = relative.generic_string();

        callback(path);
    }
}

struct ResourceHandlerImpl::ZipSource::Impl {
    void* reader = nullptr;
    vector<byte> buffer;
    string current_entry;
};
ResourceHandlerImpl::ZipSource::ZipSource(const filesystem::path& path) : impl(make_unique<Impl>()) {
    impl->reader = mz_zip_reader_create();

    if (!impl->reader) throw runtime_error("Failed to create ZIP reader");

    const int32_t result = mz_zip_reader_open_file(impl->reader, path.string().c_str());

    if (result != MZ_OK) {
        mz_zip_reader_delete(&impl->reader);

        throw runtime_error("Failed to open ZIP/JAR: " + path.string());
    }
}
ResourceHandlerImpl::ZipSource::ZipSource(vector<byte> data) : impl(make_unique<Impl>()) {
    impl->reader = mz_zip_reader_create();

    if (!impl->reader) throw runtime_error("Failed to create ZIP reader");

    if (data.size() > static_cast<size_t>(INT32_MAX)) {
        throw runtime_error("Embedded ZIP is too large");
    }

    impl->buffer = std::move(data);
    const int32_t result = mz_zip_reader_open_buffer(impl->reader,
        reinterpret_cast<const uint8_t*>(impl->buffer.data()), static_cast<int32_t>(impl->buffer.size()), 0);

    if (result != MZ_OK) {
        mz_zip_reader_delete(&impl->reader);

        throw runtime_error("Failed to open embedded ZIP");
    }
}
ResourceHandlerImpl::ZipSource::~ZipSource() {
    if (!impl || !impl->reader) return;

    mz_zip_reader_close(impl->reader);

    mz_zip_reader_delete(&impl->reader);
}
bool ResourceHandlerImpl::ZipSource::exists(string_view path) const {
    if (!impl || !impl->reader) return false;

    const string name(path);

    const int32_t result = mz_zip_reader_locate_entry(impl->reader, name.c_str(), 0);

    return result == MZ_OK;
}
vector<byte> ResourceHandlerImpl::ZipSource::read(string_view path) const {
    if (!impl || !impl->reader) throw runtime_error("ZIP reader is not initialized");

    const string name(path);

    int32_t result = MZ_OK;
    if (impl->current_entry != name) {
        result = mz_zip_reader_locate_entry(impl->reader, name.c_str(), 0);
        if (result != MZ_OK) throw runtime_error("Resource does not exist in ZIP: " + name);
    }

    mz_zip_file* file_info = nullptr;

    result = mz_zip_reader_entry_get_info(impl->reader, &file_info);

    if (result != MZ_OK || !file_info) throw runtime_error("Failed to get ZIP entry information: " + name);

    const size_t size = static_cast<size_t>(file_info->uncompressed_size);

    vector<byte> data(size);

    if (size == 0) return data;

    result = mz_zip_reader_entry_save_buffer(impl->reader, data.data(), size);

    if (result != MZ_OK) throw runtime_error("Failed to extract ZIP entry: " + name);

    return data;
}
void ResourceHandlerImpl::ZipSource::enumerate(const function<void(string_view)>& callback) const {
    if (!impl || !impl->reader) return;

    int32_t result = mz_zip_reader_goto_first_entry(impl->reader);

    while (result == MZ_OK) {
        mz_zip_file* file_info = nullptr;

        result = mz_zip_reader_entry_get_info(impl->reader, &file_info);
        if (result != MZ_OK || !file_info) break;

        if (file_info->filename) {
            impl->current_entry = file_info->filename;
            callback(impl->current_entry);
            impl->current_entry.clear();
        }
        result = mz_zip_reader_goto_next_entry(impl->reader);
    }
}

void ResourceHandlerImpl::Resources::load(ResourceHandlerImpl::ResourceSource& source) {
    source.enumerate([&](string_view path) {
        string clean_path;
        for (const auto& folder : whitelist) {
            const string prefix = folder + "/";
            if (path.starts_with(prefix)) {
                clean_path = string(path.substr(prefix.size()));
                break;
            }
        }

        if (clean_path.empty() || path.ends_with('/')) return;

        try {
            // Later sources have higher priority.
            resources.insert_or_assign(clean_path, load_resource(source, path));
        } catch (const exception&) {
            // Resource failed to load.
            // Ignore it for now.
        }
    });
}
void ResourceHandlerImpl::Resources::load(const variant<ZipFromFs, DirFromFs, ZipFromEmbed, DirFromEmbed>& source) {
    visit(
        [this](const auto& source) {
            using Source = decay_t<decltype(source)>;

            if constexpr (is_same_v<Source, ZipFromFs>) {
                ZipSource zip(source.path);
                load(zip);
            } else if constexpr (is_same_v<Source, DirFromFs>) {
                FilesystemSource directory(source.path);
                load(directory);
            } else if constexpr (is_same_v<Source, ZipFromEmbed>) {
                const auto fs = cmrc::assets::get_filesystem();
                const auto file = fs.open(source.path);
                vector<byte> data(file.size());
                if (!data.empty()) memcpy(data.data(), file.begin(), file.size());
                ZipSource zip(std::move(data));
                load(zip);
            } else if constexpr (is_same_v<Source, DirFromEmbed>) {
                auto path = source.path;
                while (!path.empty() && path.back() == '/') path.pop_back();
                CMRCSource directory(cmrc::assets::get_filesystem(), path);
                load(directory);
            }
        },
        source);
}
bool ResourceHandlerImpl::Resources::contains(string_view path) const { return resources.contains(string(path)); }
const ResourceHandlerImpl::Resource& ResourceHandlerImpl::Resources::get(string_view path) const {
    auto it = resources.find(string(path));

    if (it == resources.end()) throw runtime_error("Resource not found: " + string(path));

    return it->second;
}
const ResourceHandlerImpl::Resource* ResourceHandlerImpl::Resources::try_get(string_view path) const {
    auto it = resources.find(string(path));

    if (it == resources.end()) return nullptr;

    return &it->second;
}
size_t ResourceHandlerImpl::Resources::size() const noexcept { return resources.size(); }
const unordered_map<string, ResourceHandlerImpl::Resource>& ResourceHandlerImpl::Resources::get_resources()
    const noexcept {
    return resources;
}

ResourceHandlerImpl::Resource ResourceHandlerImpl::Resources::load_resource(
    ResourceHandlerImpl::ResourceSource& source, string_view path) {
    auto data = source.read(path);

    const auto type = determine_type(path);

    switch (type) {
        case ResourceType::PNG: {
            return Resource{.type = ResourceType::PNG, .data = decode_png(data)};
        }

        case ResourceType::JSON: {
            const auto text = normalize_text(data);

            return Resource{.type = ResourceType::JSON, .data = nlohmann::json::parse(text)};
        }

        case ResourceType::Text: {
            return Resource{.type = ResourceType::Text, .data = normalize_text(data)};
        }

        case ResourceType::Binary: {
            return Resource{.type = ResourceType::Binary, .data = std::move(data)};
        }
    }

    throw runtime_error("Unknown resource type");
}
ResourceHandlerImpl::ResourceType ResourceHandlerImpl::Resources::determine_type(string_view path) {
    const auto dot = path.find_last_of('.');

    if (dot == string_view::npos) return ResourceType::Binary;

    const auto extension = path.substr(dot + 1);

    if (extension == "png") return ResourceType::PNG;

    if (extension == "json" || extension == "mcmeta") return ResourceType::JSON;

    // Minecraft text formats.
    if (extension == "txt" || extension == "mcfunction" || extension == "properties" || extension == "json5" ||
        extension == "lang" || extension == "fsh" || extension == "vsh" || extension == "glsl" || extension == "wgsl")
        return ResourceType::Text;

    return ResourceType::Binary;
}
string ResourceHandlerImpl::Resources::normalize_text(span<const byte> data) {
    const char* input = reinterpret_cast<const char*>(data.data());

    string output;

    output.reserve(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        const char c = input[i];

        if (c == '\r') {
            // CRLF -> LF
            if (i + 1 < data.size() && input[i + 1] == '\n') {
                ++i;
            }

            output.push_back('\n');
        } else {
            output.push_back(c);
        }
    }

    return output;
}

ResourceHandlerImpl::Image ResourceHandlerImpl::Resources::decode_png(span<const byte> data) {
    if (data.empty()) throw runtime_error("Cannot decode empty PNG");

    int width = 0;
    int height = 0;
    int channels = 0;

    const auto* input = reinterpret_cast<const stbi_uc*>(data.data());

    stbi_uc* pixels = stbi_load_from_memory(input, static_cast<int>(data.size()), &width, &height, &channels, 4);

    if (!pixels) throw runtime_error(string("Failed to decode PNG: ") + stbi_failure_reason());

    const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    Image image;

    image.width = width;
    image.height = height;

    image.pixels.resize(size);

    memcpy(image.pixels.data(), pixels, size);

    stbi_image_free(pixels);

    return image;
}
ResourceHandlerImpl::CMRCSource::CMRCSource(cmrc::embedded_filesystem fs, filesystem::path root)
    : fs(std::move(fs)), root(root.generic_string()) {
    while (!this->root.empty() && this->root.back() == '/') this->root.pop_back();
}
bool ResourceHandlerImpl::CMRCSource::exists(std::string_view path) const {
    const auto full_path = root.empty() ? string(path) : root + "/" + string(path);
    return fs.exists(full_path);
}

std::vector<std::byte> ResourceHandlerImpl::CMRCSource::read(std::string_view path) const {
    const std::string name = root.empty() ? string(path) : root + "/" + string(path);

    if (!fs.exists(name)) throw std::runtime_error("CMRC resource does not exist: " + name);

    const auto file = fs.open(name);

    std::vector<std::byte> data(file.size());

    if (!data.empty()) {
        std::memcpy(data.data(), file.begin(), file.size());
    }

    return data;
}

void ResourceHandlerImpl::CMRCSource::enumerate(const std::function<void(std::string_view)>& callback) const {
    std::function<void(std::string_view)> walk;

    walk = [&](std::string_view directory) {
        const string relative_directory(directory);
        const string full_directory = root.empty() || relative_directory.empty()
                                          ? (root.empty() ? relative_directory : root)
                                          : root + "/" + relative_directory;
        const auto dir = fs.open(full_directory);

        for (const auto& filename : dir) {
            std::string path;

            if (relative_directory.empty())
                path = filename;
            else
                path = relative_directory + "/" + filename;

            const string full_path = root.empty() ? path : root + "/" + path;
            if (fs.is_directory(full_path)) {
                walk(path);
            } else {
                callback(path);
            }
        }
    };

    walk("");
}
// </AI>
