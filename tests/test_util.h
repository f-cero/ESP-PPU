#ifndef PPU16_TEST_UTIL_H
#define PPU16_TEST_UTIL_H
extern int test_failures;
void ppu16_test_register(const char *name, void (*fn)(void));
void ppu16_test_check(int cond, const char *file, int line, const char *what);
#define CHECK(cond) ppu16_test_check((cond) != 0, __FILE__, __LINE__, #cond)
#endif
