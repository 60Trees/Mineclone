#include "resource_pack.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include <minizip-ng/mz.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace std;

namespace Mineclone {
    // ============================================================
    // Resource
    // ============================================================

    span<const byte> Resource::bytes() const {
        if (auto* data = get_if<vector<byte>>(&this->data)) return *data;

        if (auto* text = get_if<string>(&this->data)) {
            return {reinterpret_cast<const byte*>(text->data()), text->size()};
        }

        if (auto* image = get_if<Image>(&this->data)) return image->bytes();

        throw runtime_error("Resource does not contain raw byte data");
    }

    string_view Resource::text() const {
        auto* text = get_if<string>(&this->data);

        if (!text) throw runtime_error("Resource is not a text resource");

        return *text;
    }

    const Image& Resource::image() const {
        auto* image = get_if<Image>(&this->data);

        if (!image) throw runtime_error("Resource is not an image resource");

        return *image;
    }

    const nlohmann::json& Resource::json() const {
        auto* json = get_if<nlohmann::json>(&this->data);

        if (!json) throw runtime_error("Resource is not a JSON resource");

        return *json;
    }

    // ============================================================
    // FilesystemSource
    // ============================================================

    FilesystemSource::FilesystemSource(filesystem::path root) : root(std::move(root)) {
        if (!filesystem::exists(this->root)) throw runtime_error("Resource directory does not exist: " + this->root.string());

        if (!filesystem::is_directory(this->root)) throw runtime_error("Resource source is not a directory: " + this->root.string());
    }

    bool FilesystemSource::exists(string_view path) const { return filesystem::is_regular_file(root / filesystem::path(path)); }

    vector<byte> FilesystemSource::read(string_view path) const {
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

    void FilesystemSource::enumerate(const function<void(string_view)>& callback) const {
        for (const auto& entry : filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;

            auto relative = filesystem::relative(entry.path(), root);

            auto path = relative.generic_string();

            callback(path);
        }
    }

    // ============================================================
    // ZipSource implementation
    // ============================================================

    struct ZipSource::Impl {
        void* reader = nullptr;
    };

    ZipSource::ZipSource(const filesystem::path& path) : impl(make_unique<Impl>()) {
        impl->reader = mz_zip_reader_create();

        if (!impl->reader) throw runtime_error("Failed to create ZIP reader");

        const int32_t result = mz_zip_reader_open_file(impl->reader, path.string().c_str());

        if (result != MZ_OK) {
            mz_zip_reader_delete(&impl->reader);

            throw runtime_error("Failed to open ZIP/JAR: " + path.string());
        }
    }

    ZipSource::~ZipSource() {
        if (!impl || !impl->reader) return;

        mz_zip_reader_close(impl->reader);

        mz_zip_reader_delete(&impl->reader);
    }

    bool ZipSource::exists(string_view path) const {
        if (!impl || !impl->reader) return false;

        const string name(path);

        const int32_t result = mz_zip_reader_locate_entry(impl->reader, name.c_str(), 0);

        return result == MZ_OK;
    }

    vector<byte> ZipSource::read(string_view path) const {
        if (!impl || !impl->reader) throw runtime_error("ZIP reader is not initialized");

        const string name(path);

        int32_t result = mz_zip_reader_locate_entry(impl->reader, name.c_str(), 0);

        if (result != MZ_OK) throw runtime_error("Resource does not exist in ZIP: " + name);

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

    void ZipSource::enumerate(const function<void(string_view)>& callback) const {
        if (!impl || !impl->reader) return;

        int32_t result = mz_zip_reader_goto_first_entry(impl->reader);

        while (result == MZ_OK) {
            mz_zip_file* file_info = nullptr;

            result = mz_zip_reader_entry_get_info(impl->reader, &file_info);

            if (result != MZ_OK || !file_info) break;

            if (file_info->filename) {
                callback(string_view(file_info->filename));
            }

            result = mz_zip_reader_goto_next_entry(impl->reader);
        }
    }

    // ============================================================
    // ResourcePack
    // ============================================================

    void ResourcePack::load(ResourceSource& source) {
        resources.clear();

        source.enumerate([&](string_view path) {
            // Only Minecraft's assets/ and data/ directories.
            const bool is_assets = path.starts_with("assets/");

            const bool is_data = path.starts_with("data/");

            if (!is_assets && !is_data) return;

            // Don't load directories.
            if (path.ends_with('/')) return;

            // Remove assets/ or data/.
            string clean_path;

            if (is_assets)
                clean_path = string(path.substr(7));
            else
                clean_path = string(path.substr(5));

            if (clean_path.empty()) return;

            try {
                resources.emplace(clean_path, load_resource(source, path));
            } catch (const exception&) {
                // Resource failed to load.
                // Ignore it for now.
            }
        });
    }

    bool ResourcePack::contains(string_view path) const { return resources.contains(string(path)); }

    const Resource& ResourcePack::get(string_view path) const {
        auto it = resources.find(string(path));

        if (it == resources.end()) throw runtime_error("Resource not found: " + string(path));

        return it->second;
    }

    const Resource* ResourcePack::try_get(string_view path) const {
        auto it = resources.find(string(path));

        if (it == resources.end()) return nullptr;

        return &it->second;
    }

    size_t ResourcePack::size() const noexcept { return resources.size(); }

    const unordered_map<string, Resource>& ResourcePack::get_resources() const noexcept { return resources; }

    // ============================================================
    // Resource loading
    // ============================================================

    Resource ResourcePack::load_resource(ResourceSource& source, string_view path) {
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

    // ============================================================
    // Type detection
    // ============================================================

    ResourceType ResourcePack::determine_type(string_view path) {
        const auto dot = path.find_last_of('.');

        if (dot == string_view::npos) return ResourceType::Binary;

        const auto extension = path.substr(dot + 1);

        if (extension == "png") return ResourceType::PNG;

        if (extension == "json" || extension == "mcmeta") return ResourceType::JSON;

        // Minecraft text formats.
        if (extension == "txt" || extension == "mcfunction" || extension == "properties" || extension == "json5" || extension == "lang" ||
            extension == "fsh" || extension == "vsh" || extension == "glsl" || extension == "wgsl")
            return ResourceType::Text;

        return ResourceType::Binary;
    }

    // ============================================================
    // Text normalization
    // ============================================================

    string ResourcePack::normalize_text(span<const byte> data) {
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

    // ============================================================
    // PNG decoding
    // ============================================================

    Image ResourcePack::decode_png(span<const byte> data) {
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

    CMRCSource::CMRCSource(cmrc::embedded_filesystem fs) : fs(std::move(fs)) {}

    bool CMRCSource::exists(std::string_view path) const { return fs.exists(std::string(path)); }

    std::vector<std::byte> CMRCSource::read(std::string_view path) const {
        const std::string name(path);

        if (!fs.exists(name)) throw std::runtime_error("CMRC resource does not exist: " + name);

        const auto file = fs.open(name);

        std::vector<std::byte> data(file.size());

        if (!data.empty()) {
            std::memcpy(data.data(), file.begin(), file.size());
        }

        return data;
    }

    void CMRCSource::enumerate(const std::function<void(std::string_view)>& callback) const {
        // CMRC doesn't have a recursive iterator in the same
        // form as std::filesystem, so recursively walk directories.
        //
        // The embedded filesystem's directory API provides the
        // files/directories directly below a path.

        std::function<void(std::string_view)> walk;

        walk = [&](std::string_view directory) {
            const auto dir = fs.open(std::string(directory));

            for (const auto& filename : dir) {
                std::string path;

                if (directory.empty())
                    path = filename;
                else
                    path = std::string(directory) + "/" + filename;

                if (fs.is_directory(path)) {
                    walk(path);
                } else {
                    callback(path);
                }
            }
        };

        walk("");
    }

}  // namespace Mineclone
