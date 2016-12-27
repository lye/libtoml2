#include "util.h"

START_TEST(noop)
{
}
END_TEST

Suite*
suite_lexer()
{
	tcase_t tests[] = {
		{ "noop", &noop },
	};

	return tcase_build_suite("lexer", tests, sizeof(tests));
}
