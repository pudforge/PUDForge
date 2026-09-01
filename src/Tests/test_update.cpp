// Reading the releases feed. The sample is the shape GitHub's API answers
// with, trimmed: the members the parser keys on are in the order the real
// reply has them, because the parser relies on that order for `html_url`.

#include "harness.hpp"

#include "../PUDForgeWin/UpdateFeed.hpp"

TEST_GROUP("update")

using pfwin::CompareVersions;
using pfwin::PackVersion;
using pfwin::ParseLatestRelease;
using pfwin::ReleaseInfo;
using pfwin::ReleaseNotesPlain;

namespace {

const char kSample[] =
    "{\"url\":\"https://api.github.com/repos/pudforge/PUDForge/releases/1\","
    "\"assets_url\":\"https://api.github.com/repos/pudforge/PUDForge/releases/1/assets\","
    "\"html_url\":\"https://github.com/pudforge/PUDForge/releases/tag/v0.1.70\","
    "\"id\":1,\"author\":{\"login\":\"pudforge\","
    "\"html_url\":\"https://github.com/pudforge\"},"
    "\"tag_name\":\"v0.1.70\",\"name\":\"v0.1.70\",\"draft\":false,\"prerelease\":false,"
    "\"assets\":[{\"name\":\"SHA256SUMS\",\"browser_download_url\":"
    "\"https://github.com/pudforge/PUDForge/releases/download/v0.1.70/SHA256SUMS\"},"
    "{\"name\":\"PUDForge.exe\",\"content_type\":\"application/x-msdownload\",\"size\":1145344,"
    "\"browser_download_url\":"
    "\"https://github.com/pudforge/PUDForge/releases/download/v0.1.70/PUDForge.exe\"}],"
    "\"body\":\"**New**\\r\\n\\r\\n- Generate a Map has a Mirror row \\u2014 the map reflects.\\r\\n"
    "\\r\\n---\\r\\n\\r\\nA Warcraft II map editor.\\r\\n\\r\\n- SHA-256 "
    "`95AEC18649232F1765043F34B57101145504E6D595168E406F8E5DFCA9F5C68F`\\r\\n\"}";

}  // namespace

TEST(the_latest_release_is_read_off_the_feed) {
  ReleaseInfo info;
  CHECK(ParseLatestRelease(kSample, info));
  CHECK(info.version == "0.1.70");
  CHECK(info.page_url == "https://github.com/pudforge/PUDForge/releases/tag/v0.1.70");
  // The exe, not the checksum file listed before it.
  CHECK(info.exe_url ==
        "https://github.com/pudforge/PUDForge/releases/download/v0.1.70/PUDForge.exe");
  // Lower-cased, so it compares with what a hex printer writes.
  CHECK(info.sha256 == "95aec18649232f1765043f34b57101145504e6d595168e406f8e5dfca9f5c68f");
  // The escape came through as the character.
  CHECK(info.notes.find("\xE2\x80\x94") != std::string::npos);
}

TEST(a_release_without_the_exe_says_so) {
  const std::string sample =
      "{\"tag_name\":\"v0.2.0\",\"html_url\":\"h\",\"assets\":[],\"body\":\"\"}";
  ReleaseInfo info;
  CHECK(ParseLatestRelease(sample, info));
  CHECK(info.version == "0.2.0");
  CHECK(info.exe_url.empty());
  CHECK(info.sha256.empty());
  // And no tag at all is no release.
  CHECK(!ParseLatestRelease("{\"message\":\"Not Found\"}", info));
}

TEST(versions_compare_as_numbers) {
  CHECK(CompareVersions("0.1.70", "0.1.9") > 0);
  CHECK(CompareVersions("0.1.9", "0.1.70") < 0);
  CHECK(CompareVersions("v0.1.70", "0.1.70") == 0);
  CHECK(CompareVersions("1.0", "0.9.99") > 0);
  CHECK(CompareVersions("0.1.70", "0.1.70.1") < 0);
  CHECK_EQ(int(PackVersion("0.1.70")), 0x000146);
  CHECK(PackVersion("v0.1.71") > PackVersion("0.1.70"));
}

TEST(release_notes_stop_at_the_rule) {
  ReleaseInfo info;
  CHECK(ParseLatestRelease(kSample, info));
  const std::string plain = ReleaseNotesPlain(info.notes);
  CHECK(plain.find("Warcraft II map editor") == std::string::npos);
  CHECK(plain.find("SHA-256") == std::string::npos);
  CHECK(plain.find("**") == std::string::npos);
  CHECK(plain.find('\r') == std::string::npos);
  CHECK(plain.rfind("New\n", 0) == 0);
  CHECK(plain.find("- Generate a Map") != std::string::npos);
  CHECK(plain.back() != '\n');
}
