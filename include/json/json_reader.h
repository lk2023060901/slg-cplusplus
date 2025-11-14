#pragma once

#include <istream>
#include <optional>
#include <string>
#include <string_view>

#include "json/json_export.h"
#include "json/json_value.h"

namespace slg::json {

class JsonReader {
public:
    SLG_JSON_API JsonReader() = default;

    SLG_JSON_API std::optional<JsonValue> ParseString(std::string_view json_text) const;
    SLG_JSON_API std::optional<JsonValue> ParseStream(std::istream& stream) const;
    SLG_JSON_API std::optional<JsonValue> ParseFile(const std::string& file_path) const;
};

}  // namespace slg::json
