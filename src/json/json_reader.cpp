#include "json/json_reader.h"

#include <fstream>
#include <sstream>
#include <string_view>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

namespace slg::json {

std::optional<JsonValue> JsonReader::ParseString(std::string_view json_text) const {
    try {
        auto json = nlohmann::json::parse(json_text.begin(), json_text.end(), nullptr, true, false);
        return JsonValue(std::move(json));
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

std::optional<JsonValue> JsonReader::ParseStream(std::istream& stream) const {
    try {
        auto json = nlohmann::json::parse(stream, nullptr, true, false);
        return JsonValue(std::move(json));
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

std::optional<JsonValue> JsonReader::ParseFile(const std::string& file_path) const {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    return ParseStream(file);
}

}  // namespace slg::json
