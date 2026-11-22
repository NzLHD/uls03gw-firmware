#include "unit.h"

int unit_checks, unit_fails;

int main(void)
{
    struct { const char *name; void (*fn)(void); } suites[] = {
        { "uls_proto", test_uls_proto },
        { "level",     test_level },
        { "policy",    test_policy },
        { "json",      test_json },
        { "config",    test_config },
        { "abstore",   test_abstore },
        { "runstate",  test_runstate },
        { "ha",        test_ha },
        { "net",       test_net },
    };
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        int before = unit_fails;
        suites[i].fn();
        printf("%-10s %s\n", suites[i].name, unit_fails == before ? "ok" : "FAILED");
    }
    printf("\n%d checks, %d failure(s)\n", unit_checks, unit_fails);
    return unit_fails ? 1 : 0;
}
