/* From orbit-os tests/test_shell_parse.c (MIT), unchanged. */
#include <stdint.h>
#include "test.h"
#include "shell_parse.h"

static char echoed[512];
static size_t echoed_len;

static void cap(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    memcpy(echoed + echoed_len, s, n);
    echoed_len += n;
    echoed[echoed_len] = 0;
}

static enum line_result feed_str(struct line_editor *le, const char *s)
{
    enum line_result r = LINE_PENDING;
    while (*s)
        r = le_feed(le, *s++);
    return r;
}

static void test_basic_line(void)
{
    struct line_editor le;
    le_init(&le, cap, NULL);
    echoed_len = 0;
    CHECK(feed_str(&le, "help") == LINE_PENDING);
    CHECK(le_feed(&le, '\r') == LINE_READY);
    CHECK_STR(le.buf, "help");
    CHECK_STR(echoed, "help\r\n");
    /* the LF of a CRLF pair is swallowed, a lone LF also ends a line */
    CHECK(le_feed(&le, '\n') == LINE_PENDING);
    CHECK(feed_str(&le, "ps\n") == LINE_READY);
    CHECK_STR(le.buf, "ps");
}

static void test_backspace_and_kill(void)
{
    struct line_editor le;
    le_init(&le, cap, NULL);
    echoed_len = 0;
    feed_str(&le, "pz");
    le_feed(&le, 0x7f);              /* DEL */
    feed_str(&le, "s");
    CHECK(le_feed(&le, '\r') == LINE_READY);
    CHECK_STR(le.buf, "ps");
    CHECK(strstr(echoed, "\b \b") != NULL);
    feed_str(&le, "garbage");
    le_feed(&le, 0x15);              /* Ctrl-U */
    feed_str(&le, "uptime\r");
    CHECK_STR(le.buf, "uptime");
    le_feed(&le, 0x08);              /* backspace on empty line is a no-op */
    CHECK(le.len == 0);
}

static void test_escape_and_ctrl(void)
{
    struct line_editor le;
    le_init(&le, NULL, NULL);
    feed_str(&le, "ec\x1b[Aho");       /* up-arrow ignored */
    CHECK(le_feed(&le, '\r') == LINE_READY);
    CHECK_STR(le.buf, "echo");
    feed_str(&le, "abc");
    CHECK(le_feed(&le, 0x03) == LINE_CANCELLED);
    CHECK(le.len == 0);
    feed_str(&le, "a\x01\x02" "b\xff\r");  /* other control / high bytes dropped */
    CHECK_STR(le.buf, "ab");
}

static void test_overflow(void)
{
    struct line_editor le;
    le_init(&le, NULL, NULL);
    for (int i = 0; i < 500; i++)
        le_feed(&le, 'x');
    CHECK(le.len == LINE_MAX_LEN - 1);
    CHECK(le_feed(&le, '\r') == LINE_READY);
    CHECK(strlen(le.buf) == LINE_MAX_LEN - 1);
}

static void test_tokenize(void)
{
    char line[128], *argv[SHELL_MAX_ARGS];
    int argc;
    strcpy(line, "  peek   0x40000000  16 ");
    argc = shell_tokenize(line, argv, SHELL_MAX_ARGS);
    CHECK(argc == 3);
    CHECK_STR(argv[0], "peek");
    CHECK_STR(argv[1], "0x40000000");
    CHECK_STR(argv[2], "16");
    strcpy(line, "echo \"hello   world\" x\"y z\"w");
    argc = shell_tokenize(line, argv, SHELL_MAX_ARGS);
    CHECK(argc == 3);
    CHECK_STR(argv[1], "hello   world");
    CHECK_STR(argv[2], "xy zw");
    strcpy(line, "echo \"unterminated");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == -1);
    strcpy(line, "a b c d e f g h i");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == -1);
    strcpy(line, "   ");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == 0);
    strcpy(line, "tab\tsep");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == 2);
}

static void test_parse_u64(void)
{
    uint64_t v;
    CHECK(parse_u64("0", &v) == 0 && v == 0);
    CHECK(parse_u64("12345", &v) == 0 && v == 12345);
    CHECK(parse_u64("0x107d001000", &v) == 0 && v == 0x107d001000ULL);
    CHECK(parse_u64("0XFFff", &v) == 0 && v == 0xffff);
    CHECK(parse_u64("0x4000_0000", &v) == 0 && v == 0x40000000);
    CHECK(parse_u64("18446744073709551615", &v) == 0 && v == UINT64_MAX);
    CHECK(parse_u64("18446744073709551616", &v) == -1);
    CHECK(parse_u64("0x1ffffffffffffffff", &v) == -1);
    CHECK(parse_u64("", &v) == -1);
    CHECK(parse_u64("0x", &v) == -1);
    CHECK(parse_u64("12a", &v) == -1);
    CHECK(parse_u64("-1", &v) == -1);
}

int main(void)
{
    RUN(test_basic_line);
    RUN(test_backspace_and_kill);
    RUN(test_escape_and_ctrl);
    RUN(test_overflow);
    RUN(test_tokenize);
    RUN(test_parse_u64);
    TEST_REPORT("test_shell_parse");
}
