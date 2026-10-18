#include "dhw_test.h"

#include <cstdio>
#include <cstring>
#include <exception>

namespace dhw::test {
namespace {

bool matches(const char* name, const std::vector<std::string>& filters) {
    if (filters.empty()) {
        return true;
    }
    for (const std::string& filter : filters) {
        if (std::strstr(name, filter.c_str()) != nullptr) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(const char* name, void (*fn)()) {
    registry().push_back(Case{name, fn});
}

int run_all(const std::vector<std::string>& filters) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const Case& item : registry()) {
        if (!matches(item.name, filters)) {
            ++skipped;
            continue;
        }
        try {
            item.fn();
            std::printf("[ ok ] %s\n", item.name);
            ++passed;
        } catch (const Failure& failure) {
            std::printf("[fail] %s\n       %s\n", item.name, failure.message.c_str());
            ++failed;
        } catch (const std::exception& error) {
            std::printf("[fail] %s\n       unexpected exception: %s\n", item.name, error.what());
            ++failed;
        } catch (...) {
            std::printf("[fail] %s\n       unexpected unknown exception\n", item.name);
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed", passed, failed);
    if (skipped > 0) {
        std::printf(", %d filtered out", skipped);
    }
    std::printf("\n");
    return failed == 0 ? 0 : 1;
}

}  // namespace dhw::test

int main(int argc, char** argv) {
    std::vector<std::string> filters;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const dhw::test::Case& item : dhw::test::registry()) {
                std::printf("%s\n", item.name);
            }
            return 0;
        }
        filters.emplace_back(argv[i]);
    }
    return dhw::test::run_all(filters);
}
