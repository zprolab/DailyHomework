#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dhw::fs {

// All paths handled by this module are UTF-8 encoded strings.

std::string join(std::string_view dir, std::string_view name);
std::string parent(std::string_view path);
std::string filename(std::string_view path);
std::string stem(std::string_view path);
std::string extension(std::string_view path);

bool exists(std::string_view path);
bool is_directory(std::string_view path);
bool is_regular_file(std::string_view path);
std::optional<std::uintmax_t> file_size(std::string_view path);
std::optional<std::int64_t> mtime_epoch_seconds(std::string_view path);

std::optional<std::string> read_text(std::string_view path);
std::optional<std::vector<std::uint8_t>> read_bytes(std::string_view path);
bool write_text(std::string_view path, std::string_view text);
bool write_bytes(std::string_view path, const void* data, std::size_t size);

// Writes through "<path>.tmp", flushes it to the device and then renames it over
// the target, so a crash can never leave a half written file behind.
bool write_text_atomic(std::string_view path, std::string_view text);
bool write_bytes_atomic(std::string_view path, const void* data, std::size_t size);

bool create_directories(std::string_view path);
bool remove_file(std::string_view path);
bool remove_all(std::string_view path);
bool rename(std::string_view from, std::string_view to);
bool copy_file(std::string_view from, std::string_view to, bool overwrite = true);

// Entry names (not full paths), sorted, without "." and "..".
std::vector<std::string> list_dir(std::string_view path);

std::string executable_dir();
std::string home_dir();
std::string temp_dir();

}  // namespace dhw::fs
