#pragma once
#include <sys/types.h>
#include <sys/tree.h>

int toml2_cmp(const void*, const void*);

RB_PROTOTYPE(toml2_tree_t, toml2_t, link, toml2_cmp);
