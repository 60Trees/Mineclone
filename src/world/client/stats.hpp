/**
 * @file world/client/stats.hpp
 * @brief Chunk loading / meshing benchmarks exposed to the debug screen
 * @detail
 * All of these are written by worker threads and read by the debug overlay on the
 * main thread, so every access is guarded.
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>
#include <world/shared.hpp>

/// @brief A fixed-size rolling window of durations, in seconds.
struct RollingAverage {
    static constexpr size_t window = 128;

    void add(double seconds) {
        std::lock_guard lock(mutex);
        samples[next] = seconds;
        next = (next + 1) % window;
        if (count < window) ++count;
    }

    struct Snapshot {
        /// @brief Mean of every sample currently in the window.
        double average = 0.0;
        /// @brief Most recently recorded sample.
        double latest = 0.0;
        /// @brief Number of samples currently in the window (up to \ref window).
        size_t count = 0;
    };

    Snapshot snapshot() const {
        std::lock_guard lock(mutex);
        Snapshot result;
        result.count = count;
        if (count == 0) return result;

        double total = 0.0;
        for (size_t i = 0; i < count; ++i) total += samples[i];
        result.average = total / static_cast<double>(count);
        result.latest = samples[(next + window - 1) % window];
        return result;
    }

    private:
    mutable std::mutex mutex;
    std::array<double, window> samples{};
    size_t next = 0;
    size_t count = 0;
};

/// @brief Counts events and reports how many happened in the last second.
struct RateMeter {
    using Clock = std::chrono::steady_clock;

    void add(uint64_t amount = 1) {
        std::lock_guard lock(mutex);
        const auto now = Clock::now();
        events.emplace_back(now, amount);
        trim(now);
    }

    /// @brief Events in the trailing one-second window.
    double per_second() const {
        std::lock_guard lock(mutex);
        trim(Clock::now());

        uint64_t total = 0;
        for (const auto& [time, amount] : events) total += amount;
        return static_cast<double>(total);
    }

    private:
    void trim(Clock::time_point now) const {
        const auto cutoff = now - std::chrono::seconds(1);
        while (!events.empty() && events.front().first < cutoff) events.pop_front();
    }

    mutable std::mutex mutex;
    mutable std::deque<std::pair<Clock::time_point, uint64_t>> events;
};

/// @brief One chunk that recently finished meshing.
struct RecentMesh {
    ChunkIndex index;
    double seconds = 0.0;
    uint64_t revision = 0;
};

/// @brief Chunk pipeline benchmarks and the most recent meshes.
struct ChunkLoadStats {
    RollingAverage mesh_time;
    RollingAverage generation_time;
    RollingAverage pick_time;

    RateMeter meshed_per_second;
    RateMeter generated_per_second;
    RateMeter loaded_per_second;
    RateMeter picked_per_second;

    std::atomic<size_t> mesh_queue{0};
    std::atomic<size_t> generation_queue{0};

    static constexpr size_t recent_limit = 5;

    void record_mesh(ChunkIndex index, double seconds, uint64_t revision) {
        mesh_time.add(seconds);

        std::lock_guard lock(recent_mutex);
        recent_meshes.push_front({index, seconds, revision});
        if (recent_meshes.size() > recent_limit) recent_meshes.pop_back();
    }

    /// @brief Newest first, at most \ref recent_limit entries.
    std::vector<RecentMesh> recent() const {
        std::lock_guard lock(recent_mutex);
        return {recent_meshes.begin(), recent_meshes.end()};
    }

    private:
    mutable std::mutex recent_mutex;
    std::deque<RecentMesh> recent_meshes;
};
