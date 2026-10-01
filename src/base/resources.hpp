#pragma once

#include "cmrc/cmrc.hpp"
#include <cmrc/cmrc.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <triolib.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
CMRC_DECLARE(assets);

struct ResourceHandler : triolib::AppModule {
    bool has_loaded = false;
    bool has_started_loading = false;

    enum class ResourceOriginType {
        ZipFromFs,
        FolderFromFs,
        ZipFromCmrc,
        FolderFromCmrc
    };
    std::vector<std::pair<std::string, ResourceOriginType>> resource_origins;
    std::unordered_set<std::string> whitelist;

    virtual void start_loading() = 0;

    bool does_go_after_init(const triolib::AppModule& other) const override {
        return true;
    }

    void init() override { start_loading(); }
    void loop() override {}
    void quit() override {}

    enum class ResourceType { Binary, Text, PNG, JSON };

    struct Image {
        int width = 0;
        int height = 0;

        // Always RGBA8.
        std::vector<std::byte> pixels;

        [[nodiscard]]
        std::span<const std::byte> bytes() const {
            return pixels;
        }
    };

    using ResourceData = std::variant<std::vector<std::byte>, std::string,
        Image, nlohmann::json>;

    struct Resource {
        ResourceType type;
        ResourceData data;

        [[nodiscard]]
        std::span<const std::byte> bytes() const;

        [[nodiscard]]
        std::string_view text() const;

        [[nodiscard]]
        const Image& image() const;

        [[nodiscard]]
        const nlohmann::json& json() const;
    };

    class ResourceSource {
        public:
        virtual ~ResourceSource() = default;

        [[nodiscard]]
        virtual bool exists(std::string_view path) const = 0;

        [[nodiscard]]
        virtual std::vector<std::byte> read(std::string_view path) const = 0;

        virtual void enumerate(
            const std::function<void(std::string_view)>& callback) const = 0;
    };

    class FilesystemSource final : public ResourceSource {
        public:
        explicit FilesystemSource(std::filesystem::path root);

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback)
            const override;

        private:
        std::filesystem::path root;
    };

    struct ZipSource final : public ResourceSource {
        explicit ZipSource(const std::filesystem::path& path);
        explicit ZipSource(std::vector<std::byte> data);
        ~ZipSource() override;

        ZipSource(const ZipSource&) = delete;
        ZipSource& operator=(const ZipSource&) = delete;

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback)
            const override;

        private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    class CMRCSource final : public ResourceSource {
        public:
        explicit CMRCSource(
            cmrc::embedded_filesystem fs, std::filesystem::path root = {});

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback)
            const override;

        private:
        cmrc::embedded_filesystem fs;
        std::string root;
    };

    class Resources {
        public:
        using AtlasRect =
            std::pair<glm::vec<2, unsigned int>, glm::vec<2, unsigned int>>;

        struct Atlas {
            std::string name;
            Image image;
            std::unordered_map<std::string, AtlasRect> locations;

            [[nodiscard]] const AtlasRect& operator[](
                std::string_view path) const;
        };

        // Resource names are stored without their whitelisted top-level folder.
        std::vector<std::string> whitelist{"data"};

        struct ZipFromFs {
            std::filesystem::path path;
        };

        struct DirFromFs {
            std::filesystem::path path;
        };

        struct ZipFromEmbed {
            std::string path;
        };

        struct DirFromEmbed {
            std::string path;
        };

        Resources() = default;

        void load(ResourceSource& source);
        void load(const std::variant<ZipFromFs, DirFromFs, ZipFromEmbed,
            DirFromEmbed>& source);

        [[nodiscard]]
        bool contains(std::string_view path) const;

        [[nodiscard]]
        const Resource& get(std::string_view path) const;

        [[nodiscard]]
        const Resource* try_get(std::string_view path) const;

        [[nodiscard]]
        std::size_t size() const noexcept;

        [[nodiscard]]
        const std::unordered_map<std::string, Resource>& get_resources()
            const noexcept;

        [[nodiscard]] std::vector<std::string> list_files(
            std::string_view directory) const;
        [[nodiscard]] std::vector<std::string> list_dirs(
            std::string_view directory) const;
        void define_atlas(std::string name, std::vector<std::string> paths);
        void load_all_atlases();
        [[nodiscard]] const std::vector<Atlas>& get_atlases(
            std::string_view name) const;
        [[nodiscard]] std::optional<std::pair<const Image*, AtlasRect>>
        find_texture(std::string_view path) const;

        private:
        std::unordered_map<std::string, Resource> resources;

        static Resource load_resource(
            ResourceSource& source, std::string_view path);

        static ResourceType determine_type(std::string_view path);

        static std::string normalize_text(std::span<const std::byte> data);

        static Image decode_png(std::span<const std::byte> data);

        std::vector<std::pair<std::string, std::vector<std::string>>>
            atlas_definitions;
        std::unordered_map<std::string, std::vector<Atlas>> atlases;
    };

    Resources resources;
};
