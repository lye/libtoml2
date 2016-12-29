#include "util.h"
#include "toml2.h"

static toml2_t
check_init(const char *str)
{
	toml2_t doc;
	toml2_init(&doc);
	ck_assert_int_eq(0, toml2_parse(&doc, str, strlen(str)));
	return doc;
}

START_TEST(init_free)
{
	toml2_t doc;
	toml2_init(&doc);
	toml2_free(&doc);
}
END_TEST

START_TEST(basic_table)
{
	toml2_t doc = check_init("[foo]\nbar = 42");
	toml2_free(&doc);
}
END_TEST

Suite*
suite_grammar()
{
	tcase_t tests[] = {
		{ "init_free",   &init_free   },
		{ "basic_table", &basic_table },
	};

	return tcase_build_suite("grammar", tests, sizeof(tests));
}
