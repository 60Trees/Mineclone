#pragma once

// Minecraft blockstate JSON model (variants / multipart).
// Requires: C++17, nlohmann/json (https://github.com/nlohmann/json)

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace assets {
    /// @brief A structured JSON template for `assets/{namespace}/blockstates/{block}.json`
    struct blockstates {
        using json = nlohmann::json;
        using properties = std::map<std::string, std::string>;  // e.g. {"facing","north"}

        // ------------------------------------------------------------------
        // A single model reference (used by both variants and multipart "apply")
        // ------------------------------------------------------------------
        struct model_ref {
            std::string model;    // resource location, e.g. "minecraft:block/stone"
            int x = 0;            // rotation around x in 90-degree steps (0, 90, 180, 270)
            int y = 0;            // rotation around y
            int z = 0;            // rotation around z
            bool uvlock = false;  // lock texture rotation
            int weight = 1;       // relative probability when several models are listed

            static model_ref from_json(const json& j) {
                if (!j.is_object() || !j.contains("model"))
                    throw std::runtime_error("blockstates: model entry requires a \"model\" string");
                model_ref m;
                m.model = j.at("model").get<std::string>();
                m.x = j.value("x", 0);
                m.y = j.value("y", 0);
                m.z = j.value("z", 0);
                m.uvlock = j.value("uvlock", false);
                m.weight = j.value("weight", 1);
                return m;
            }

            json to_json() const {
                json j = {{"model", model}};
                if (x) j["x"] = x;
                if (y) j["y"] = y;
                if (z) j["z"] = z;
                if (uvlock) j["uvlock"] = true;
                if (weight != 1) j["weight"] = weight;
                return j;
            }
        };

        // One model, or several (picked randomly by weight)
        struct model_list {
            std::vector<model_ref> models;
            bool was_array = false;  // remember original form for round-tripping

            bool empty() const { return models.empty(); }

            static model_list from_json(const json& j) {
                model_list l;
                if (j.is_array()) {
                    l.was_array = true;
                    for (const auto& e : j) l.models.push_back(model_ref::from_json(e));
                } else {
                    l.models.push_back(model_ref::from_json(j));
                }
                return l;
            }

            json to_json() const {
                if (!was_array && models.size() == 1) return models.front().to_json();
                json a = json::array();
                for (const auto& m : models) a.push_back(m.to_json());
                return a;
            }

            int total_weight() const {
                int sum = 0;
                for (const auto& m : models) sum += std::max(m.weight, 0);
                return sum;
            }

            // Weighted random choice. Returns nullptr if empty.
            template <class URBG>
            const model_ref* pick(URBG& rng) const {
                if (models.empty()) return nullptr;
                const int total = total_weight();
                if (total <= 0) return &models.front();
                std::uniform_int_distribution<int> dist(0, total - 1);
                int r = dist(rng);
                for (const auto& m : models) {
                    r -= std::max(m.weight, 0);
                    if (r < 0) return &m;
                }
                return &models.back();
            }

            // Deterministic choice from a seed value (e.g. a hash of the block position).
            const model_ref* pick(std::uint64_t seed) const {
                std::mt19937_64 rng(seed);
                return pick(rng);
            }
        };

        // ------------------------------------------------------------------
        // Multipart conditions
        // ------------------------------------------------------------------

        // A set of "state": "value" pairs that must ALL match.
        // A value may be "a|b|c" to allow any of several values.
        struct condition {
            std::map<std::string, std::string> states;

            bool matches(const properties& props) const {
                for (const auto& [key, wanted] : states) {
                    auto it = props.find(key);
                    if (it == props.end()) return false;
                    if (!value_in_list(it->second, wanted)) return false;
                }
                return true;
            }

            static condition from_json(const json& j) {
                if (!j.is_object()) throw std::runtime_error("blockstates: condition must be an object");
                condition c;
                for (auto it = j.begin(); it != j.end(); ++it) c.states[it.key()] = scalar_to_string(it.value());
                return c;
            }

            json to_json() const {
                json j = json::object();
                for (const auto& [k, v] : states) j[k] = v;
                return j;
            }

            private:
            static bool value_in_list(std::string_view actual, std::string_view list) {
                size_t start = 0;
                while (true) {
                    size_t bar = list.find('|', start);
                    std::string_view token = list.substr(start, bar == std::string_view::npos ? bar : bar - start);
                    if (token == actual) return true;
                    if (bar == std::string_view::npos) return false;
                    start = bar + 1;
                }
            }
        };

        // "when" clause: plain condition, OR of conditions, or AND of conditions.
        struct when {
            enum class op { single, any /*OR*/, all /*AND*/ };

            op kind = op::single;
            std::vector<condition> conditions;  // exactly one entry when kind == single

            bool matches(const properties& props) const {
                switch (kind) {
                    case op::single:
                        return conditions.empty() || conditions.front().matches(props);
                    case op::any:
                        return std::any_of(
                            conditions.begin(), conditions.end(), [&](const condition& c) { return c.matches(props); });
                    case op::all:
                        return std::all_of(
                            conditions.begin(), conditions.end(), [&](const condition& c) { return c.matches(props); });
                }
                return false;
            }

            static when from_json(const json& j) {
                if (!j.is_object()) throw std::runtime_error("blockstates: \"when\" must be an object");
                when w;
                if (j.contains("OR")) {
                    w.kind = op::any;
                    for (const auto& e : j.at("OR")) w.conditions.push_back(condition::from_json(e));
                } else if (j.contains("AND")) {
                    w.kind = op::all;
                    for (const auto& e : j.at("AND")) w.conditions.push_back(condition::from_json(e));
                } else {
                    w.kind = op::single;
                    w.conditions.push_back(condition::from_json(j));
                }
                return w;
            }

            json to_json() const {
                if (kind == op::single) return conditions.empty() ? json::object() : conditions.front().to_json();
                json arr = json::array();
                for (const auto& c : conditions) arr.push_back(c.to_json());
                return json{{kind == op::any ? "OR" : "AND", arr}};
            }
        };

        // A multipart case: optional "when" + "apply"
        struct part {
            std::optional<when> cond;  // unset => always applies
            model_list apply;

            bool matches(const properties& props) const { return !cond || cond->matches(props); }

            static part from_json(const json& j) {
                if (!j.is_object() || !j.contains("apply"))
                    throw std::runtime_error("blockstates: multipart case requires \"apply\"");
                part p;
                if (j.contains("when")) p.cond = when::from_json(j.at("when"));
                p.apply = model_list::from_json(j.at("apply"));
                return p;
            }

            json to_json() const {
                json j = json::object();
                if (cond) j["when"] = cond->to_json();
                j["apply"] = apply.to_json();
                return j;
            }
        };

        // ------------------------------------------------------------------
        // Data
        // ------------------------------------------------------------------

        // Key is e.g. "facing=north,half=top"; a single-variant block uses "".
        std::map<std::string, model_list> variants;
        std::vector<part> multipart;

        bool has_variants() const { return !variants.empty(); }
        bool has_multipart() const { return !multipart.empty(); }

        // ------------------------------------------------------------------
        // Parsing / serialisation
        // ------------------------------------------------------------------

        static blockstates from_json(const json& j) {
            if (!j.is_object()) throw std::runtime_error("blockstates: root must be an object");

            blockstates b;
            if (j.contains("variants")) {
                for (auto it = j.at("variants").begin(); it != j.at("variants").end(); ++it)
                    b.variants[it.key()] = model_list::from_json(it.value());
            }
            if (j.contains("multipart")) {
                for (const auto& e : j.at("multipart")) b.multipart.push_back(part::from_json(e));
            }
            if (b.variants.empty() && b.multipart.empty())
                throw std::runtime_error("blockstates: needs \"variants\" or \"multipart\"");
            return b;
        }

        static blockstates parse(std::string_view text) { return from_json(json::parse(text)); }

        json to_json() const {
            json j = json::object();
            if (!variants.empty()) {
                json v = json::object();
                for (const auto& [k, l] : variants) v[k] = l.to_json();
                j["variants"] = std::move(v);
            }
            if (!multipart.empty()) {
                json m = json::array();
                for (const auto& p : multipart) m.push_back(p.to_json());
                j["multipart"] = std::move(m);
            }
            return j;
        }

        std::string dump(int indent = 2) const { return to_json().dump(indent); }

        // ------------------------------------------------------------------
        // Lookup
        // ------------------------------------------------------------------

        // Parse "facing=north,half=top" into a property map ("" => empty map).
        static properties parse_key(std::string_view key) {
            properties props;
            size_t start = 0;
            while (start < key.size()) {
                size_t comma = key.find(',', start);
                std::string_view pair = key.substr(start, comma == std::string_view::npos ? comma : comma - start);
                size_t eq = pair.find('=');
                if (eq != std::string_view::npos)
                    props[std::string(pair.substr(0, eq))] = std::string(pair.substr(eq + 1));
                if (comma == std::string_view::npos) break;
                start = comma + 1;
            }
            return props;
        }

        // Build the canonical (alphabetically sorted) variant key from properties.
        static std::string make_key(const properties& props) {
            std::string out;
            for (const auto& [k, v] : props) {  // std::map is already sorted
                if (!out.empty()) out += ',';
                out += k + '=' + v;
            }
            return out;
        }

        // Find the variant for a block state. Exact key match first, then any variant
        // whose listed properties are all a subset of `props` (partial keys).
        const model_list* find_variant(const properties& props) const {
            if (auto it = variants.find(make_key(props)); it != variants.end()) return &it->second;
            if (auto it = variants.find(""); it != variants.end()) return &it->second;
            for (const auto& [key, list] : variants) {
                const properties want = parse_key(key);
                bool ok = true;
                for (const auto& [k, v] : want) {
                    auto f = props.find(k);
                    if (f == props.end() || f->second != v) {
                        ok = false;
                        break;
                    }
                }
                if (ok) return &list;
            }
            return nullptr;
        }

        // Return the model chosen for the given state: a single model for `variants`,
        // or one model per matching multipart case.
        template <class URBG>
        std::vector<const model_ref*> resolve(const properties& props, URBG& rng) const {
            std::vector<const model_ref*> out;
            if (!variants.empty()) {
                if (const model_list* l = find_variant(props))
                    if (const model_ref* m = l->pick(rng)) out.push_back(m);
            }
            for (const auto& p : multipart) {
                if (p.matches(props))
                    if (const model_ref* m = p.apply.pick(rng)) out.push_back(m);
            }
            return out;
        }

        std::vector<const model_ref*> resolve(const properties& props, std::uint64_t seed = 0) const {
            std::mt19937_64 rng(seed);
            return resolve(props, rng);
        }

        private:
        // JSON "when" values may be strings, booleans or numbers; normalise to string.
        static std::string scalar_to_string(const json& v) {
            if (v.is_string()) return v.get<std::string>();
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            if (v.is_number_integer()) return std::to_string(v.get<long long>());
            throw std::runtime_error("blockstates: condition value must be string, bool or integer");
        }
    };
}  // namespace assets
