#include "dhw/version.h"
#include "dhw_test.h"

#include <cstring>

DHW_TEST(version_is_exposed) {
    DHW_CHECK(std::strcmp(dhw::version(), dhw::kAppVersion) == 0);
    DHW_CHECK_EQ(dhw::kDataVersion, 1);
}
