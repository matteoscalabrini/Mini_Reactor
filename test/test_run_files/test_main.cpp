#include <unity.h>
#include <string>
#include <vector>
#include "storage/RunFiles.hpp"

void setUp() {}
void tearDown() {}

void test_csv_and_name_path_zero_pad() {
  TEST_ASSERT_EQUAL_STRING("/runs/00007.csv", RunFiles::csvPath(7).c_str());
  TEST_ASSERT_EQUAL_STRING("/runs/00007.name", RunFiles::namePath(7).c_str());
  TEST_ASSERT_EQUAL_STRING("/runs/12345.csv", RunFiles::csvPath(12345).c_str());
}

void test_parse_id_basename_and_path() {
  TEST_ASSERT_EQUAL_INT(7, RunFiles::parseId("00007.csv"));
  TEST_ASSERT_EQUAL_INT(7, RunFiles::parseId("/runs/00007.csv"));
  TEST_ASSERT_EQUAL_INT(42, RunFiles::parseId("00042.csv"));
}

void test_parse_id_rejects_non_run_files() {
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("00007.name"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("reactor_log.csv"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("/runs/notes.txt"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId(""));
}

void test_next_id() {
  TEST_ASSERT_EQUAL_INT(1, RunFiles::nextId({}));
  TEST_ASSERT_EQUAL_INT(8, RunFiles::nextId({1, 7, 3}));
  TEST_ASSERT_EQUAL_INT(2, RunFiles::nextId({1}));
}

void test_sanitize_trims_strips_ctrl_truncates() {
  TEST_ASSERT_EQUAL_STRING("Ethanol distillation",
      RunFiles::sanitizeName("  Ethanol distillation  ").c_str());
  TEST_ASSERT_EQUAL_STRING("ab", RunFiles::sanitizeName(std::string("a\nb\t")).c_str());
  // 40 'x' truncates to 32
  TEST_ASSERT_EQUAL_UINT32(32u, (uint32_t)RunFiles::sanitizeName(std::string(40, 'x')).size());
  TEST_ASSERT_EQUAL_STRING("", RunFiles::sanitizeName("   ").c_str());
}

void test_label_fallback() {
  TEST_ASSERT_EQUAL_STRING("Run 7", RunFiles::label(7, "").c_str());
  TEST_ASSERT_EQUAL_STRING("Buffer prep", RunFiles::label(7, "Buffer prep").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_csv_and_name_path_zero_pad);
  RUN_TEST(test_parse_id_basename_and_path);
  RUN_TEST(test_parse_id_rejects_non_run_files);
  RUN_TEST(test_next_id);
  RUN_TEST(test_sanitize_trims_strips_ctrl_truncates);
  RUN_TEST(test_label_fallback);
  return UNITY_END();
}
