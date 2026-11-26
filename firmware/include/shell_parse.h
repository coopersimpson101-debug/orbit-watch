/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/shell_parse.h.
 * Changes for orbit-watch: none (verbatim) */
/* Shell front-end pieces that do not touch hardware: an interactive line
 * editor fed one byte at a time and an argv tokenizer. */
#ifndef ORBIT_SHELL_PARSE_H
#define ORBIT_SHELL_PARSE_H

#include <stddef.h>
#include <stdint.h>

#define LINE_MAX_LEN 128
#define SHELL_MAX_ARGS 8

typedef void (*line_echo_fn)(void *ctx, const char *s, size_t n);

struct line_editor {
    char buf[LINE_MAX_LEN];
    size_t len;
    int last_cr;     /* swallow the LF of a CRLF pair */
    int esc_state;   /* 0 none, 1 got ESC, 2 inside CSI */
    line_echo_fn echo;
    void *echo_ctx;
};

enum line_result { LINE_PENDING = 0, LINE_READY = 1, LINE_CANCELLED = 2 };

void le_init(struct line_editor *le, line_echo_fn echo, void *ctx);
/* Feed one input byte. On LINE_READY the NUL-terminated line is in
 * le->buf and stays valid until the next le_feed(). */
enum line_result le_feed(struct line_editor *le, char c);

/* Split `line` in place into at most `max` arguments. Whitespace separates
 * arguments; double quotes group (no escapes inside). Returns argc, or -1
 * on an unterminated quote or too many arguments. */
int shell_tokenize(char *line, char **argv, int max);

/* Parse decimal or 0x-prefixed hex (underscores allowed as separators).
 * Returns 0 on success, -1 on bad syntax or overflow. */
int parse_u64(const char *s, uint64_t *out);

#endif
