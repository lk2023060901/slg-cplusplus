#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include "json/json_export.h"

namespace slg::json {

class JsonValue {
public:
    using ValueType = nlohmann::json::value_t;

    SLG_JSON_API JsonValue();
    SLG_JSON_API explicit JsonValue(nlohmann::json value);

    SLG_JSON_API static JsonValue Object();
    SLG_JSON_API static JsonValue Array();

    SLG_JSON_API ValueType GetType() const noexcept;
    SLG_JSON_API bool IsNull() const noexcept;
    SLG_JSON_API bool IsObject() const noexcept;
    SLG_JSON_API bool IsArray() const noexcept;

    SLG_JSON_API std::optional<JsonValue> Get(std::string_view key) const;
    SLG_JSON_API std::optional<JsonValue> Get(std::size_t index) const;

    template <typename T>
    void Set(T&& value);

    template <typename T>
    std::optional<T> Get() const;

    template <typename T>
    std::optional<T> GetAs(std::string_view key) const;

    template <typename T>
    std::optional<T> GetAs(std::size_t index) const;

    template <typename T>
    std::optional<T> As() const;

    SLG_JSON_API void Set(std::string_view key, const JsonValue& value);
    SLG_JSON_API void Set(std::string_view key, JsonValue&& value);

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, JsonValue>>>
    void Set(std::string_view key, T&& value);

    SLG_JSON_API void Append(const JsonValue& value);
    SLG_JSON_API void Append(JsonValue&& value);

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, JsonValue>>>
    void Append(T&& value);

    SLG_JSON_API void Insert(std::string_view key, const JsonValue& value);
    SLG_JSON_API void Insert(std::string_view key, JsonValue&& value);

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, JsonValue>>>
    void Insert(std::string_view key, T&& value);

    SLG_JSON_API void Insert(std::size_t index, const JsonValue& value);
    SLG_JSON_API void Insert(std::size_t index, JsonValue&& value);

    template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, JsonValue>>>
    void Insert(std::size_t index, T&& value);

    SLG_JSON_API const nlohmann::json& Raw() const noexcept;
    SLG_JSON_API nlohmann::json& Raw() noexcept;

    SLG_JSON_API std::string Serialize(int indent = -1) const;

private:
    nlohmann::json value_;
};

// Template implementations ---------------------------------------------------

template <typename T>
void JsonValue::Set(T&& value) {
    value_ = std::forward<T>(value);
}

template <typename T>
std::optional<T> JsonValue::Get() const {
    return As<T>();
}

template <typename T>
std::optional<T> JsonValue::GetAs(std::string_view key) const {
    const auto nested = Get(key);
    if (!nested.has_value()) {
        return std::nullopt;
    }

    return nested->template Get<T>();
}

template <typename T>
std::optional<T> JsonValue::GetAs(std::size_t index) const {
    const auto nested = Get(index);
    if (!nested.has_value()) {
        return std::nullopt;
    }

    return nested->template Get<T>();
}

template <typename T>
std::optional<T> JsonValue::As() const {
    if constexpr (std::is_same_v<std::decay_t<T>, JsonValue>) {
        return JsonValue(value_);
    }

    try {
        return value_.get<T>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

template <typename T, typename>
void JsonValue::Set(std::string_view key, T&& value) {
    if (!IsObject()) {
        throw std::logic_error("JsonValue must be an object for Set(key, value)");
    }
    value_[std::string(key)] = std::forward<T>(value);
}

template <typename T, typename>
void JsonValue::Append(T&& value) {
    if (!IsArray()) {
        throw std::logic_error("JsonValue must be an array for Append");
    }
    value_.push_back(std::forward<T>(value));
}

template <typename T, typename>
void JsonValue::Insert(std::string_view key, T&& value) {
    if (!IsObject()) {
        throw std::logic_error("JsonValue must be an object for Insert(key, value)");
    }
    const auto key_string = std::string(key);
    if (value_.contains(key_string)) {
        throw std::logic_error("JsonValue object already contains the key");
    }
    value_.emplace(key_string, std::forward<T>(value));
}

template <typename T, typename>
void JsonValue::Insert(std::size_t index, T&& value) {
    if (!IsArray()) {
        throw std::logic_error("JsonValue must be an array for Insert(index, value)");
    }
    if (index > value_.size()) {
        throw std::out_of_range("Insert index exceeds array size");
    }
    auto iter = value_.begin() + static_cast<nlohmann::json::difference_type>(index);
    value_.insert(iter, std::forward<T>(value));
}

}  // namespace slg::json
