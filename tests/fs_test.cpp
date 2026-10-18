#include "dhw/fs.h"
#include "dhw_test.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct ScopedDir {
    explicit ScopedDir(const std::string& label) {
        static int counter = 0;
        path = dhw::fs::join(dhw::fs::temp_dir(),
                             "dhw_test_" + label + "_" + std::to_string(++counter));
        dhw::fs::remove_all(path);
        dhw::fs::create_directories(path);
    }
    ~ScopedDir() { dhw::fs::remove_all(path); }
    std::string file(const std::string& name) const { return dhw::fs::join(path, name); }
    std::string path;
};

}  // namespace

DHW_TEST(fs_path_helpers) {
    DHW_CHECK_EQ(dhw::fs::join("/a/b", "c.txt"), std::string("/a/b/c.txt"));
    DHW_CHECK_EQ(dhw::fs::join("/a/b/", "c.txt"), std::string("/a/b/c.txt"));
    DHW_CHECK_EQ(dhw::fs::join("", "c.txt"), std::string("c.txt"));
    DHW_CHECK_EQ(dhw::fs::join("/a/b", ""), std::string("/a/b"));
    DHW_CHECK_EQ(dhw::fs::parent("/a/b/c.txt"), std::string("/a/b"));
    DHW_CHECK_EQ(dhw::fs::filename("/a/b/c.txt"), std::string("c.txt"));
    DHW_CHECK_EQ(dhw::fs::stem("/a/b/c.txt"), std::string("c"));
    DHW_CHECK_EQ(dhw::fs::extension("/a/b/c.txt"), std::string(".txt"));
    DHW_CHECK_EQ(dhw::fs::filename("/数据/每日作业数据/data.json"), std::string("data.json"));
    DHW_CHECK_EQ(dhw::fs::parent("/数据/每日作业数据/data.json"), std::string("/数据/每日作业数据"));
    DHW_CHECK_EQ(dhw::fs::stem("data.json.tmp"), std::string("data.json"));
}

DHW_TEST(fs_text_round_trip) {
    ScopedDir dir("text");
    const std::string path = dir.file("data.json");
    const std::string text = "{\"subject\":\"语文\"}\n第二行\n";
    DHW_CHECK(dhw::fs::write_text(path, text));
    DHW_CHECK(dhw::fs::exists(path));
    DHW_CHECK(dhw::fs::is_regular_file(path));
    DHW_CHECK(!dhw::fs::is_directory(path));
    DHW_CHECK_EQ(dhw::fs::read_text(path).value(), text);
    DHW_CHECK_EQ(dhw::fs::file_size(path).value(), text.size());
    DHW_CHECK(dhw::fs::mtime_epoch_seconds(path).has_value());
}

DHW_TEST(fs_atomic_write_replaces_and_leaves_no_temp) {
    ScopedDir dir("atomic");
    const std::string path = dir.file("data.json");
    DHW_CHECK(dhw::fs::write_text_atomic(path, "first"));
    DHW_CHECK(dhw::fs::write_text_atomic(path, "second"));
    DHW_CHECK_EQ(dhw::fs::read_text(path).value(), std::string("second"));
    DHW_CHECK(!dhw::fs::exists(path + ".tmp"));
    const std::vector<std::string> names = dhw::fs::list_dir(dir.path);
    DHW_CHECK_EQ(names.size(), std::size_t{1});
    DHW_CHECK_EQ(names[0], std::string("data.json"));
}

DHW_TEST(fs_binary_bytes) {
    ScopedDir dir("bytes");
    const std::string path = dir.file("badge.png");
    const std::vector<std::uint8_t> data{0x89, 0x50, 0x4E, 0x47, 0x00, 0xFF, 0x00, 0x01};
    DHW_CHECK(dhw::fs::write_bytes_atomic(path, data.data(), data.size()));
    const std::vector<std::uint8_t> loaded = dhw::fs::read_bytes(path).value();
    DHW_CHECK_EQ(loaded.size(), data.size());
    DHW_CHECK(loaded == data);
    DHW_CHECK_EQ(dhw::fs::file_size(path).value(), data.size());
}

DHW_TEST(fs_missing_paths_are_reported) {
    ScopedDir dir("missing");
    DHW_CHECK(!dhw::fs::exists(dir.file("nope.json")));
    DHW_CHECK(!dhw::fs::read_text(dir.file("nope.json")).has_value());
    DHW_CHECK(!dhw::fs::read_bytes(dir.file("nope.png")).has_value());
    DHW_CHECK(!dhw::fs::file_size(dir.file("nope.png")).has_value());
    DHW_CHECK(!dhw::fs::mtime_epoch_seconds(dir.file("nope.png")).has_value());
    DHW_CHECK(!dhw::fs::write_text(dhw::fs::join(dir.path, "sub/dir/x.json"), "x"));
    DHW_CHECK_EQ(dhw::fs::list_dir(dir.file("nope")).size(), std::size_t{0});
}

DHW_TEST(fs_directory_and_move_operations) {
    ScopedDir dir("dirs");
    const std::string nested = dhw::fs::join(dir.path, "备份/backups");
    DHW_CHECK(dhw::fs::create_directories(nested));
    DHW_CHECK(dhw::fs::is_directory(nested));
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(nested, "b.json"), "b"));
    DHW_CHECK(dhw::fs::write_text(dhw::fs::join(nested, "a.json"), "a"));
    const std::vector<std::string> names = dhw::fs::list_dir(nested);
    DHW_CHECK_EQ(names.size(), std::size_t{2});
    DHW_CHECK_EQ(names[0], std::string("a.json"));
    DHW_CHECK_EQ(names[1], std::string("b.json"));

    const std::string source = dhw::fs::join(nested, "a.json");
    const std::string moved = dhw::fs::join(dir.path, "a.json");
    DHW_CHECK(dhw::fs::rename(source, moved));
    DHW_CHECK(!dhw::fs::exists(source));
    DHW_CHECK_EQ(dhw::fs::read_text(moved).value(), std::string("a"));

    const std::string copy = dhw::fs::join(dir.path, "a_copy.json");
    DHW_CHECK(dhw::fs::copy_file(moved, copy));
    DHW_CHECK_EQ(dhw::fs::read_text(copy).value(), std::string("a"));
    DHW_CHECK(dhw::fs::remove_file(copy));
    DHW_CHECK(!dhw::fs::exists(copy));
    DHW_CHECK(dhw::fs::remove_all(dir.path + "_gone_"));
    DHW_CHECK(dhw::fs::remove_all(nested));
    DHW_CHECK(!dhw::fs::exists(nested));
}

DHW_TEST(fs_finds_the_running_executable) {
    const std::string dir = dhw::fs::executable_dir();
    DHW_CHECK(!dir.empty());
    DHW_CHECK(dhw::fs::is_directory(dir));
    DHW_CHECK(!dhw::fs::home_dir().empty());
    DHW_CHECK(!dhw::fs::temp_dir().empty());
}
