/* json_mini.c - see json_mini.h */
#include "json_mini.h"
#include <string.h>

/* ---------------------------------------------------------------- parser */

typedef struct {
    const char *p, *end;
} cur_t;

static void skip_ws(cur_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
        c->p++;
}

static int hex4(const char *p, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char ch = p[i];
        v <<= 4;
        if (ch >= '0' && ch <= '9')      v |= (uint32_t)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v |= (uint32_t)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v |= (uint32_t)(ch - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static int put_utf8(char **o, char *oend, uint32_t cp)
{
    int n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (oend - *o < n)
        return -1;
    char *d = *o;
    switch (n) {
    case 1: d[0] = (char)cp; break;
    case 2: d[0] = (char)(0xC0 | (cp >> 6));  d[1] = (char)(0x80 | (cp & 0x3F)); break;
    case 3: d[0] = (char)(0xE0 | (cp >> 12)); d[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            d[2] = (char)(0x80 | (cp & 0x3F)); break;
    default: d[0] = (char)(0xF0 | (cp >> 18)); d[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
             d[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[3] = (char)(0x80 | (cp & 0x3F)); break;
    }
    *o += n;
    return 0;
}

/* Parse a string starting at '"' into [*o, oend), NUL-terminated. */
static int parse_string(cur_t *c, char **o, char *oend, size_t *out_len)
{
    char *start = *o;
    if (c->p >= c->end || *c->p != '"')
        return JSON_ERR_SYNTAX;
    c->p++;
    while (c->p < c->end && *c->p != '"') {
        unsigned char ch = (unsigned char)*c->p++;
        if (ch < 0x20)
            return JSON_ERR_SYNTAX;             /* raw control chars are not allowed */
        if (ch != '\\') {
            if (*o >= oend) return JSON_ERR_SCRATCH;
            *(*o)++ = (char)ch;
            continue;
        }
        if (c->p >= c->end)
            return JSON_ERR_SYNTAX;
        char e = *c->p++;
        char lit = 0;
        switch (e) {
        case '"': lit = '"'; break;   case '\\': lit = '\\'; break;
        case '/': lit = '/'; break;   case 'b':  lit = '\b'; break;
        case 'f': lit = '\f'; break;  case 'n':  lit = '\n'; break;
        case 'r': lit = '\r'; break;  case 't':  lit = '\t'; break;
        case 'u': {
            uint32_t cp, lo;
            if (c->end - c->p < 4 || hex4(c->p, &cp)) return JSON_ERR_SYNTAX;
            c->p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {             /* surrogate pair */
                if (c->end - c->p < 6 || c->p[0] != '\\' || c->p[1] != 'u' ||
                    hex4(c->p + 2, &lo) || lo < 0xDC00 || lo > 0xDFFF)
                    return JSON_ERR_SYNTAX;
                c->p += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return JSON_ERR_SYNTAX;
            }
            if (cp == 0) return JSON_ERR_SYNTAX;            /* no embedded NULs */
            if (put_utf8(o, oend, cp)) return JSON_ERR_SCRATCH;
            continue;
        }
        default: return JSON_ERR_SYNTAX;
        }
        if (*o >= oend) return JSON_ERR_SCRATCH;
        *(*o)++ = lit;
    }
    if (c->p >= c->end)
        return JSON_ERR_SYNTAX;
    c->p++;                                     /* closing quote */
    if (*o >= oend) return JSON_ERR_SCRATCH;
    *(*o)++ = 0;
    *out_len = (size_t)(*o - start - 1);
    return 0;
}

static int lit_match(cur_t *c, const char *lit)
{
    size_t n = strlen(lit);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, lit, n) != 0)
        return 0;
    c->p += n;
    return 1;
}

static int parse_int(cur_t *c, int64_t *out)
{
    bool neg = false;
    uint64_t v = 0;
    int digits = 0;
    if (c->p < c->end && *c->p == '-') { neg = true; c->p++; }
    if (c->p < c->end && *c->p == '0' && c->p + 1 < c->end && c->p[1] >= '0' && c->p[1] <= '9')
        return JSON_ERR_NUMBER;                 /* no leading zeros */
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        v = v * 10 + (uint64_t)(*c->p++ - '0');
        if (++digits > 15)
            return JSON_ERR_NUMBER;
    }
    if (!digits)
        return JSON_ERR_SYNTAX;
    if (c->p < c->end && *c->p == '.') {        /* allow "180.0" (some senders format floats) */
        int frac = 0;
        c->p++;
        while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
            if (*c->p++ != '0')
                return JSON_ERR_NUMBER;         /* integers only */
            frac++;
        }
        if (!frac)
            return JSON_ERR_SYNTAX;
    }
    if (c->p < c->end && (*c->p == 'e' || *c->p == 'E'))
        return JSON_ERR_NUMBER;
    *out = neg ? -(int64_t)v : (int64_t)v;
    return 0;
}

int json_parse_flat(const char *s, size_t len, char *scratch, size_t scratch_len,
                    json_member_cb cb, void *ctx)
{
    cur_t c = { s, s + len };
    int rc;

    skip_ws(&c);
    if (c.p >= c.end || *c.p++ != '{')
        return JSON_ERR_SYNTAX;
    skip_ws(&c);
    if (c.p < c.end && *c.p == '}') {
        c.p++;
        goto done;
    }
    for (;;) {
        char *o = scratch, *oend = scratch + scratch_len;
        size_t klen;
        json_value_t v;
        const char *key;

        skip_ws(&c);
        key = o;
        if ((rc = parse_string(&c, &o, oend, &klen)) != 0)
            return rc;
        skip_ws(&c);
        if (c.p >= c.end || *c.p++ != ':')
            return JSON_ERR_SYNTAX;
        skip_ws(&c);
        if (c.p >= c.end)
            return JSON_ERR_SYNTAX;

        memset(&v, 0, sizeof(v));
        char ch = *c.p;
        if (ch == '"') {
            v.type = JV_STRING;
            v.str = o;
            if ((rc = parse_string(&c, &o, oend, &v.str_len)) != 0)
                return rc;
        } else if (ch == '{' || ch == '[') {
            return JSON_ERR_NESTED;
        } else if (lit_match(&c, "true")) {
            v.type = JV_BOOL; v.b = true;
        } else if (lit_match(&c, "false")) {
            v.type = JV_BOOL; v.b = false;
        } else if (lit_match(&c, "null")) {
            v.type = JV_NULL;
        } else {
            v.type = JV_INT;
            if ((rc = parse_int(&c, &v.i)) != 0)
                return rc;
        }
        if ((rc = cb(ctx, key, &v)) != 0)
            return rc;

        skip_ws(&c);
        if (c.p >= c.end)
            return JSON_ERR_SYNTAX;
        if (*c.p == ',') { c.p++; continue; }
        if (*c.p == '}') { c.p++; break; }
        return JSON_ERR_SYNTAX;
    }
done:
    skip_ws(&c);
    return c.p == c.end ? 0 : JSON_ERR_SYNTAX;
}

/* ---------------------------------------------------------------- writer */

static void put(json_writer_t *w, const char *s, size_t n)
{
    if (w->overflow)
        return;
    if (w->len + n + 1 > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = 0;
}

static void putc_(json_writer_t *w, char ch) { put(w, &ch, 1); }

static void put_escaped(json_writer_t *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    putc_(w, '"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        switch (ch) {
        case '"':  put(w, "\\\"", 2); break;
        case '\\': put(w, "\\\\", 2); break;
        case '\n': put(w, "\\n", 2);  break;
        case '\r': put(w, "\\r", 2);  break;
        case '\t': put(w, "\\t", 2);  break;
        default:
            if (ch < 0x20) {
                char u[6] = { '\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 15] };
                put(w, u, 6);
            } else {
                putc_(w, (char)ch);
            }
        }
    }
    putc_(w, '"');
}

static void put_i64(json_writer_t *w, int64_t v)
{
    char tmp[24];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) tmp[n++] = '-';
    while (n) putc_(w, tmp[--n]);
}

void jw_init(json_writer_t *w, char *buf, size_t cap)
{
    w->buf = buf; w->cap = cap; w->len = 0; w->overflow = cap == 0; w->need_comma = false;
    if (cap) buf[0] = 0;
}

void jw_begin(json_writer_t *w) { putc_(w, '{'); w->need_comma = false; }
void jw_end(json_writer_t *w)   { putc_(w, '}'); w->need_comma = true; }

void jw_key(json_writer_t *w, const char *key)
{
    if (w->need_comma) putc_(w, ',');
    put_escaped(w, key);
    putc_(w, ':');
    w->need_comma = true;
}

void jw_str(json_writer_t *w, const char *key, const char *val)
{
    jw_key(w, key);
    if (val) put_escaped(w, val); else put(w, "null", 4);
}

void jw_int(json_writer_t *w, const char *key, int64_t val) { jw_key(w, key); put_i64(w, val); }

void jw_int_or_null(json_writer_t *w, const char *key, int64_t val, bool is_null)
{
    jw_key(w, key);
    if (is_null) put(w, "null", 4); else put_i64(w, val);
}

void jw_bool(json_writer_t *w, const char *key, bool val)
{
    jw_key(w, key);
    if (val) put(w, "true", 4); else put(w, "false", 5);
}

void jw_null(json_writer_t *w, const char *key) { jw_key(w, key); put(w, "null", 4); }

void jw_raw(json_writer_t *w, const char *key, const char *raw)
{
    jw_key(w, key);
    put(w, raw, strlen(raw));
}

int jw_finish(json_writer_t *w)
{
    return w->overflow ? -1 : (int)w->len;
}
