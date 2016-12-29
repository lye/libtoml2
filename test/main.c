#include "util.h"

typedef Suite*(*suite_def)();

extern Suite 
	*suite_lexer(),
	*suite_grammar();

static suite_def suites[] = {
	&suite_lexer,
	&suite_grammar,
};

int
main()
{
	int number_failed = 0;
	int i;

	Suite *suite = NULL;
	SRunner *runner = NULL;

	runner = srunner_create(suite);

	for (i = 0; i < sizeof(suites) / sizeof(suite_def); i += 1) {
		srunner_add_suite(runner, suites[i]());
	}

	srunner_run_all(runner, CK_NORMAL);
	number_failed = srunner_ntests_failed(runner);
	srunner_free(runner);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

