/* Tiny test registry — no framework, prints one line per suite.         */
#include <stdio.h>
#include <stdlib.h>

typedef void (*test_fn)(void);
static struct { const char *name; test_fn fn; int failed; } tests[64];
static int n_tests = 0;
int test_failures = 0;

void ppu16_test_register(const char *name, test_fn fn)
{ tests[n_tests].name = name; tests[n_tests].fn = fn; n_tests++; }

void ppu16_test_check(int cond, const char *file, int line, const char *what)
{
    if (!cond) {
        fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, what);
        test_failures++;
    }
}

#define CHECK(cond) ppu16_test_check((cond) != 0, __FILE__, __LINE__, #cond)

/* Registered by each suite. */
void register_tests_color(void);
void register_tests_bg(void);
void register_tests_sprite(void);
void register_tests_affine(void);
void register_tests_render(void);
void register_tests_link(void);
void register_tests_mvid(void);

int main(void)
{
    register_tests_color();
    register_tests_bg();
    register_tests_sprite();
    register_tests_affine();
    register_tests_render();
    register_tests_link();
    register_tests_mvid();

    for (int i = 0; i < n_tests; i++) {
        int before = test_failures;
        tests[i].fn();
        tests[i].failed = test_failures - before;
        printf("%-42s %s\n", tests[i].name, tests[i].failed ? "FAILED" : "ok");
    }
    printf("\n%d test(s), %d failure(s)\n", n_tests, test_failures);
    return test_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
