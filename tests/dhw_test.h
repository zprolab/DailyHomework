#pragma once

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace dhw::test {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)());
};

struct Failure {
    std::string message;
};

int run_all(const std::vector<std::string>& filters);

template <typename T, typename = void>
struct is_streamable : std::false_type {};

template <typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string to_text(const T& value) {
    if constexpr (is_streamable<T>::value) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

}  // namespace dhw::test

#define DHW_TEST(name)                                                          \
    static void name();                                                         \
    static const ::dhw::test::Registrar dhw_registrar_##name(#name, &name);     \
    static void name()

#define DHW_CHECK(cond)                                                          \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::ostringstream dhw_os;                                           \
            dhw_os << __FILE__ << ":" << __LINE__ << ": CHECK failed: " << #cond; \
            throw ::dhw::test::Failure{dhw_os.str()};                             \
        }                                                                        \
    } while (false)

#define DHW_CHECK_EQ(a, b)                                                        \
    do {                                                                          \
        const auto dhw_a = (a);                                                   \
        const auto dhw_b = (b);                                                   \
        if (!(dhw_a == dhw_b)) {                                                  \
            std::ostringstream dhw_os;                                            \
            dhw_os << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " << #a   \
                   << " == " << #b << "\n  left  = " << ::dhw::test::to_text(dhw_a) \
                   << "\n  right = " << ::dhw::test::to_text(dhw_b);              \
            throw ::dhw::test::Failure{dhw_os.str()};                              \
        }                                                                         \
    } while (false)
