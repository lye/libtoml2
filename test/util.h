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

