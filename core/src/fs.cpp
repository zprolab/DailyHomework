#include "dhw/fs.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <io.h>       // _commit / _fileno for flushing a file to disk
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace dhw::fs {
namespace {

namespace stdfs = std::filesystem;

stdfs::path to_native(std::string_view utf8) {
#if defined(_WIN32)
    return stdfs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
    return stdfs::path(std::string(utf8));
#endif
}

std::string from_native(const stdfs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// std::fopen / std::remove take a narrow path on Windows and interpret it in the
// active ANSI code page, which silently fails for any non-ASCII path. File BOMs and
// Chinese file names are ordinary here, so the wide CRT entry points are used instead.
#if defined(_WIN32)
std::FILE* open_binary_write(const stdfs::path& path) {
    return _wfopen(path.c_str(), L"wb");
}

void remove_native(const stdfs::path& path) {
    _wremove(path.c_str());
}
#else
std::FILE* open_binary_write(const stdfs::path& path) {
    return std::fopen(path.c_str(), "wb");
}

void remove_native(const stdfs::path& path) {
    std::remove(path.c_str());
}
#endif

bool flush_to_disk(std::FILE* file) {
    if (std::fflush(file) != 0) {
        return false;
    }
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(::fileno(file)) == 0;
#endif
}

#if defined(_WIN32)
// POSIX rename() replaces an existing target atomically; the Windows CRT does not, so
// the Win32 call with MOVEFILE_REPLACE_EXISTING is used for the same guarantee.
bool replace_existing(std::string_view from, std::string_view to) {
    const stdfs::path from_path = to_native(from);
    const stdfs::path to_path = to_native(to);
    return ::MoveFileExW(from_path.wstring().c_str(), to_path.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#endif

bool write_atomic(std::string_view path, const void* data, std::size_t size) {
    const std::string target(path);
    const std::string temporary = target + ".tmp";
    std::FILE* file = open_binary_write(to_native(temporary));
    if (file == nullptr) {
        return false;
    }
    const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
    const bool flushed = flush_to_disk(file);
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) {
        remove_native(to_native(temporary));
        return false;
    }
#if defined(_WIN32)
    if (!replace_existing(temporary, target)) {
        remove_native(to_native(temporary));
        return false;
    }
#else
    std::error_code error;
    stdfs::rename(to_native(temporary), to_native(target), error);
    if (error) {
        remove_native(to_native(temporary));
        return false;
    }
#endif
    return true;
}

}  // namespace

std::string join(std::string_view dir, std::string_view name) {
    if (dir.empty()) {
        return std::string(name);
    }
    if (name.empty()) {
        return std::string(dir);
    }
    if (dir.back() == '/' || dir.back() == '\\') {
        return std::string(dir) + std::string(name);
    }
    return std::string(dir) + "/" + std::string(name);
}

std::string parent(std::string_view path) {
    const stdfs::path native = to_native(path);
    const std::string result = from_native(native.parent_path());
    return result;
}

std::string filename(std::string_view path) {
    return from_native(to_native(path).filename());
}

std::string stem(std::string_view path) {
    return from_native(to_native(path).stem());
}

std::string extension(std::string_view path) {
    return from_native(to_native(path).extension());
}

bool exists(std::string_view path) {
    std::error_code error;
    return stdfs::exists(to_native(path), error) && !error;
}

bool is_directory(std::string_view path) {
    std::error_code error;
    return stdfs::is_directory(to_native(path), error) && !error;
}

bool is_regular_file(std::string_view path) {
    std::error_code error;
    return stdfs::is_regular_file(to_native(path), error) && !error;
}

std::optional<std::uintmax_t> file_size(std::string_view path) {
    std::error_code error;
    const std::uintmax_t size = stdfs::file_size(to_native(path), error);
    if (error) {
        return std::nullopt;
    }
    return size;
}

std::optional<std::int64_t> mtime_epoch_seconds(std::string_view path) {
    std::error_code error;
    const stdfs::file_time_type time = stdfs::last_write_time(to_native(path), error);
    if (error) {
        return std::nullopt;
    }
    // file_clock has an implementation defined epoch: 1601-01-01 on Windows, the Unix
    // epoch on the POSIX platforms we target. Convert to Unix epoch seconds so callers
    // can compare the value with std::time(nullptr).
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
#if defined(_WIN32)
    constexpr std::int64_t kFileTimeToUnixEpoch = 11644473600LL;  // seconds between 1601 and 1970
    return static_cast<std::int64_t>(seconds) - kFileTimeToUnixEpoch;
#else
    return static_cast<std::int64_t>(seconds);
#endif
}

std::optional<std::string> read_text(std::string_view path) {
    std::ifstream stream(to_native(path), std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> read_bytes(std::string_view path) {
    std::ifstream stream(to_native(path), std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
    if (stream.bad()) {
        return std::nullopt;
    }
    return data;
}

bool write_text(std::string_view path, std::string_view text) {
    return write_bytes(path, text.data(), text.size());
}

bool write_bytes(std::string_view path, const void* data, std::size_t size) {
    std::FILE* file = open_binary_write(to_native(path));
    if (file == nullptr) {
        return false;
    }
    const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}

bool write_text_atomic(std::string_view path, std::string_view text) {
    return write_atomic(path, text.data(), text.size());
}

bool write_bytes_atomic(std::string_view path, const void* data, std::size_t size) {
    return write_atomic(path, data, size);
}

bool create_directories(std::string_view path) {
    std::error_code error;
    stdfs::create_directories(to_native(path), error);
    return !error;
}

bool remove_file(std::string_view path) {
    std::error_code error;
    return stdfs::remove(to_native(path), error) && !error;
}

bool remove_all(std::string_view path) {
    std::error_code error;
    stdfs::remove_all(to_native(path), error);
    return !error;
}

bool rename(std::string_view from, std::string_view to) {
#if defined(_WIN32)
    return replace_existing(from, to);
#else
    std::error_code error;
    stdfs::rename(to_native(from), to_native(to), error);
    return !error;
#endif
}

bool copy_file(std::string_view from, std::string_view to, bool overwrite) {
    std::error_code error;
    const auto options = overwrite ? stdfs::copy_options::overwrite_existing : stdfs::copy_options::none;
    return stdfs::copy_file(to_native(from), to_native(to), options, error) && !error;
}

std::vector<std::string> list_dir(std::string_view path) {
    std::vector<std::string> names;
    std::error_code error;
    stdfs::directory_iterator iterator(to_native(path), error);
    if (error) {
        return names;
    }
    for (const stdfs::directory_entry& entry : iterator) {
        const std::string name = from_native(entry.path().filename());
        if (name == "." || name == "..") {
            continue;
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string executable_dir() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    const stdfs::path path(buffer);
    return from_native(path.parent_path());
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    std::error_code error;
    const stdfs::path resolved = stdfs::weakly_canonical(to_native(buffer.c_str()), error);
    return from_native((error ? to_native(buffer.c_str()) : resolved).parent_path());
#else
    std::error_code error;
    const stdfs::path resolved = stdfs::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return from_native(resolved.parent_path());
#endif
}

std::string home_dir() {
#if defined(_WIN32)
    const wchar_t* value = _wgetenv(L"USERPROFILE");
    if (value == nullptr) {
        return {};
    }
    return from_native(stdfs::path(value));
#else
    const char* value = std::getenv("HOME");
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::string temp_dir() {
    std::error_code error;
    const stdfs::path path = stdfs::temp_directory_path(error);
    if (error) {
        return home_dir();
    }
    return from_native(path);
}

}  // namespace dhw::fs
