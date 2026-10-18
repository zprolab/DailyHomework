#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dhw {

struct JsonParse;

// Ordered JSON value: objects keep insertion order so that writing a data file
// back produces a stable, diff friendly result.
class Json {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(double value);
    Json(int value);
    Json(long long value);
    Json(const char* value);
    Json(std::string value);

    static Json array();
    static Json object();

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_number() const { return kind_ == Kind::Number; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_object() const { return kind_ == Kind::Object; }

    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0.0) const;
    long long as_int(long long fallback = 0) const;
    const std::string& as_string() const;
    std::string string_or(std::string fallback) const;

    std::size_t size() const;
    bool empty() const { return size() == 0; }

    const Json& at(std::size_t index) const;
    Json& at(std::size_t index);

    const Json* find(std::string_view key) const;
    Json& set(std::string key, Json value);
    bool erase(std::string_view key);

    const Array& elements() const { return array_; }
    const Object& members() const { return object_; }

    Json& push_back(Json value);

    // indent < 0 produces a single line; otherwise that many spaces per level.
    std::string dump(int indent = 2) const;

    static JsonParse parse(std::string_view text);

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

std::ostream& operator<<(std::ostream& out, const Json& value);

struct JsonParse {
    bool ok = false;
    Json value;
    std::string error;
};

}  // namespace dhw
