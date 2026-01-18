#include "TestUtil.h"
#include "utils/translit_icao.h"

#include <cstring>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_translit_privet(void)
{
    char out[64];
    translit_icao_ru_to_ascii("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Privet", out);
}

void test_translit_yo_zh(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xD0\x81\xD0\xB6", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Ezh", out);
}

void test_translit_shchuka(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xD0\xA9\xD1\x83\xD0\xBA\xD0\xB0", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Shchuka", out);
}

void test_translit_hardsign_omit(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xD0\xBF\xD0\xBE\xD0\xB4\xD1\x8A\xD0\xB5\xD0\xB7\xD0\xB4", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("podezd", out);
}

void test_translit_mixed_ascii_cyrillic(void)
{
    char out[64];
    translit_icao_ru_to_ascii("WiFi: \xD0\xA4\xD1\x8B\xD0\xBD\xD0\xB4\xD1\x8B\xD0\xBA", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("WiFi: Fyndyk", out);
}

void test_translit_cyrillic_ghe_upturn(void)
{
    char out[64];
    translit_icao_ru_to_ascii("\xD2\x90\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xBA", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Ganok", out);
}

void test_translit_ukrainian_yi(void)
{
    char out[64];
    translit_icao_ru_to_ascii("\xD0\x87\xD0\xB6\xD0\xB0\xD0\xBA", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Yizhak", out);
}

void test_translit_belarusian_short_u(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xD0\x8E\xD0\xBB\xD0\xB0\xD1\x81", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Ulas", out);
}

void test_translit_serbian_lj(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xD0\x89\xD1\x83\xD0\xB1\xD0\xB0\xD0\xB2", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Ljubav", out);
}

void test_translit_greek_athina(void)
{
    char out[32];
    translit_icao_ru_to_ascii("\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Athina", out);
}

void test_translit_malformed_utf8(void)
{
    char out[16];
    translit_icao_ru_to_ascii("\xD0\x9F\xD1\x80\xD0", out, sizeof(out));
    TEST_ASSERT_NOT_NULL(std::strchr(out, '?'));
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_translit_privet);
    RUN_TEST(test_translit_yo_zh);
    RUN_TEST(test_translit_shchuka);
    RUN_TEST(test_translit_hardsign_omit);
    RUN_TEST(test_translit_mixed_ascii_cyrillic);
    RUN_TEST(test_translit_cyrillic_ghe_upturn);
    RUN_TEST(test_translit_ukrainian_yi);
    RUN_TEST(test_translit_belarusian_short_u);
    RUN_TEST(test_translit_serbian_lj);
    RUN_TEST(test_translit_greek_athina);
    RUN_TEST(test_translit_malformed_utf8);
    exit(UNITY_END());
}

void loop() {}
