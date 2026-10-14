// it looks cool (:
// <AI>
#include <triolib.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

namespace {
    namespace ansi {
        constexpr const char* reset = "\033[0m";
        constexpr const char* red = "\033[31m";
        constexpr const char* white = "\033[37m";
        constexpr const char* blue = "\033[34m";
        constexpr const char* green = "\033[32m";
        constexpr const char* orange = "\033[38;5;208m";
        constexpr const char* yellow = "\033[33m";
        constexpr const char* grey = "\033[90m";
    }  // namespace ansi
}  // namespace

std::string prettify_json(const nlohmann::json& j, int indent, int line_limit, int chars_for_single_line_object) {
    using json = nlohmann::json;

    indent = std::max(0, indent);
    chars_for_single_line_object = std::max(0, chars_for_single_line_object);

    auto paint = [](const std::string& value, const char* colour) { return std::string(colour) + value + ansi::reset; };

    auto symbols = [&](const std::string& value) {
        if (value == "{" || value == "}" || value == "[" || value == "]") return paint(value, ansi::yellow);
        return paint(value, ansi::white);
    };

    std::function<std::string(int, int)> spaces = [&](int count, int do_marker) -> std::string {
        if (count <= 0) return std::string();
        if (do_marker != true) return std::string(static_cast<std::size_t>(count), ' ');
        if (count < 2) return std::string() + ansi::grey + "│" + ansi::reset;
        if (count > indent) return spaces(indent, true) + spaces(count - indent, true);
        return std::string() + ansi::grey + "│" + ansi::reset + std::string(static_cast<std::size_t>(count - 1), ' ');
    };

    // Produce a compact representation for measuring container length.
    // This representation is not coloured.
    auto compact_length = [](const json& value) -> std::size_t { return value.dump(-1).size(); };

    auto key = [](const json& key) {
        if (key.is_string()) return key.dump().substr(1, key.dump().size() - 2);
        return prettify_json(key, 0, -1, 0);
    };

    // Render one JSON value on a single line.
    auto compact = [&](const auto& self, const json& value) -> std::string {
        if (value.is_null()) return paint("null", ansi::red);

        if (value.is_boolean()) return paint(value.get<bool>() ? "true" : "false", ansi::orange);

        if (value.is_number()) return paint(value.dump(), ansi::blue);

        if (value.is_string()) return paint(value.dump(), ansi::green);

        if (value.is_array()) {
            std::string out = symbols("[");

            bool first = true;
            for (const auto& item : value) {
                if (!first) out += symbols(", ");

                out += self(self, item);
                first = false;
            }

            out += symbols("]");
            return out;
        }

        if (value.is_object()) {
            std::string out = symbols("{");

            bool first = true;
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (!first) out += symbols(", ");

                out += paint(key(it.key()), ansi::white);
                out += symbols(": ");
                out += self(self, it.value());
                first = false;
            }

            out += symbols("}");
            return out;
        }

        return {};
    };

    // Render a value recursively.
    //
    // collapse_threshold determines the maximum compact JSON length
    // allowed for a container to remain on one line.
    auto render = [&](const auto& self, const json& value, int depth, std::size_t collapse_threshold,
                      bool is_root) -> std::string {
        if (!value.is_structured()) return compact(compact, value);

        const bool object = value.is_object();
        const char* open = object ? "{" : "[";
        const char* close = object ? "}" : "]";

        if (value.empty()) return symbols(std::string(open) + close);

        // Keep small containers on one line.
        if ((!is_root || line_limit < 0) && compact_length(value) <= collapse_threshold) {
            return compact(compact, value);
        }

        // If truncation is active, large containers can be collapsed.
        if (line_limit >= 0 && !is_root && collapse_threshold == static_cast<std::size_t>(-1)) {
            return symbols(open) + " " + paint("...", ansi::grey) + " " + symbols(close);
        }

        const std::string current_indent = spaces(depth * indent, true);
        const std::string child_indent = spaces((depth + 1) * indent, true);

        std::string out = symbols(open);
        bool first = true;

        for (auto it = value.begin(); it != value.end(); ++it) {
            out += first ? "\n" : symbols(",");
            if (!first) out += "\n";

            out += child_indent;

            if (object) {
                out += paint(key(it.key()), ansi::white);
                out += symbols(": ");
            }

            out += self(self, it.value(), depth + 1, collapse_threshold, false);

            first = false;
        }

        out += "\n";
        out += current_indent;
        out += symbols(close);

        return out;
    };

    // First produce the normal formatted representation.
    std::string result = render(render, j, 0, static_cast<std::size_t>(chars_for_single_line_object), true);

    auto count_lines = [](const std::string& value) {
        return static_cast<int>(std::count(value.begin(), value.end(), '\n')) + 1;
    };

    if (line_limit < 1 || count_lines(result) <= line_limit) return result;

    // Increase the inline threshold progressively. This collapses
    // small nested containers first, then progressively larger ones.
    const std::size_t maximum_threshold = j.dump(-1).size();

    for (std::size_t threshold = static_cast<std::size_t>(chars_for_single_line_object) + 1;
        threshold <= maximum_threshold; ++threshold) {
        result = render(render, j, 0, threshold, true);

        if (count_lines(result) <= line_limit) return result;
    }

    // If even the root cannot fit within the limit, show a compact
    // root placeholder. This preserves the opening and closing braces.
    if (j.is_object()) return symbols("{") + " " + paint("...", ansi::grey) + " " + symbols("}");

    // The interface specifies a brace-delimited result.
    return symbols("{") + " " + paint("...", ansi::grey) + " " + symbols("}");
}
// </AI>
