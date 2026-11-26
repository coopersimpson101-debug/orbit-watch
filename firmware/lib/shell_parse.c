/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/shell/shell_parse.c.
 * Changes for orbit-watch: none (verbatim) */
#include "shell_parse.h"

static void echo(struct line_editor *le, const char *s, size_t n)
{
    if (le->echo)
        le->echo(le->echo_ctx, s, n);
}

void le_init(struct line_editor *le, line_echo_fn fn, void *ctx)
{
    le->len = 0;
    le->buf[0] = 0;
    le->last_cr = 0;
    le->esc_state = 0;
    le->echo = fn;
    le->echo_ctx = ctx;
}

enum line_result le_feed(struct line_editor *le, char c)
{
    unsigned char uc = (unsigned char)c;

    /* Ignore ANSI escape sequences (arrow keys etc.): ESC [ ... final */
    if (le->esc_state == 1) {
        le->esc_state = (c == '[') ? 2 : 0;
        return LINE_PENDING;
    }
    if (le->esc_state == 2) {
        if (uc >= 0x40 && uc <= 0x7e)
            le->esc_state = 0;
        return LINE_PENDING;
    }
    if (uc == 0x1b) {
        le->esc_state = 1;
        return LINE_PENDING;
    }

    if (c == '\n' && le->last_cr) {
        le->last_cr = 0;
        return LINE_PENDING;
    }
    le->last_cr = (c == '\r');

    if (c == '\r' || c == '\n') {
        le->buf[le->len] = 0;
        le->len = 0;
        echo(le, "\r\n", 2);
        return LINE_READY;
    }
    if (uc == 0x7f || uc == 0x08) { /* DEL / backspace */
        if (le->len) {
            le->len--;
            echo(le, "\b \b", 3);
        }
        return LINE_PENDING;
    }
    if (uc == 0x15) { /* Ctrl-U: kill line */
        while (le->len) {
            le->len--;
            echo(le, "\b \b", 3);
        }
        return LINE_PENDING;
    }
    if (uc == 0x03) { /* Ctrl-C */
        le->len = 0;
        le->buf[0] = 0;
        echo(le, "^C\r\n", 4);
        return LINE_CANCELLED;
    }
    if (uc < 0x20 || uc > 0x7e)
        return LINE_PENDING; /* drop other control / non-ASCII bytes */
    if (le->len + 1 >= LINE_MAX_LEN) {
        echo(le, "\a", 1);
        return LINE_PENDING;
    }
    le->buf[le->len++] = c;
    echo(le, &c, 1);
    return LINE_PENDING;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

int shell_tokenize(char *line, char **argv, int max)
{
    int argc = 0;
    char *r = line, *w = line;
    while (*r) {
        while (is_space(*r))
            r++;
        if (!*r)
            break;
        if (argc >= max)
            return -1;
        argv[argc++] = w;
        while (*r && !is_space(*r)) {
            if (*r == '"') {
                r++;
                while (*r && *r != '"')
                    *w++ = *r++;
                if (*r != '"')
                    return -1;
                r++;
            } else {
                *w++ = *r++;
            }
        }
        if (*r)
            r++;
        *w++ = 0;
    }
    return argc;
}

int parse_u64(const char *s, uint64_t *out)
{
    uint64_t v = 0;
    unsigned base = 10;
    int digits = 0;
    if (!s || !*s)
        return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (; *s; s++) {
        unsigned d;
        if (*s == '_')
            continue;
        if (*s >= '0' && *s <= '9')
            d = (unsigned)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = (unsigned)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = (unsigned)(*s - 'A' + 10);
        else
            return -1;
        if (v > (UINT64_MAX - d) / base)
            return -1;
        v = v * base + d;
        digits++;
    }
    if (!digits)
        return -1;
    *out = v;
    return 0;
}
