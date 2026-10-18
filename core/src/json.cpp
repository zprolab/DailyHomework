#include "dhw/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>

namespace dhw {
namespace {

constexpr int kMaxDepth = 128;

const std::string kEmptyString;

void append_escaped(std::string& out, const std::string& value) {
    out.push_back('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
}

bool round_trips(const char* text, double value) {
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    return end != nullptr && *end == '\0' && parsed == value;
}

std::string format_number(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        return "null";
    }
    if (value == 0.0) {
        return std::signbit(value) ? "-0" : "0";
    }
    if (value == std::floor(value) && std::abs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        return buffer;
    }
    for (int precision = 15; precision <= 17; ++precision) {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (round_trips(buffer, value)) {
            return buffer;
        }
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

void append_utf8(std::string& out, unsigned int code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

class Parser {
public:
    Parser(std::string_view text) : text_(text) {}

    bool run(Json& out, std::string& error) {
        skip_bom();
        skip_space();
        if (!parse_value(out, 0)) {
            error = error_;
            return false;
        }
        skip_space();
        if (pos_ != text_.size()) {
            error = fail("trailing characters after the top level value");
            return false;
        }
        return true;
    }

private:
    std::string fail(const std::string& message) {
        int line = 1;
        int column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), " (line %d, column %d)", line, column);
        return message + buffer;
    }

    void skip_bom() {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
    }

    void skip_space() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool literal(std::string_view word) {
        if (text_.compare(pos_, word.size(), word) == 0) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    bool parse_value(Json& out, int depth) {
        if (depth > kMaxDepth) {
            error_ = fail("maximum nesting depth exceeded");
            return false;
        }
        if (pos_ >= text_.size()) {
            error_ = fail("unexpected end of input");
            return false;
        }
        switch (text_[pos_]) {
            case 'n':
                if (!literal("null")) {
                    error_ = fail("invalid literal");
                    return false;
                }
                out = Json();
                return true;
            case 't':
                if (!literal("true")) {
                    error_ = fail("invalid literal");
                    return false;
                }
                out = Json(true);
                return true;
            case 'f':
                if (!literal("false")) {
                    error_ = fail("invalid literal");
                    return false;
                }
                out = Json(false);
                return true;
            case '"': {
                std::string value;
                if (!parse_string(value)) {
                    return false;
                }
                out = Json(std::move(value));
                return true;
            }
            case '[':
                return parse_array(out, depth);
            case '{':
                return parse_object(out, depth);
            default:
                return parse_number(out);
        }
    }

    bool parse_array(Json& out, int depth) {
        ++pos_;  // '['
        Json result = Json::array();
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            out = std::move(result);
            return true;
        }
        while (true) {
            Json item;
            skip_space();
            if (!parse_value(item, depth + 1)) {
                return false;
            }
            result.push_back(std::move(item));
            skip_space();
            if (pos_ >= text_.size()) {
                error_ = fail("unterminated array");
                return false;
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                out = std::move(result);
                return true;
            }
            error_ = fail("expected ',' or ']' in array");
            return false;
        }
    }

    bool parse_object(Json& out, int depth) {
        ++pos_;  // '{'
        Json result = Json::object();
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            out = std::move(result);
            return true;
        }
        while (true) {
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                error_ = fail("expected a string key");
                return false;
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                error_ = fail("expected ':' after an object key");
                return false;
            }
            ++pos_;
            skip_space();
            Json value;
            if (!parse_value(value, depth + 1)) {
                return false;
            }
            // Duplicate keys keep the last value, matching serde_json.
            result.set(std::move(key), std::move(value));
            skip_space();
            if (pos_ >= text_.size()) {
                error_ = fail("unterminated object");
                return false;
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                out = std::move(result);
                return true;
            }
            error_ = fail("expected ',' or '}' in object");
            return false;
        }
    }

    bool parse_hex4(unsigned int& code_point) {
        if (pos_ + 4 > text_.size()) {
            error_ = fail("truncated \\u escape");
            return false;
        }
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = text_[pos_ + static_cast<std::size_t>(i)];
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value |= static_cast<unsigned int>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= static_cast<unsigned int>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value |= static_cast<unsigned int>(ch - 'A' + 10);
            } else {
                error_ = fail("invalid hex digit in \\u escape");
                return false;
            }
        }
        pos_ += 4;
        code_point = value;
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (true) {
            if (pos_ >= text_.size()) {
                error_ = fail("unterminated string");
                return false;
            }
            const char ch = text_[pos_];
            if (ch == '"') {
                ++pos_;
                return true;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                error_ = fail("raw control character in string");
                return false;
            }
            if (ch != '\\') {
                out.push_back(ch);
                ++pos_;
                continue;
            }
            ++pos_;
            if (pos_ >= text_.size()) {
                error_ = fail("unterminated escape sequence");
                return false;
            }
            const char escape = text_[pos_++];
            switch (escape) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned int first = 0;
                    if (!parse_hex4(first)) {
                        return false;
                    }
                    unsigned int code_point = first;
                    if (first >= 0xD800 && first <= 0xDBFF) {
                        if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                            error_ = fail("unpaired high surrogate");
                            return false;
                        }
                        pos_ += 2;
                        unsigned int second = 0;
                        if (!parse_hex4(second)) {
                            return false;
                        }
                        if (second < 0xDC00 || second > 0xDFFF) {
                            error_ = fail("invalid low surrogate");
                            return false;
                        }
                        code_point = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                    } else if (first >= 0xDC00 && first <= 0xDFFF) {
                        error_ = fail("unpaired low surrogate");
                        return false;
                    }
                    append_utf8(out, code_point);
                    break;
                }
                default:
                    error_ = fail("invalid escape sequence");
                    return false;
            }
        }
    }

    bool parse_number(Json& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
            error_ = fail("invalid value");
            return false;
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                error_ = fail("invalid number: expected a digit after '.'");
                return false;
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                error_ = fail("invalid number: expected an exponent digit");
                return false;
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        const std::string text(text_.substr(start, pos_ - start));
        out = Json(std::strtod(text.c_str(), nullptr));
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

void dump_value(const Json& value, int indent, int level, std::string& out) {
    const bool pretty = indent >= 0;
    const std::string pad = pretty ? std::string(static_cast<std::size_t>(indent) * level, ' ') : std::string();
    const std::string inner_pad =
        pretty ? std::string(static_cast<std::size_t>(indent) * (level + 1), ' ') : std::string();

    switch (value.kind()) {
        case Json::Kind::Null:
            out += "null";
            break;
        case Json::Kind::Bool:
            out += value.as_bool() ? "true" : "false";
            break;
        case Json::Kind::Number:
            out += format_number(value.as_number());
            break;
        case Json::Kind::String:
            append_escaped(out, value.as_string());
            break;
        case Json::Kind::Array: {
            const Json::Array& items = value.elements();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out += ',';
                }
                if (pretty) {
                    out += '\n';
                    out += inner_pad;
                }
                dump_value(items[i], indent, level + 1, out);
            }
            if (pretty) {
                out += '\n';
                out += pad;
            }
            out += ']';
            break;
        }
        case Json::Kind::Object: {
            const Json::Object& items = value.members();
            if (items.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out += ',';
                }
                if (pretty) {
                    out += '\n';
                    out += inner_pad;
                }
                append_escaped(out, items[i].first);
                out += pretty ? ": " : ":";
                dump_value(items[i].second, indent, level + 1, out);
            }
            if (pretty) {
                out += '\n';
                out += pad;
            }
            out += '}';
            break;
        }
    }
}

}  // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) : kind_(Kind::Null) {}
Json::Json(bool value) : kind_(Kind::Bool), bool_(value) {}
Json::Json(double value) : kind_(Kind::Number), number_(value) {}
Json::Json(int value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
Json::Json(long long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
Json::Json(const char* value) : kind_(Kind::String), string_(value == nullptr ? "" : value) {}
Json::Json(std::string value) : kind_(Kind::String), string_(std::move(value)) {}

Json Json::array() {
    Json value;
    value.kind_ = Kind::Array;
    return value;
}

Json Json::object() {
    Json value;
    value.kind_ = Kind::Object;
    return value;
}

bool Json::as_bool(bool fallback) const {
    return is_bool() ? bool_ : fallback;
}

double Json::as_number(double fallback) const {
    return is_number() ? number_ : fallback;
}

long long Json::as_int(long long fallback) const {
    if (!is_number()) {
        return fallback;
    }
    return static_cast<long long>(number_ < 0 ? number_ - 0.5 : number_ + 0.5);
}

const std::string& Json::as_string() const {
    return is_string() ? string_ : kEmptyString;
}

std::string Json::string_or(std::string fallback) const {
    return is_string() ? string_ : std::move(fallback);
}

std::size_t Json::size() const {
    if (is_array()) {
        return array_.size();
    }
    if (is_object()) {
        return object_.size();
    }
    if (is_string()) {
        return string_.size();
    }
    return 0;
}

const Json& Json::at(std::size_t index) const {
    static const Json kNull;
    if (!is_array() || index >= array_.size()) {
        return kNull;
    }
    return array_[index];
}

Json& Json::at(std::size_t index) {
    static Json kNull;
    if (!is_array() || index >= array_.size()) {
        kNull = Json();
        return kNull;
    }
    return array_[index];
}

const Json* Json::find(std::string_view key) const {
    if (!is_object()) {
        return nullptr;
    }
    for (const Member& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

Json& Json::set(std::string key, Json value) {
    if (kind_ != Kind::Object) {
        *this = Json::object();
    }
    for (Member& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return member.second;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
    return object_.back().second;
}

bool Json::erase(std::string_view key) {
    if (!is_object()) {
        return false;
    }
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return true;
        }
    }
    return false;
}

Json& Json::push_back(Json value) {
    if (kind_ != Kind::Array) {
        *this = Json::array();
    }
    array_.push_back(std::move(value));
    return array_.back();
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_value(*this, indent, 0, out);
    return out;
}

JsonParse Json::parse(std::string_view text) {
    JsonParse result;
    Parser parser(text);
    result.ok = parser.run(result.value, result.error);
    if (!result.ok) {
        result.value = Json();
    }
    return result;
}

std::ostream& operator<<(std::ostream& out, const Json& value) {
    return out << value.dump(-1);
}

}  // namespace dhw
