#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_mypl(const char* source, char* out, size_t out_size) {
    FILE* f = fopen("/tmp/test_phase9_src.mypl", "w");
    if (f == NULL) return -1;
    fprintf(f, "%s", source);
    fclose(f);

    int rc = system("./bin/mypl /tmp/test_phase9_src.mypl > /tmp/test_phase9_out.txt 2>&1");

    FILE* outf = fopen("/tmp/test_phase9_out.txt", "r");
    if (outf != NULL) {
        out[0] = '\0';
        size_t n = fread(out, 1, out_size - 1, outf);
        out[n] = '\0';
        fclose(outf);
    }
    return WEXITSTATUS(rc);
}

static int output_contains(const char* out, const char* substr) {
    return strstr(out, substr) != NULL;
}

TEST(phase9_dbms_output_buffer_and_get_lines) {
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    dbms_output.enable(10);\n"
        "    dbms_output.put_line(\"hello\");\n"
        "    dbms_output.put_line(\"world\");\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    print int_to_string(length(lines));\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "2"));
}

TEST(phase9_dbms_output_disabled_put_is_noop) {
    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    dbms_output.put_line(\"ignored\");\n"
        "    array<string> lines = dbms_output.get_lines();\n"
        "    print int_to_string(length(lines));\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "0"));
}

TEST(phase9_utl_file_write_and_read) {
    FILE* f = fopen("/tmp/test_phase9_file.txt", "w");
    if (f != NULL) fclose(f);

    char out[256];
    int rc = run_mypl(
        "proc main() -> int {\n"
        "    int h = utl_file.fopen(\"/tmp/test_phase9_file.txt\", \"w\");\n"
        "    utl_file.put_line(h, \"hello\");\n"
        "    utl_file.put_line(h, \"world\");\n"
        "    utl_file.fclose(h);\n"
        "\n"
        "    int r = utl_file.fopen(\"/tmp/test_phase9_file.txt\", \"r\");\n"
        "    string a = utl_file.get_line(r);\n"
        "    string b = utl_file.get_line(r);\n"
        "    utl_file.fclose(r);\n"
        "\n"
        "    print a;\n"
        "    print b;\n"
        "    return 0;\n"
        "}\n",
        out, sizeof(out));
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(1, output_contains(out, "hello"));
    ASSERT_INT_EQ(1, output_contains(out, "world"));
}

int main(void) {
    RUN_TEST(phase9_dbms_output_buffer_and_get_lines);
    RUN_TEST(phase9_dbms_output_disabled_put_is_noop);
    RUN_TEST(phase9_utl_file_write_and_read);
    TEST_SUMMARY();
}
