#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <cmrc/cmrc.hpp>
#include <nlohmann/json.hpp>

namespace Mineclone {
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

    using ResourceData = std::variant<std::vector<std::byte>, std::string, Image, nlohmann::json>;

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

        virtual void enumerate(const std::function<void(std::string_view)>& callback) const = 0;
    };

    // ------------------------------------------------------------
    // Filesystem source
    // ------------------------------------------------------------

    class FilesystemSource final : public ResourceSource {
        public:
        explicit FilesystemSource(std::filesystem::path root);

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback) const override;

        private:
        std::filesystem::path root;
    };

    // ------------------------------------------------------------
    // ZIP/JAR source
    // ------------------------------------------------------------

    class ZipSource final : public ResourceSource {
        public:
        explicit ZipSource(const std::filesystem::path& path);
        explicit ZipSource(std::vector<std::byte> data);
        ~ZipSource() override;

        ZipSource(const ZipSource&) = delete;
        ZipSource& operator=(const ZipSource&) = delete;

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback) const override;

        private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    class CMRCSource final : public ResourceSource {
        public:
        explicit CMRCSource(cmrc::embedded_filesystem fs, std::filesystem::path root = {});

        [[nodiscard]]
        bool exists(std::string_view path) const override;

        [[nodiscard]]
        std::vector<std::byte> read(std::string_view path) const override;

        void enumerate(const std::function<void(std::string_view)>& callback) const override;

        private:
        cmrc::embedded_filesystem fs;
        std::string root;
    };

    // ------------------------------------------------------------
    // Resource pack
    // ------------------------------------------------------------

    class Resources {
        public:
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
        void load(const std::variant<ZipFromFs, DirFromFs, ZipFromEmbed, DirFromEmbed>& source);

        [[nodiscard]]
        bool contains(std::string_view path) const;

        [[nodiscard]]
        const Resource& get(std::string_view path) const;

        [[nodiscard]]
        const Resource* try_get(std::string_view path) const;

        [[nodiscard]]
        std::size_t size() const noexcept;

        [[nodiscard]]
        const std::unordered_map<std::string, Resource>& get_resources() const noexcept;

        private:
        std::unordered_map<std::string, Resource> resources;

        static Resource load_resource(ResourceSource& source, std::string_view path);

        static ResourceType determine_type(std::string_view path);

        static std::string normalize_text(std::span<const std::byte> data);

        static Image decode_png(std::span<const std::byte> data);
    };

    using ResourcePack = Resources;

}  // namespace Mineclone
