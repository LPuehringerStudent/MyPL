#include "test_harness.h"
#include "parser.h"

TEST(parser_returns_null_for_empty_source) {
    Program* program = parse("");
    ASSERT_PTR_NULL(program);
}

int main(void) {
    RUN_TEST(parser_returns_null_for_empty_source);
    TEST_SUMMARY();
}
