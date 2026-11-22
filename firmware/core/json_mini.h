/*
 * json_mini.h - tiny JSON helpers for the config API and MQTT payloads. Pure C99, no malloc.
 *
 * Parser: one flat object {"key": value, ...}. Values: string, integer, true/false, null.
 * Nested objects/arrays and fractional numbers are rejected (no setting needs them).
 * Writer: appends to a fixed buffer and remembers overflow.
 */
#ifndef ULS_JSON_MINI_H
#define ULS_JSON_MINI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { JV_STRING, JV_INT, JV_BOOL, JV_NULL } json_vtype_t;

typedef struct {
    json_vtype_t type;
    const char  *str;       /* JV_STRING: unescaped, NUL-terminated (in the scratch buffer) */
    size_t       str_len;
    int64_t      i;         /* JV_INT */
    bool         b;         /* JV_BOOL */
} json_value_t;

/* Called once per member. Return 0 to continue, non-zero to stop (that value is returned). */
typedef int (*json_member_cb)(void *ctx, const char *key, const json_value_t *v);

/* Parse a flat object. scratch holds unescaped key/string text for the duration of
 * one callback (needs roughly the length of the longest string + key + 2).
 * Returns 0 on success, the callback's non-zero value, or JSON_ERR_*. */
#define JSON_ERR_SYNTAX   (-1)
#define JSON_ERR_NESTED   (-2)
#define JSON_ERR_NUMBER   (-3)
#define JSON_ERR_SCRATCH  (-4)
int json_parse_flat(const char *s, size_t len, char *scratch, size_t scratch_len,
                    json_member_cb cb, void *ctx);

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   overflow;
    bool   need_comma;
} json_writer_t;

void jw_init(json_writer_t *w, char *buf, size_t cap);
void jw_begin(json_writer_t *w);                               /* {  */
void jw_end(json_writer_t *w);                                 /* }  */
void jw_key(json_writer_t *w, const char *key);
void jw_str(json_writer_t *w, const char *key, const char *val); /* val NULL -> null */
void jw_int(json_writer_t *w, const char *key, int64_t val);
void jw_int_or_null(json_writer_t *w, const char *key, int64_t val, bool is_null);
void jw_bool(json_writer_t *w, const char *key, bool val);
void jw_null(json_writer_t *w, const char *key);
void jw_raw(json_writer_t *w, const char *key, const char *raw); /* pre-encoded JSON value */
/* Returns the string length, or -1 if the buffer overflowed. Always NUL-terminated. */
int  jw_finish(json_writer_t *w);

#endif
