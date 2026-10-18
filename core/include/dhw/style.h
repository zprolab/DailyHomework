#pragma once

#include <string>

#include "dhw/json.h"
#include "dhw/layout.h"

namespace dhw {

// Lenient readers: a missing field keeps its default, an unknown key is ignored
// and a field of the wrong type keeps its default. They never fail.
LayoutStyle style_from_json(const Json& root);
// Keys are written in LayoutStyle declaration order so the file stays readable.
Json style_to_json(const LayoutStyle& style);

struct StyleLoad {
    LayoutStyle style;
    bool loaded = false;
    std::string error;
};

// A missing file is not an error: the defaults come back with an empty error.
// Unreadable or malformed content reports the reason and still yields defaults.
StyleLoad load_style_file(const std::string& path);
bool save_style_file(const std::string& path, const LayoutStyle& style);

inline constexpr const char* kStyleFileName = "style.json";

}  // namespace dhw
