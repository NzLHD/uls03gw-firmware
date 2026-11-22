#include "unit.h"
#include "json_mini.h"

typedef struct { int n; char keys[8][32]; json_value_t v[8]; char strs[8][64]; } collect_t;

static int collect(void *ctx, const char *key, const json_value_t *v)
{
    collect_t *c = ctx;
    if (c->n >= 8) return 99;
    snprintf(c->keys[c->n], 32, "%s", key);
    c->v[c->n] = *v;
    if (v->type == JV_STRING) snprintf(c->strs[c->n], 64, "%s", v->str);
    c->n++;
    return 0;
}

static int parse(const char *s, collect_t *c)
{
    char scratch[128];
    memset(c, 0, sizeof(*c));
    return json_parse_flat(s, strlen(s), scratch, sizeof(scratch), collect, c);
}

void test_json(void)
{
    collect_t c;

    CHECK_EQ(parse(" { } ", &c), 0);
    CHECK_EQ(c.n, 0);

    CHECK_EQ(parse("{\"a\":1,\"b\":-20,\"c\":true,\"d\":false,\"e\":null,\"f\":\"x\"}", &c), 0);
    CHECK_EQ(c.n, 6);
    CHECK_STR(c.keys[0], "a");
    CHECK(c.v[0].type == JV_INT && c.v[0].i == 1);
    CHECK(c.v[1].type == JV_INT && c.v[1].i == -20);
    CHECK(c.v[2].type == JV_BOOL && c.v[2].b);
    CHECK(c.v[3].type == JV_BOOL && !c.v[3].b);
    CHECK(c.v[4].type == JV_NULL);
    CHECK_STR(c.strs[5], "x");

    /* escapes and unicode */
    CHECK_EQ(parse("{\"s\":\"a\\\"b\\\\c\\/d\\n\\u00e9\\u20ac\\ud83d\\ude00\"}", &c), 0);
    CHECK_STR(c.strs[0], "a\"b\\c/d\n\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    CHECK_EQ(parse("{\"s\":\"\\u0000\"}", &c), JSON_ERR_SYNTAX);    /* no NUL */
    CHECK_EQ(parse("{\"s\":\"\\udc00\"}", &c), JSON_ERR_SYNTAX);    /* lone low surrogate */
    CHECK_EQ(parse("{\"s\":\"tab\there\"}", &c), JSON_ERR_SYNTAX);  /* raw control char */

    /* numbers: integers only, x.0 tolerated */
    CHECK_EQ(parse("{\"n\":180.0}", &c), 0);
    CHECK(c.v[0].i == 180);
    CHECK_EQ(parse("{\"n\":1.5}", &c), JSON_ERR_NUMBER);
    CHECK_EQ(parse("{\"n\":1e3}", &c), JSON_ERR_NUMBER);
    CHECK_EQ(parse("{\"n\":012}", &c), JSON_ERR_NUMBER);
    CHECK_EQ(parse("{\"n\":1234567890123456}", &c), JSON_ERR_NUMBER);
    CHECK_EQ(parse("{\"n\":-}", &c), JSON_ERR_SYNTAX);

    /* structure */
    CHECK_EQ(parse("{\"o\":{}}", &c), JSON_ERR_NESTED);
    CHECK_EQ(parse("{\"o\":[1]}", &c), JSON_ERR_NESTED);
    CHECK_EQ(parse("{\"a\":1,}", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("{\"a\":1} x", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("{\"a\" 1}", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("{\"a\":tru}", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("[1]", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("{\"a\":\"unterminated}", &c), JSON_ERR_SYNTAX);
    CHECK_EQ(parse("", &c), JSON_ERR_SYNTAX);

    /* scratch too small */
    {
        char tiny[4];
        CHECK_EQ(json_parse_flat("{\"abcdef\":1}", 12, tiny, sizeof(tiny), collect, &c), JSON_ERR_SCRATCH);
    }

    /* writer */
    char buf[128];
    json_writer_t w;
    jw_init(&w, buf, sizeof(buf));
    jw_begin(&w);
    jw_int(&w, "a", -5);
    jw_str(&w, "s", "q\"\\\n\x01");
    jw_null(&w, "n");
    jw_bool(&w, "b", true);
    jw_int_or_null(&w, "x", 7, true);
    jw_key(&w, "o"); jw_begin(&w); jw_int(&w, "k", 1); jw_end(&w);
    jw_str(&w, "z", NULL);
    jw_end(&w);
    CHECK(jw_finish(&w) > 0);
    CHECK_STR(buf, "{\"a\":-5,\"s\":\"q\\\"\\\\\\n\\u0001\",\"n\":null,\"b\":true,\"x\":null,\"o\":{\"k\":1},\"z\":null}");

    jw_init(&w, buf, 8);
    jw_begin(&w); jw_str(&w, "long", "value"); jw_end(&w);
    CHECK_EQ(jw_finish(&w), -1);
    CHECK(strlen(buf) < 8);

    /* round trip through the parser */
    jw_init(&w, buf, sizeof(buf));
    jw_begin(&w); jw_str(&w, "ssid", "caf\xc3\xa9 \"x\""); jw_int(&w, "n", -2147483647 - 1); jw_end(&w);
    CHECK_EQ(parse(buf, &c), 0);
    CHECK_STR(c.strs[0], "caf\xc3\xa9 \"x\"");
    CHECK_EQ(c.v[1].i, -2147483647LL - 1);
}
