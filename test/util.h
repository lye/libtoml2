#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <check.h>

typedef struct {
	const char *name;
	void (*func)();
}
tcase_t;

Suite* tcase_build_suite(const char *name, tcase_t *tests, size_t blen);

#ifndef ck_assert_double_eq
#define _ck_assert_double(X, OP, Y) do { \
	double _ck_x = (X); \
	double _ck_y = (Y); \
	ck_assert_msg(_ck_x OP _ck_y, "Assertion '%s' failed: %s == %lf, %s == %lf", #X" "#OP" "#Y, #X, _ck_x, #Y, _ck_y); \
} while (0)

#define ck_assert_double_eq(X, Y) _ck_assert_double(X, ==, Y)
#endif
