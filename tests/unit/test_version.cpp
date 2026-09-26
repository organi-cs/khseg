#include <gtest/gtest.h>
#include <khseg/khseg.hpp>

#include <string_view>

TEST(Version, HeaderMatchesLibrary) {
  EXPECT_EQ(std::string_view(khseg::version()), KHSEG_VERSION_STRING);
}
