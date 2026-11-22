/* unit.h - minimal test harness for the PC-side tests */
#ifndef UNIT_H
#define UNIT_H
#include <stdio.h>
#include <string.h>

extern int unit_checks, unit_fails;

#define CHECK(cond) do { unit_checks++; if (!(cond)) { unit_fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); unit_checks++; \
    if (_a != _b) { unit_fails++; printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); unit_checks++; \
    if (!_a || !_b || strcmp(_a, _b)) { unit_fails++; printf("FAIL %s:%d: %s == \"%s\" (got \"%s\")\n", \
    __FILE__, __LINE__, #a, _b ? _b : "(null)", _a ? _a : "(null)"); } } while (0)

void test_uls_proto(void);
void test_level(void);
void test_policy(void);
void test_json(void);
void test_config(void);
void test_abstore(void);
void test_runstate(void);
void test_ha(void);
void test_net(void);

#endif
