#include "util.h"

Suite*
tcase_build_suite_with_fixtures(const char *name, void(*setup)(), void(*teardown)(), tcase_t *tests, size_t blen)
{
	Suite *suite = suite_create(name);
	size_t ntests = blen / sizeof(tcase_t);

	for (size_t i = 0; i < ntests; i += 1) {
		tcase_t *test = &tests[i];

		TCase *tc = tcase_create(test->name);
		tcase_add_checked_fixture(tc, setup, teardown);
		tcase_add_test(tc, test->func);
		suite_add_tcase(suite, tc);
	}

	return suite;
}

Suite*
tcase_build_suite(const char *name, tcase_t *tests, size_t blen)
{
	return tcase_build_suite_with_fixtures(name, NULL, NULL, tests, blen);
}

