#pragma once

#include <functional>
#include <vector>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <utility>
#include <cstddef>

namespace utils {
    template <typename T>
    class weighted_set {
        public:
        struct Entry {
            T value;
            double weight;
        };

        private:
        std::vector<Entry> entries;

        inline static thread_local std::mt19937_64 rng{std::random_device{}()};

        public:
        using iterator = typename std::vector<Entry>::iterator;
        using const_iterator = typename std::vector<Entry>::const_iterator;

        inline void add(T value, double weight = 1.0) {
            if (weight < 0.0) throw std::invalid_argument("Weight cannot be negative");

            auto it = find(value);

            if (it != entries.end()) {
                it->weight = weight;
                return;
            }

            entries.push_back({std::move(value), weight});
        }

        inline bool remove(const T& value) {
            auto it = find(value);

            if (it == entries.end()) return false;

            entries.erase(it);
            return true;
        }

        inline iterator find(const T& value) {
            return std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.value == value; });
        }

        inline const_iterator find(const T& value) const {
            return std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.value == value; });
        }

        T& random() {
            if (entries.empty()) throw std::out_of_range("WeightedSet is empty");

            double total = 0.0;

            for (const auto& e : entries) total += e.weight;

            if (total <= 0.0) throw std::runtime_error("All weights are zero");

            std::uniform_real_distribution<double> dist(0.0, total);
            double choice = dist(rng);

            for (auto& e : entries) {
                choice -= e.weight;

                if (choice < 0.0) return e.value;
            }

            return entries.back().value;
        }

        bool try_random(const std::function<bool(T&)>& function) {
            std::vector<std::size_t> remaining(entries.size());

            std::iota(remaining.begin(), remaining.end(), 0);

            while (!remaining.empty()) {
                double total = 0.0;

                for (auto index : remaining) total += entries[index].weight;

                if (total <= 0.0) return false;

                std::uniform_real_distribution<double> dist(0.0, total);
                double choice = dist(rng);

                std::size_t selected = 0;

                for (; selected < remaining.size(); ++selected) {
                    choice -= entries[remaining[selected]].weight;

                    if (choice < 0.0) break;
                }

                if (selected == remaining.size()) selected--;

                std::size_t index = remaining[selected];

                remaining.erase(remaining.begin() + selected);

                if (function(entries[index].value)) return true;
            }

            return false;
        }

        inline std::size_t size() const { return entries.size(); }
        inline bool empty() const { return entries.empty(); }

        inline void clear() { entries.clear(); }

        inline iterator begin() { return entries.begin(); }
        inline iterator end() { return entries.end(); }

        inline const_iterator begin() const { return entries.begin(); }
        inline const_iterator end() const { return entries.end(); }
    };
}  // namespace utils
