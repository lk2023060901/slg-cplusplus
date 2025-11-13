#include "json/json_writer.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace slg::json {

JsonWriter::JsonWriter() = default;

JsonWriter::JsonWriter(JsonValue root) : root_(std::move(root)) {}

void JsonWriter::SetRoot(JsonValue root) {
    root_ = std::move(root);
}

const JsonValue& JsonWriter::GetRoot() const noexcept {
    return root_;
}

JsonValue& JsonWriter::GetRoot() noexcept {
    return root_;
}

std::string JsonWriter::WriteToString(int indent) const {
    return root_.Serialize(indent);
}

void JsonWriter::WriteToStream(std::ostream& stream, int indent) const {
    stream << WriteToString(indent);
    if (!stream.good()) {
        throw std::runtime_error("Failed to write JSON to stream");
    }
}

void JsonWriter::WriteToFile(const std::string& file_path, int indent) const {
    std::ofstream file(file_path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open JSON file for writing: " + file_path);
    }

    WriteToStream(file, indent);
}

}  // namespace slg::json
