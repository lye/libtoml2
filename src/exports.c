#include "toml2.h"
#include "toml2-grammar.h"
#include <stdlib.h>
#include <string.h>

const char*
toml2_type_name(toml2_type_t type)
{
	switch (type) {
		case TOML2_TABLE: return "table";
		case TOML2_LIST: return "list";
		case TOML2_INT: return "int";
		case TOML2_FLOAT: return "float";
		case TOML2_STRING: return "string";
		case TOML2_DATE: return "date";
		case TOML2_BOOL: return "bool";
		default: return "invalid";
	}
}

toml2_type_t
toml2_type(toml2_t *this)
{
	return this->type;
}

const char*
toml2_name(toml2_t *this)
{
	if (NULL == this) {
		return NULL;
	}
	return this->name;
}

toml2_t*
toml2_get(toml2_t *this, const char *name)
{
	if (NULL == this || TOML2_TABLE != this->type) {
		return NULL;
	}

	toml2_t proto = {
		.name = name,
	};

	return RB_FIND(toml2_tree_t, &this->tree, &proto);
}

toml2_t*
toml2_get_path(toml2_t *this, const char *name)
{
	char *dup = strdup(name);
	if (NULL == dup) {
		// XXX: It would be nice to propagate ENOMEM better.
		return NULL;
	}

	char *work, *tmp;

	for (
		work = strtok_r(dup, ".", &tmp);
		NULL != work && this != NULL;
		work = strtok_r(NULL, ".", &tmp)
	) {
		if (TOML2_TABLE == this->type) {
			this = toml2_get(this, work);
		}
		else if (TOML2_LIST == this->type) {
			char *end = NULL;
			size_t off = strtol(work, &end, 10);

			if (0 != *end) {
				this = NULL;
			}
			else {
				this = toml2_index(this, off);
			}
		}
		else {
			this = NULL;
		}
	}

	free(dup);
	return this;
}

double
toml2_float(toml2_t *this)
{
	if (NULL == this) {
		return 0.;
	}
	if (TOML2_INT == this->type) {
		return (double) this->ival;
	}
	if (TOML2_FLOAT == this->type) {
		return this->fval;
	}
	return 0.;
}

bool
toml2_bool(toml2_t *this)
{
	if (NULL != this && TOML2_BOOL == this->type) {
		return this->bval;
	}
	return false;
}

int64_t
toml2_int(toml2_t *this)
{
	if (NULL == this) {
		return 0;
	}
	if (TOML2_INT == this->type) {
		return this->ival;
	}
	if (TOML2_FLOAT == this->type) {
		return (int64_t) this->fval;
	}
	return 0;
}

const char*
toml2_string(toml2_t *this)
{
	if (NULL != this && TOML2_STRING == this->type) {
		return this->sval;
	}
	return NULL;
}

struct tm
toml2_date(toml2_t *this)
{
	if (NULL != this && TOML2_DATE == this->type) {
		return this->tval;
	}

	struct tm ret = {0};
	return ret;
}

size_t
toml2_len(toml2_t *this)
{
	if (NULL == this) {
		return 0;
	}
	if (TOML2_TABLE == this->type) {
		return this->tree_len;
	}
	if (TOML2_LIST == this->type) {
		return this->ary_len;
	}
	return 0;
}

toml2_t*
toml2_index(toml2_t *this, size_t idx)
{
	if (NULL == this) {
		return NULL;
	}
	if (TOML2_LIST == this->type && idx < this->ary_len) {
		return &this->ary[idx];
	}
	if (TOML2_TABLE == this->type && idx < this->tree_len) {
		toml2_t *tmp = RB_MIN(toml2_tree_t, &this->tree);

		for (size_t i = 0; i < idx; i += 1) {
			tmp = RB_NEXT(toml2_tree_t, &this->tree, tmp);
		}

		return tmp;
	}
	return NULL;
}

int
toml2_iter_init(toml2_iter_t *iter, toml2_t *doc)
{
	if (TOML2_TABLE == doc->type) {
		iter->parent = doc;
		iter->next = RB_MIN(toml2_tree_t, &doc->tree);
	}
	else if (TOML2_LIST == doc->type) {
		iter->parent = doc;
		iter->index = 0;
	}
	else {
		return 1;
	}

	return 0;
}

toml2_t*
toml2_iter_next(toml2_iter_t *iter)
{
	if (TOML2_TABLE == iter->parent->type) {
		toml2_t *next = iter->next;
		if (NULL != next) {
			iter->next = RB_NEXT(toml2_tree_t, &iter->parent->tree, next);
		}
		return next;
	}
	else if (TOML2_LIST == iter->parent->type) {
		if (iter->index >= iter->parent->ary_len) {
			return NULL;
		}

		toml2_t *next = &iter->parent->ary[iter->index];
		iter->index += 1;
		return next;
	}
	else {
		return NULL;
	}
}

void
toml2_iter_free(toml2_iter_t *iter)
{
	bzero(iter, sizeof(*iter));
}
