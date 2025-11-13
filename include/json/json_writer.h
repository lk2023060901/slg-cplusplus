#pragma once

#include <ostream>
#include <string>

#include "json/json_export.h"
#include "json/json_value.h"

namespace slg::json {

class JsonWriter {
public:
    SLG_JSON_API JsonWriter();
    SLG_JSON_API explicit JsonWriter(JsonValue root);

    SLG_JSON_API void SetRoot(JsonValue root);
    SLG_JSON_API const JsonValue& GetRoot() const noexcept;
    SLG_JSON_API JsonValue& GetRoot() noexcept;

    SLG_JSON_API std::string WriteToString(int indent = -1) const;
    SLG_JSON_API void WriteToStream(std::ostream& stream, int indent = -1) const;
    SLG_JSON_API void WriteToFile(const std::string& file_path, int indent = -1) const;

private:
    JsonValue root_;
};

}  // namespace slg::json
