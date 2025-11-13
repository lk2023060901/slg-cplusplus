#include "json/json_reader.h"

#include <fstream>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace slg::json {

namespace {

[[noreturn]] void ThrowParseError(const nlohmann::json::parse_error& error,
                                  std::string_view context) {
    std::ostringstream builder;
    builder << "Failed to parse JSON " << context << ": " << error.what();
    throw std::runtime_error(builder.str());
}

}  // namespace

JsonValue JsonReader::ParseString(std::string_view json_text) const {
    try {
        auto json = nlohmann::json::parse(json_text.begin(), json_text.end(), nullptr, true, false);
        return JsonValue(std::move(json));
    } catch (const nlohmann::json::parse_error& error) {
        ThrowParseError(error, "string");
    }
}

JsonValue JsonReader::ParseStream(std::istream& stream) const {
    try {
        auto json = nlohmann::json::parse(stream, nullptr, true, false);
        return JsonValue(std::move(json));
    } catch (const nlohmann::json::parse_error& error) {
        ThrowParseError(error, "stream");
    }
}

JsonValue JsonReader::ParseFile(const std::string& file_path) const {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open JSON file: " + file_path);
    }

    return ParseStream(file);
}

}  // namespace slg::json
