#include "json/json_value.h"

#include <sstream>
#include <string>

namespace slg::json {

namespace {

[[noreturn]] void ThrowTypeMismatch(std::string_view operation,
                                    std::string_view expected,
                                    const nlohmann::json& actual) {
    std::ostringstream builder;
    builder << "JsonValue::" << operation << " expects " << expected << " but actual type is "
            << actual.type_name();
    throw std::logic_error(builder.str());
}

void RequireObject(const nlohmann::json& value, std::string_view operation) {
    if (!value.is_object()) {
        ThrowTypeMismatch(operation, "object", value);
    }
}

void RequireArray(const nlohmann::json& value, std::string_view operation) {
    if (!value.is_array()) {
        ThrowTypeMismatch(operation, "array", value);
    }
}

void RequireInsertIndex(std::size_t index, std::size_t size, std::string_view operation) {
    if (index > size) {
        std::ostringstream builder;
        builder << "JsonValue::" << operation << " index " << index
                << " exceeds array size " << size;
        throw std::out_of_range(builder.str());
    }
}

}  // namespace

JsonValue::JsonValue() = default;

JsonValue::JsonValue(nlohmann::json value) : value_(std::move(value)) {}

JsonValue JsonValue::Object() {
    return JsonValue(nlohmann::json::object());
}

JsonValue JsonValue::Array() {
    return JsonValue(nlohmann::json::array());
}

JsonValue::ValueType JsonValue::GetType() const noexcept {
    return value_.type();
}

bool JsonValue::IsNull() const noexcept {
    return value_.is_null();
}

bool JsonValue::IsObject() const noexcept {
    return value_.is_object();
}

bool JsonValue::IsArray() const noexcept {
    return value_.is_array();
}

std::optional<JsonValue> JsonValue::Get(std::string_view key) const {
    if (!IsObject()) {
        return std::nullopt;
    }

    const auto key_string = std::string(key);
    const auto iter = value_.find(key_string);
    if (iter == value_.end()) {
        return std::nullopt;
    }

    return JsonValue(*iter);
}

std::optional<JsonValue> JsonValue::Get(std::size_t index) const {
    if (!IsArray()) {
        return std::nullopt;
    }

    if (index >= value_.size()) {
        return std::nullopt;
    }

    return JsonValue(value_.at(index));
}

void JsonValue::Set(std::string_view key, const JsonValue& value) {
    RequireObject(value_, "Set(key, JsonValue)");
    value_[std::string(key)] = value.value_;
}

void JsonValue::Set(std::string_view key, JsonValue&& value) {
    RequireObject(value_, "Set(key, JsonValue&&)");
    value_[std::string(key)] = std::move(value.value_);
}

void JsonValue::Append(const JsonValue& value) {
    RequireArray(value_, "Append(JsonValue)");
    value_.push_back(value.value_);
}

void JsonValue::Append(JsonValue&& value) {
    RequireArray(value_, "Append(JsonValue&&)");
    value_.push_back(std::move(value.value_));
}

void JsonValue::Insert(std::string_view key, const JsonValue& value) {
    RequireObject(value_, "Insert(key, JsonValue)");
    const auto key_string = std::string(key);
    if (value_.contains(key_string)) {
        throw std::logic_error("JsonValue::Insert(key, JsonValue) key already exists");
    }
    value_.emplace(key_string, value.value_);
}

void JsonValue::Insert(std::string_view key, JsonValue&& value) {
    RequireObject(value_, "Insert(key, JsonValue&&)");
    const auto key_string = std::string(key);
    if (value_.contains(key_string)) {
        throw std::logic_error("JsonValue::Insert(key, JsonValue&&) key already exists");
    }
    value_.emplace(key_string, std::move(value.value_));
}

void JsonValue::Insert(std::size_t index, const JsonValue& value) {
    RequireArray(value_, "Insert(index, JsonValue)");
    RequireInsertIndex(index, value_.size(), "Insert(index, JsonValue)");
    auto iter = value_.begin() + static_cast<nlohmann::json::difference_type>(index);
    value_.insert(iter, value.value_);
}

void JsonValue::Insert(std::size_t index, JsonValue&& value) {
    RequireArray(value_, "Insert(index, JsonValue&&)");
    RequireInsertIndex(index, value_.size(), "Insert(index, JsonValue&&)");
    auto iter = value_.begin() + static_cast<nlohmann::json::difference_type>(index);
    value_.insert(iter, std::move(value.value_));
}

const nlohmann::json& JsonValue::Raw() const noexcept {
    return value_;
}

nlohmann::json& JsonValue::Raw() noexcept {
    return value_;
}

std::string JsonValue::Serialize(int indent) const {
    return value_.dump(indent);
}

}  // namespace slg::json
