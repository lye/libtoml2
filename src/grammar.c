#include "toml2.h"
#include "toml2-lexer.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static int toml2_cmp(const void*, const void*);

RB_PROTOTYPE_STATIC(toml2_tree_t, toml2_t, link, toml2_cmp);
RB_GENERATE_STATIC(toml2_tree_t, toml2_t, link, toml2_cmp);

static int
toml2_cmp(const void *lhs, const void *rhs)
{
	const toml2_t *l = lhs;
	const toml2_t *r = rhs;

	if (NULL == l || NULL == r) {
		// uhh.
		kill(getpid(), SIGTRAP);
		return l == r ? 0 : (l == NULL ? 1 : -1);
	}

	return strcmp(l->name, r->name);
}

void
toml2_init(toml2_t *doc)
{
	bzero(doc, sizeof(toml2_t));
	RB_INIT(&doc->tree);
}

void
toml2_free(toml2_t *doc)
{
	free((char*) doc->name);

	if (TOML2_TABLE == doc->type) {
		while (!RB_EMPTY(&doc->tree)) {
			toml2_t *child = RB_MIN(toml2_tree_t, &doc->tree);
			RB_REMOVE(toml2_tree_t, &doc->tree, child);
			toml2_free(child);
			free(child);
		}
	}
	else if (TOML2_LIST == doc->type) {
		for (size_t i = 0; i < doc->ary_len; i += 1) {
			toml2_t *child = &doc->ary[i];
			toml2_free(child);
		}
		free(doc->ary);
	}
	else if (TOML2_STRING == doc->type) {
		free((char*) doc->sval);
	}
}

static toml2_t*
toml2_find_subdoc(toml2_t *this, const char *name)
{
	toml2_t proto = {
		.name = name,
	};

	return RB_FIND(toml2_tree_t, &this->tree, &proto);
}

// This is where a smart person would pull in a parser generator or something.
// Alas I am not a smart person.

typedef struct {
	toml2_lex_t *lex;
	toml2_t *root;
	char *saved_name;
	size_t stack_len;
	size_t stack_cap;
	toml2_t **stack;
}
toml2_parse_t;

static void
toml2_parse_init(toml2_parse_t *p, toml2_lex_t *lex, toml2_t *root)
{
	bzero(p, sizeof(toml2_parse_t));
	p->lex = lex;
	p->root = root;
}

static void
toml2_parse_free(toml2_parse_t *p)
{
	free(p->stack);
}

static int
toml2_parse_push(toml2_parse_t *p, toml2_t *ctx)
{
	if (p->stack_len == p->stack_cap) {
		size_t new_cap = p->stack_cap == 0 ? 3 : p->stack_cap + 3;
		void *new = realloc(p->stack, new_cap * sizeof(toml2_t*));
		if (NULL == new) {
			return TOML2_NO_MEMORY;
		}

		p->stack = new;
		p->stack_cap = new_cap;
	}

	p->stack[p->stack_len] = ctx;
	p->stack_len += 1;
	return 0;
}

static int
toml2_parse_pop(toml2_parse_t *p)
{
	if (p->stack_len == 0) {
		return TOML2_INTERNAL_ERROR;
	}

	p->stack_len -= 1;
	return 0;
}

static int
toml2_parse_reset(toml2_parse_t *p, toml2_t *ctx)
{
	p->stack_len = 0;
	return toml2_parse_push(p, ctx);
}

static toml2_t*
toml2_parse_top(toml2_parse_t *p)
{
	if (p->stack_len == 0) {
		return NULL;
	}

	return p->stack[p->stack_len - 1];
}

static int
toml2_parse_set_top(toml2_parse_t *p, toml2_t *ctx)
{
	if (p->stack_len == 0) {
		return TOML2_INTERNAL_ERROR;
	}

	p->stack[p->stack_len - 1] = ctx;
	return 0;
}

typedef enum {
	START_LINE,
	TABLE_OR_ATABLE,
	TABLE_ID,
	TABLE_DOT_OR_END,
	ATABLE_ID,
	ATABLE_DOT_OR_END,
	ATABLE_CLOSE,
	NEWLINE,
	VALUE_EQUALS,
	VALUE,
	DONE,
}
toml2_parse_mode_t;

typedef int(*trans_t)(toml2_parse_t*, toml2_token_t*, toml2_parse_mode_t);

// toml2_g_adj_ctx sets the top of the context stack to a sub-element,
// allocating the sub-element if needed. This creates does not set the
// new element type, but will return an error if the parent element is
// not a TOML2_TABLE.
static int
toml2_g_adj_ctx(toml2_parse_t *p, toml2_token_t *t, toml2_parse_mode_t m)
{
	toml2_t *doc = toml2_parse_top(p);
	if (0 == doc->type) {
		doc->type = TOML2_TABLE;
	}
	else if (TOML2_TABLE != doc->type) {
		return TOML2_TABLE_REASSIGNED;
	}

	const char *name = NULL;
	if (TOML2_TOKEN_IDENTIFIER == t->type || TOML2_TOKEN_STRING == t->type) {
		name = toml2_token_utf8(p->lex, t);
	}
	else {
		return TOML2_INTERNAL_ERROR;
	}

	toml2_t *sub = toml2_find_subdoc(doc, name);
	if (NULL == sub) {
		sub = malloc(sizeof(toml2_t));
		toml2_init(sub);
		sub->name = name;
		RB_INSERT(toml2_tree_t, &doc->tree, sub);
	}

	toml2_parse_set_top(p, sub);
	return 0;
}

// toml2_g_push_ctx pushes a new context onto the stack with the given 
// identifier. The new context has no type set; the intent is that this is used
// for inline arrays/objects which may be nested ad nauseum.
static int
toml2_g_push_ctx(toml2_parse_t *p, toml2_token_t *t, toml2_parse_mode_t m)
{
	toml2_t *doc = toml2_parse_top(p);
	if (0 == doc->type) {
		doc->type = TOML2_TABLE;
	}
	else if (TOML2_TABLE != doc->type) {
		return TOML2_TABLE_REASSIGNED;
	}

	char *name = NULL;
	if (TOML2_TOKEN_IDENTIFIER == t->type || TOML2_TOKEN_STRING == t->type) {
		name = toml2_token_utf8(p->lex, t);
	}
	else {
		return TOML2_INTERNAL_ERROR;
	}

	toml2_t *sub = toml2_find_subdoc(doc, name);
	if (NULL != sub) {
		free(name);
		return TOML2_VALUE_REASSIGNED;
	}

	sub = malloc(sizeof(toml2_t));
	toml2_init(sub);
	sub->name = name;
	RB_INSERT(toml2_tree_t, &doc->tree, sub);
	return 0;
}

// toml2_g_pop_ctx pops a context off the top of the stack. It's useful
// for closing inline tables/arrays.
static int
toml2_g_pop_ctx(toml2_parse_t *p, toml2_token_t *t, toml2_parse_mode_t m)
{
	return toml2_parse_pop(p);
}

// toml2_g_reset_ctx wipes the context stack and pushes the root onto it.
// This is used when a new table is defined but realistically should just
// be implemented in terms of push/pop.
static int
toml2_g_reset_ctx(toml2_parse_t *p, toml2_token_t *t, toml2_parse_mode_t m)
{
	return toml2_parse_reset(p, p->root);
}

// toml2_g_save pulls the saved_name field and associates it with the
// context at the top of the stack. It must be paired with a corresponding
// toml2_g_push_ctx; the pushed context is popped off.
static int
toml2_g_save(toml2_parse_t *p, toml2_token_t *t, toml2_parse_mode_t m)
{
	toml2_t *doc = toml2_parse_top(p);
	if (0 != doc->type) {
		return TOML2_VALUE_REASSIGNED;
	}

	if (TOML2_TOKEN_IDENTIFIER == t->type) {
		// This is actually true/false.
		char *buf = toml2_token_utf8(p->lex, t);
		bool is_true = !strcmp(buf, "true");
		bool is_false = !is_true && !strcmp(buf, "false");
		free(buf);

		if (!is_true && !is_false) {
			return TOML2_MISPLACED_IDENTIFIER;
		}

		doc->type = TOML2_BOOL;
		doc->bval = is_true;
	}
	else if (TOML2_TOKEN_STRING == t->type) {
		doc->type = TOML2_STRING;
		doc->sval = toml2_token_utf8(p->lex, t);
	}
	else if (TOML2_TOKEN_INT == t->type) {
		doc->type = TOML2_INT;
		doc->ival = t->ival;
	}
	else if (TOML2_TOKEN_DOUBLE == t->type) {
		doc->type = TOML2_FLOAT;
		doc->fval = t->fval;
	}
	else if (TOML2_TOKEN_DATE == t->type) {
		doc->type = TOML2_DATE;
		doc->tval = t->time;
	}
	else {
		return TOML2_INTERNAL_ERROR;
	}

	return toml2_g_pop_ctx(p, t, m);
}

typedef struct {
	toml2_token_type_t tok;
	toml2_parse_mode_t next;
	trans_t fn;
}
toml2_g_trans_t;

typedef struct {
	toml2_parse_mode_t mode;
	toml2_g_trans_t transitions[6];
}
toml2_g_node_t;

static const toml2_g_node_t toml2_g_tables[] = {
	{ START_LINE, {
		{ TOML2_TOKEN_BRACKET_OPEN,  TABLE_OR_ATABLE,  &toml2_g_reset_ctx   },
		{ TOML2_TOKEN_IDENTIFIER,    VALUE_EQUALS,     &toml2_g_push_ctx    },
		{ TOML2_TOKEN_STRING,        VALUE_EQUALS,     &toml2_g_push_ctx    },
		{0},
	}},
	{ TABLE_OR_ATABLE, {
		{ TOML2_TOKEN_BRACKET_OPEN,  ATABLE_ID,        NULL                 },
		{ TOML2_TOKEN_IDENTIFIER,    TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{ TOML2_TOKEN_STRING,        TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{0},
	}},
	{ TABLE_ID, {
		{ TOML2_TOKEN_IDENTIFIER,    TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{ TOML2_TOKEN_STRING,        TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{0},
	}},
	{ TABLE_DOT_OR_END, {
		{ TOML2_TOKEN_DOT,           TABLE_ID,         NULL                 },
		{ TOML2_TOKEN_BRACKET_CLOSE, NEWLINE,          NULL                 },
		{0},
	}},
	{ ATABLE_ID, {
		{ TOML2_TOKEN_IDENTIFIER,    TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{ TOML2_TOKEN_STRING,        TABLE_DOT_OR_END, &toml2_g_adj_ctx     },
		{0},
	}},
	{ ATABLE_DOT_OR_END, {
		{ TOML2_TOKEN_DOT,           TABLE_ID,         NULL                 },
		{ TOML2_TOKEN_BRACKET_CLOSE, ATABLE_CLOSE,     NULL                 },
		{0},
	}},
	{ ATABLE_CLOSE, {
		{ TOML2_TOKEN_BRACKET_CLOSE, NEWLINE,          NULL                 },
		{0},
	}},
	{ VALUE_EQUALS, {
		{ TOML2_TOKEN_EQUALS,        VALUE,            NULL                 },
		{0},
	}},
	{ VALUE, {
		{ TOML2_TOKEN_IDENTIFIER,    NEWLINE,          &toml2_g_save        },
		{ TOML2_TOKEN_STRING,        NEWLINE,          &toml2_g_save        },
		{ TOML2_TOKEN_INT,           NEWLINE,          &toml2_g_save        },
		{ TOML2_TOKEN_DOUBLE,        NEWLINE,          &toml2_g_save        },
		{0},
	}},
	{ NEWLINE, {
		{ TOML2_TOKEN_NEWLINE,       START_LINE,       NULL                 },
		{ TOML2_TOKEN_EOF,           DONE,             NULL                 },
	}}
};

int
toml2_parse(toml2_t *root, const char *data, size_t datalen)
{
	toml2_lex_t lexer;
	toml2_parse_t parser;
	toml2_token_t tok;
	toml2_parse_mode_t mode = START_LINE;

	toml2_parse_init(&parser, &lexer, root);
	int ret = toml2_lex_init(&lexer, data, datalen);
	if (0 != ret) {
		goto cleanup;
	}

	size_t num_tables = sizeof(toml2_g_tables) / sizeof(toml2_g_node_t);
	size_t num_trans = sizeof(toml2_g_tables[0].transitions) / sizeof(toml2_g_trans_t);

	do {
		ret = toml2_lex_token(&lexer, &tok);
		if (ret) {
			goto cleanup;
		}

		const toml2_g_trans_t *next = NULL;

		for (size_t i = 0; i < num_tables && NULL == next; i += 1) {
			const toml2_g_node_t *tb = &toml2_g_tables[i];
			if (tb->mode != mode) {
				continue;
			}

			for (size_t j = 0; j < num_trans; j += 1) {
				const toml2_g_trans_t *tr = &tb->transitions[j];
				if (tr->tok != tok.type) {
					continue;
				}

				next = tr;
				break;
			}
		}
			
		if (NULL == next) {
			ret = TOML2_PARSE_ERROR;
			goto cleanup;
		}

		if (NULL != next->fn) {
			ret = next->fn(&parser, &tok, mode);
		}

		mode = next->next;
	}
	while (DONE != mode);

	cleanup: {
		toml2_parse_free(&parser);
		toml2_lex_free(&lexer);
		return ret;
	}
}
