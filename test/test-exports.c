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

START_TEST(iter_list)
{
	toml2_t doc = check_init("x = [1, 2, 3]");
	toml2_iter_t iter;
	ck_assert_int_eq(0, toml2_iter_init(&iter, toml2_get(&doc, "x")));

	for (size_t i = 1; i <= 3; i += 1) {
		toml2_t *subdoc = toml2_iter_next(&iter);
		ck_assert_ptr_ne(subdoc, NULL);
		ck_assert_int_eq(TOML2_INT, toml2_type(subdoc));
		ck_assert_int_eq(i, toml2_int(subdoc));
	}

	toml2_iter_free(&iter);
	toml2_free(&doc);
}
END_TEST

START_TEST(iter_table)
{
	toml2_t doc = check_init("x = { a = 1, b = 2 }");
	toml2_iter_t iter;
	ck_assert_int_eq(0, toml2_iter_init(&iter, toml2_get(&doc, "x")));

	toml2_t *subdoc = NULL;
	size_t count = 0;

	while (NULL != (subdoc = toml2_iter_next(&iter))) {
		count += 1;
		const char *name = toml2_name(subdoc);
		ck_assert_ptr_ne(name, NULL);
		ck_assert_int_eq(1, strlen(name));
		ck_assert_int_eq(TOML2_INT, toml2_type(subdoc));

		if (name[0] == 'a') {
			ck_assert_int_eq(1, toml2_int(subdoc));
		}
		else if (name[0] == 'b') {
			ck_assert_int_eq(2, toml2_int(subdoc));
		}
		else {
			ck_assert(false);
		}
	}

	ck_assert_int_eq(2, count);
	toml2_iter_free(&iter);
	toml2_free(&doc);
}
END_TEST

START_TEST(iter_empty_list)
{
	toml2_t doc = check_init("x = []");
	toml2_iter_t iter;
	ck_assert_int_eq(0, toml2_iter_init(&iter, toml2_get(&doc, "x")));
	ck_assert_ptr_eq(NULL, toml2_iter_next(&iter));
	toml2_iter_free(&iter);
	toml2_free(&doc);
}
END_TEST

START_TEST(iter_empty_table)
{
	toml2_t doc = check_init("x = {}");
	toml2_iter_t iter;
	ck_assert_int_eq(0, toml2_iter_init(&iter, toml2_get(&doc, "x")));
	ck_assert_ptr_eq(NULL, toml2_iter_next(&iter));
	toml2_iter_free(&iter);
	toml2_free(&doc);
}
END_TEST

START_TEST(err_iter_int)
{
	toml2_t doc = check_init("x = 1");
	toml2_iter_t iter;
	ck_assert_int_eq(1, toml2_iter_init(&iter, toml2_get(&doc, "x")));
	toml2_free(&doc);
}
END_TEST

START_TEST(diorite)
{
	const char *toml = 
		"[material]\n"
		"name = \"diorite\"\n"
		"color = \"A9A9A9\"\n\n"
		"[render.\"1d\"]\n"
		"tile = \"16/materials/diorite.png\"";
	
	toml2_t doc = check_init(toml);
	ck_assert_int_eq(
		TOML2_TABLE,
		toml2_type(toml2_get_path(&doc, "material"))
	);
	ck_assert_int_eq(
		TOML2_TABLE,
		toml2_type(toml2_get_path(&doc, "render"))
	);
	ck_assert_int_eq(
		TOML2_TABLE,
		toml2_type(toml2_get_path(&doc, "render.1d"))
	);
	ck_assert_int_eq(
		TOML2_STRING,
		toml2_type(toml2_get_path(&doc, "render.1d.tile"))
	);
	ck_assert_str_eq(
		"16/materials/diorite.png",
		toml2_string(toml2_get_path(&doc, "render.1d.tile"))
	);
	toml2_free(&doc);
}
END_TEST

Suite*
suite_exports()
{
	tcase_t tests[] = {
		{ "iter_list",        &iter_list        },
		{ "iter_table",       &iter_table       },
		{ "iter_empty_list",  &iter_empty_list  },
		{ "iter_empty_table", &iter_empty_table },
		{ "err_iter_int",     &err_iter_int     },
		{ "diorite",          &diorite          },
	};

	return tcase_build_suite("exports", tests, sizeof(tests));
}
