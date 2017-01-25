#include "toml2.h"
#include "toml2-lexer.h"
#include "toml2-grammar.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

RB_GENERATE(toml2_tree_t, toml2_t, link, toml2_cmp);

int
toml2_cmp(const void *lhs, const void *rhs)
{
	const toml2_t *l = lhs;
	const toml2_t *r = rhs;

	if (NULL == l || NULL == r || NULL == l->name || NULL == r->name) {
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

// This is where a smart person would pull in a parser generator or something.
// Alas I am not a smart person.

typedef enum {
	UNDEFINED,
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
	IARRAY_VAL_OR_END,
	IARRAY_COM_OR_END,
	IARRAY_VAL,
	ITABLE_ID_OR_END,
	ITABLE_ID,
	ITABLE_COLON,
	ITABLE_VAL,
	ITABLE_COM_OR_END,
	DONE,
}
toml2_parse_mode_t;

typedef struct {
	toml2_t *doc;
	toml2_parse_mode_t prev_mode;
}
toml2_frame_t;

typedef struct {
	toml2_lex_t *lex;
	size_t stack_len;
	size_t stack_cap;
	toml2_frame_t *stack;
}
toml2_parse_t;

static int
toml2_frame_new_slot(
	toml2_parse_t *p,
	toml2_frame_t *top,
	toml2_frame_t *out,
	toml2_token_t *tok
) {
	if (0 == top->doc->type) {
		top->doc->type = TOML2_TABLE;
	}
	if (TOML2_TABLE != top->doc->type) {
		return TOML2_INTERNAL_ERROR;
	}
	if (TOML2_TOKEN_STRING != tok->type && TOML2_TOKEN_IDENTIFIER != tok->type) {
		return TOML2_INTERNAL_ERROR;
	}

	char *name = toml2_token_utf8(p->lex, tok);
	toml2_t *doc = toml2_get(top->doc, name);
	if (NULL != doc) {
		// Just free the name, it's already set.
		free(name);
	}
	else {
		// Otherwise need to allocate a new toml2_t and give it the name.
		doc = malloc(sizeof(toml2_t));
		if (NULL == doc) {
			free(name);
			return TOML2_NO_MEMORY;
		}

		toml2_init(doc);
		doc->name = name;

		RB_INSERT(toml2_tree_t, &top->doc->tree, doc);
		top->doc->tree_len += 1;
	}

	out->doc = doc;
	out->prev_mode = 0;
	return 0;
}

static int
toml2_frame_push_slot(toml2_frame_t *top, toml2_frame_t *out)
{
	if (top->doc->ary_len == top->doc->ary_cap) {
		size_t new_cap = top->doc->ary_cap + 3;
		void *new_data = realloc(top->doc->ary, new_cap * sizeof(toml2_t));
		if (NULL == new_data) {
			return TOML2_NO_MEMORY;
		}

		top->doc->ary_cap = new_cap;
		top->doc->ary = new_data;
	}

	out->doc = &top->doc->ary[top->doc->ary_len];
	out->prev_mode = 0;
	toml2_init(out->doc);

	top->doc->ary_len += 1;
	return 0;
}

static int
toml2_frame_save(toml2_frame_t *top, toml2_lex_t *lex, toml2_token_t *tok)
{
	if (TOML2_TOKEN_STRING == tok->type) {
		top->doc->type = TOML2_STRING;
		top->doc->sval = toml2_token_utf8(lex, tok);
	}
	else if (TOML2_TOKEN_IDENTIFIER == tok->type) {
		char *val = toml2_token_utf8(lex, tok);
		if (NULL == val) {
			return TOML2_NO_MEMORY;
		}

		bool is_true = !strcmp(val, "true");
		bool is_false = !strcmp(val, "false");
		free(val);

		if (!is_true && !is_false) {
			return TOML2_MISPLACED_IDENTIFIER;
		}

		top->doc->type = TOML2_BOOL;
		top->doc->bval = is_true;
	}
	else if (TOML2_TOKEN_INT == tok->type) {
		top->doc->type = TOML2_INT;
		top->doc->ival = tok->ival;
	}
	else if (TOML2_TOKEN_DOUBLE == tok->type) {
		top->doc->type = TOML2_FLOAT;
		top->doc->fval = tok->fval;
	}
	else if (TOML2_TOKEN_DATE == tok->type) {
		top->doc->type = TOML2_DATE;
		top->doc->tval = tok->time;
	}
	else {
		return TOML2_PARSE_ERROR;
	}

	return 0;
}

static void
toml2_parse_init(toml2_parse_t *p, toml2_lex_t *lex)
{
	bzero(p, sizeof(toml2_parse_t));
	p->lex = lex;
}

static void
toml2_parse_free(toml2_parse_t *p)
{
	free(p->stack);
}

static toml2_frame_t*
toml2_parse_top(toml2_parse_t *p)
{
	if (0 == p->stack_len) {
		return NULL;
	}

	return &p->stack[p->stack_len - 1];
}

static int
toml2_parse_push(toml2_parse_t *p, toml2_frame_t frame)
{
	if (p->stack_len == p->stack_cap) {
		size_t new_cap = p->stack_cap + 3;
		void *new_data = realloc(p->stack, new_cap * sizeof(toml2_frame_t));
		if (NULL == new_data) {
			return TOML2_NO_MEMORY;
		}

		p->stack_cap = new_cap;
		p->stack = new_data;
	}

	p->stack[p->stack_len] = frame;
	p->stack_len += 1;
	return 0;
}

// toml2_g_subfield sets the top frame to it's subfield, specified by the
// current token. The top frame must be a table or untyped; in the latter 
// case it is typed as a table. If the subfield doesn't exist, it is created
// as an untyped field.
//
// This is used when resolving table declarations.
static int
toml2_g_subfield(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}

	if (0 == top->doc->type) {
		top->doc->type = TOML2_TABLE;
	}
	else if (TOML2_LIST == top->doc->type) {
		// If the current frame is a list, select the last entry in the
		// list as the current frame. If there is no frame, create it.
		if (!top->doc->declared) {
			return TOML2_LIST_REASSIGNED;
		}

		if (0 != top->doc->ary_len) {
			top->doc = &top->doc->ary[top->doc->ary_len - 1];
		}
		else {
			toml2_frame_t newtop;
			int err = toml2_frame_push_slot(top, &newtop);
			if (0 != err) {
				return err;
			}

			*top = newtop;
			top->doc->declared = true;
		}
	}
	else if (TOML2_TABLE != top->doc->type) {
		return TOML2_TABLE_REASSIGNED;
	}
	if (TOML2_TOKEN_STRING != tok->type && TOML2_TOKEN_IDENTIFIER != tok->type) {
		return TOML2_INTERNAL_ERROR;
	}

	toml2_frame_t new;
	int ret = toml2_frame_new_slot(p, top, &new, tok);
	if (0 != ret) {
		return ret;
	}

	*top = new;
	return 0;
}

// toml2_g_subtable sets the top frame to a new entry in the list contained
// in the top frame. Top frame must be a list or untyped.
static int
toml2_g_subtable(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}

	if (0 == top->doc->type) {
		top->doc->type = TOML2_LIST;
		top->doc->declared = true;
	}
	else if (TOML2_LIST != top->doc->type) {
		return TOML2_LIST_REASSIGNED;
	}
	else if (!top->doc->declared) {
		return TOML2_LIST_REASSIGNED;
	}

	toml2_frame_t new;
	int ret;
   	if (0 != (ret = toml2_frame_push_slot(top, &new))) {
		return ret;
	}
	new.doc->type = TOML2_TABLE;
	new.doc->declared = true;

	*top = new;

	return 0;
}

// toml2_g_endtable does misc. cleanup for the end of a table declaration.
static int
toml2_g_endtable(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}

	if (0 == top->doc->type) {
		// Force to a table to allow empty tables.
		top->doc->type = TOML2_TABLE;
	}
	else if (TOML2_TABLE != top->doc->type) {
		return TOML2_INTERNAL_ERROR;
	}

	// Ensure that this is a new table.
	if (top->doc->declared) {
		return TOML2_TABLE_REASSIGNED;
	}
	top->doc->declared = true;

	return 0;
}

// toml2_g_reset pops off the top frame and pushes the first frame back on,
// effectively reverting the top frame to the root node.
static int
toml2_g_reset(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	if (2 != p->stack_len) {
		return TOML2_INTERNAL_ERROR;
	}

	p->stack[1] = p->stack[0];
	return 0;
}

// toml2_g_name pushes a new frame with the name set to the token value.
// The top frame must be a table or untyped.
static int
toml2_g_name(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}
	if (0 == top->doc->type) {
		top->doc->type = TOML2_TABLE;
	}
	else if (TOML2_TABLE != top->doc->type) {
		return TOML2_INTERNAL_ERROR;
	}

	toml2_frame_t new;
	int ret;

	if (0 != (ret = toml2_frame_new_slot(p, top, &new, tok))) {
		return ret;
	}

	if (0 != (ret = toml2_parse_push(p, new))) {
		return ret;
	}

	return 0;
}

// toml2_g_save stores the token in the top frame, then pops it off the stack.
static int
toml2_g_save(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}
	if (0 != top->doc->type) {
		return TOML2_VALUE_REASSIGNED;
	}

	int ret;
	if (0 != (ret = toml2_frame_save(top, p->lex, tok))) {
		return ret;
	}

	p->stack_len -= 1;
	return 0;
}

// toml2_g_append appends a value to the list in the top frame, then persists
// the current token to it. The current frame is unchanged. If the list is 
// not empty, the new value must be the same type as the first value.
static int
toml2_g_append(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}
	if (0 == top->doc->type) {
		top->doc->type = TOML2_LIST;
	}
	else if (TOML2_LIST != top->doc->type) {
		return TOML2_LIST_REASSIGNED;
	}

	toml2_frame_t new;
	int ret;

	if (0 != (ret = toml2_frame_push_slot(top, &new))) {
		return ret;
	}
	if (0 != (ret = toml2_frame_save(&new, p->lex, tok))) {
		return ret;
	}

	if (1 < top->doc->ary_len) {
		toml2_type_t old = top->doc->ary[0].type;
		toml2_type_t new = top->doc->ary[1].type;
		if (old != new) {
			return TOML2_MIXED_LIST;
		}
	}	

	return 0;
}

// toml2_g_push pushes a new frame on the stack, saving the current parser
// mode. The new frame is either an array or a table, depending on the token.
// If the top of the stack is untyped (via toml2_g_name) it is consumed,
// otherwise the stack is unaltered.
static int
toml2_g_push(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	toml2_frame_t *top = toml2_parse_top(p);
	if (NULL == top) {
		return TOML2_INTERNAL_ERROR;
	}

	// This is a bit of a mess; if the top-level object is an array, we need
	// to append an entry. Otherwise it should be untyped and we can use it
	// directly -- replacing the frame.
	toml2_frame_t new;
	toml2_type_t new_type;
	int ret;

	// If the token is [, create a list; if { an object.
	if (TOML2_TOKEN_BRACKET_OPEN == tok->type) {
		new_type = TOML2_LIST;
	}
	else if (TOML2_TOKEN_BRACE_OPEN == tok->type) {
		new_type = TOML2_TABLE;
	}
	else {
		return TOML2_INTERNAL_ERROR;
	}

	if (TOML2_LIST == top->doc->type) {
		if (0 != (ret = toml2_frame_push_slot(top, &new))) {
			return ret;
		}

		// Enforce that, if the original top was a list that the new type
		// matches the first type.
		if (top->doc->ary_len > 1 && new_type != top->doc->ary[0].type) {
			return TOML2_MIXED_LIST;
		}
	}
	else if (0 != top->doc->type) {
		return TOML2_VALUE_REASSIGNED;
	}
	else {
		new = *top;
		if (1 == p->stack_len) {
			return TOML2_INTERNAL_ERROR;
		}
		p->stack_len -= 1;
	}

	new.prev_mode = *m;
	new.doc->declared = new.doc->declared || new_type == TOML2_TABLE;
	new.doc->type = new_type;
	
	return toml2_parse_push(p, new);
}

// toml2_g_pop removes the top frame of the stack and reverts the parser mode
// to what it was when the frame was pushed.
static int
toml2_g_pop(toml2_parse_t *p, toml2_token_t *tok, toml2_parse_mode_t *m)
{
	if (2 >= p->stack_len) {
		return TOML2_INTERNAL_ERROR;
	}

	toml2_frame_t *top = toml2_parse_top(p);
	if (0 == top->prev_mode) {
		return TOML2_INTERNAL_ERROR;
	}

	// Figure out the next parse mode based on where we stopped parsing.
	if (VALUE == top->prev_mode) {
		*m = NEWLINE;
	}
	else if (
		IARRAY_VAL == top->prev_mode
		|| IARRAY_VAL_OR_END == top->prev_mode
	) {
		*m = IARRAY_COM_OR_END;
	}
	else if (ITABLE_VAL == top->prev_mode) {
		*m = ITABLE_COM_OR_END;
	}
	else {
		return TOML2_INTERNAL_ERROR;
	}

	p->stack_len -= 1;
	return 0;
}

typedef int(*trans_t)(toml2_parse_t*, toml2_token_t*, toml2_parse_mode_t*);

typedef struct {
	toml2_token_type_t tok;
	toml2_parse_mode_t next;
	trans_t fn;
}
toml2_g_trans_t;

typedef struct {
	toml2_parse_mode_t mode;
	toml2_g_trans_t transitions[10];
}
toml2_g_node_t;

static const toml2_g_node_t toml2_g_tables[] = {
	{ START_LINE, {
		{ TOML2_TOKEN_BRACKET_OPEN,  TABLE_OR_ATABLE,  &toml2_g_reset       },
		{ TOML2_TOKEN_IDENTIFIER,    VALUE_EQUALS,     &toml2_g_name        },
		{ TOML2_TOKEN_STRING,        VALUE_EQUALS,     &toml2_g_name        },
		{ TOML2_TOKEN_EOF,           DONE,             NULL                 },
		{ TOML2_TOKEN_NEWLINE,       START_LINE,       NULL                 },
		{0},
	}},
	{ TABLE_OR_ATABLE, {
		{ TOML2_TOKEN_BRACKET_OPEN,  ATABLE_ID,        NULL                 },
		{ TOML2_TOKEN_IDENTIFIER,    TABLE_DOT_OR_END, &toml2_g_subfield    },
		{ TOML2_TOKEN_STRING,        TABLE_DOT_OR_END, &toml2_g_subfield    },
		{0},
	}},
	{ TABLE_ID, {
		{ TOML2_TOKEN_IDENTIFIER,    TABLE_DOT_OR_END, &toml2_g_subfield    },
		{ TOML2_TOKEN_STRING,        TABLE_DOT_OR_END, &toml2_g_subfield    },
		{0},
	}},
	{ TABLE_DOT_OR_END, {
		{ TOML2_TOKEN_DOT,           TABLE_ID,         NULL                 },
		{ TOML2_TOKEN_BRACKET_CLOSE, NEWLINE,          &toml2_g_endtable    },
		{0},
	}},
	{ ATABLE_ID, {
		{ TOML2_TOKEN_IDENTIFIER,    ATABLE_DOT_OR_END, &toml2_g_subfield   },
		{ TOML2_TOKEN_STRING,        ATABLE_DOT_OR_END, &toml2_g_subfield   },
		{0},
	}},
	{ ATABLE_DOT_OR_END, {
		{ TOML2_TOKEN_DOT,           ATABLE_ID,        NULL                 },
		{ TOML2_TOKEN_BRACKET_CLOSE, ATABLE_CLOSE,     NULL                 },
		{0},
	}},
	{ ATABLE_CLOSE, {
		{ TOML2_TOKEN_BRACKET_CLOSE, NEWLINE,          &toml2_g_subtable    },
		{0},
	}},
	{ VALUE_EQUALS, {
		{ TOML2_TOKEN_EQUALS,        VALUE,            NULL                 },
		{0},
	}},
	{ VALUE, {
		{ TOML2_TOKEN_STRING,        NEWLINE,           &toml2_g_save       },
		{ TOML2_TOKEN_INT,           NEWLINE,           &toml2_g_save       },
		{ TOML2_TOKEN_DOUBLE,        NEWLINE,           &toml2_g_save       },
		{ TOML2_TOKEN_IDENTIFIER,    NEWLINE,           &toml2_g_save       },
		{ TOML2_TOKEN_DATE,          NEWLINE,           &toml2_g_save       },
		{ TOML2_TOKEN_BRACKET_OPEN,  IARRAY_VAL_OR_END, &toml2_g_push       },
		{ TOML2_TOKEN_BRACE_OPEN,    ITABLE_ID_OR_END,  &toml2_g_push       },
		{0},
	}},
	{ IARRAY_VAL_OR_END, {
		{ TOML2_TOKEN_STRING,        IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_INT,           IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_DOUBLE,        IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_IDENTIFIER,    IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_DATE,          IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_BRACKET_OPEN,  IARRAY_VAL_OR_END, &toml2_g_push       },
		{ TOML2_TOKEN_BRACE_OPEN,    ITABLE_ID_OR_END,  &toml2_g_push       },
		{ TOML2_TOKEN_BRACKET_CLOSE, UNDEFINED,         &toml2_g_pop        },
		{ TOML2_TOKEN_NEWLINE,       IARRAY_VAL_OR_END, NULL                },
		{0},
	}},
	{ IARRAY_COM_OR_END, {
		{ TOML2_TOKEN_COMMA,         IARRAY_VAL,        NULL                },
		{ TOML2_TOKEN_BRACKET_CLOSE, UNDEFINED,         &toml2_g_pop        },
		{ TOML2_TOKEN_NEWLINE,       IARRAY_COM_OR_END, NULL                },
		{0},
	}},
	{ IARRAY_VAL, {
		{ TOML2_TOKEN_STRING,        IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_INT,           IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_DOUBLE,        IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_IDENTIFIER,    IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_DATE,          IARRAY_COM_OR_END, &toml2_g_append     },
		{ TOML2_TOKEN_BRACKET_OPEN,  IARRAY_VAL_OR_END, &toml2_g_push       },
		{ TOML2_TOKEN_BRACE_OPEN,    ITABLE_ID_OR_END,  &toml2_g_push       },
		{ TOML2_TOKEN_BRACKET_CLOSE, UNDEFINED,         &toml2_g_pop        },
		{ TOML2_TOKEN_NEWLINE,       IARRAY_VAL,        NULL                },
		{0},
	}},
	{ ITABLE_ID_OR_END, {
		{ TOML2_TOKEN_STRING,        ITABLE_COLON,      &toml2_g_name       },
		{ TOML2_TOKEN_BRACE_CLOSE,   UNDEFINED,         &toml2_g_pop        },
		{ TOML2_TOKEN_NEWLINE,       ITABLE_ID_OR_END,  NULL                },
		{0}
	}},
	{ ITABLE_COLON, {
		{ TOML2_TOKEN_COLON,         ITABLE_VAL,        NULL                },
		{ TOML2_TOKEN_NEWLINE,       ITABLE_COLON,      NULL                },
		{0}
	}},
	{ ITABLE_VAL, {
		{ TOML2_TOKEN_STRING,        ITABLE_COM_OR_END, &toml2_g_save       },
		{ TOML2_TOKEN_INT,           ITABLE_COM_OR_END, &toml2_g_save       },
		{ TOML2_TOKEN_DOUBLE,        ITABLE_COM_OR_END, &toml2_g_save       },
		{ TOML2_TOKEN_IDENTIFIER,    ITABLE_COM_OR_END, &toml2_g_save       },
		{ TOML2_TOKEN_DATE,          ITABLE_COM_OR_END, &toml2_g_save       },
		{ TOML2_TOKEN_BRACKET_OPEN,  IARRAY_VAL_OR_END, &toml2_g_push       },
		{ TOML2_TOKEN_BRACE_OPEN,    ITABLE_ID_OR_END,  &toml2_g_push       },
		{ TOML2_TOKEN_NEWLINE,       ITABLE_VAL,        NULL                },
		{0},
	}},
	{ ITABLE_COM_OR_END, {
		{ TOML2_TOKEN_COMMA,         ITABLE_ID,         NULL                },
		{ TOML2_TOKEN_BRACE_CLOSE,   UNDEFINED,         &toml2_g_pop        },
		{ TOML2_TOKEN_NEWLINE,       ITABLE_COM_OR_END, NULL                },
		{0},
	}},
	{ ITABLE_ID, {
		{ TOML2_TOKEN_STRING,        ITABLE_COLON,      &toml2_g_name       },
		{ TOML2_TOKEN_NEWLINE,       ITABLE_ID,         NULL                },
		{0},
	}},
	{ NEWLINE, {
		{ TOML2_TOKEN_NEWLINE,       START_LINE,       NULL                 },
		{ TOML2_TOKEN_EOF,           DONE,             NULL                 },
		{0},
	}}
};

int
toml2_parse(toml2_t *root, const char *data, size_t datalen)
{
	int ret;
	toml2_lex_t lexer;
	toml2_parse_t parser;
	toml2_token_t tok;
	toml2_parse_mode_t mode = START_LINE;
	toml2_frame_t root_frame = {
		.doc = root,
		.prev_mode = 0,
	};

	// root node is always a table.
	root->type = TOML2_TABLE;

	toml2_parse_init(&parser, &lexer);

	if (0 != (ret = toml2_lex_init(&lexer, data, datalen))) {
		goto cleanup;
	}
	if (0 != (ret = toml2_parse_push(&parser, root_frame))) {
		goto cleanup;
	}
	if (0 != (ret = toml2_parse_push(&parser, root_frame))) {
		goto cleanup;
	}

	size_t num_tables = sizeof(toml2_g_tables) / sizeof(toml2_g_node_t);
	size_t num_trans = sizeof(toml2_g_tables[0].transitions) / sizeof(toml2_g_trans_t);

	do {
		ret = toml2_lex_token(&lexer, &tok);
		if (ret) {
			goto cleanup;
		}

		if (TOML2_TOKEN_COMMENT == tok.type) {
			continue;
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
			
		// BREAK HERE
		if (NULL == next) {
			ret = TOML2_PARSE_ERROR;
			goto cleanup;
		}

		toml2_parse_mode_t orig_mode = mode;

		if (NULL != next->fn) {
			ret = next->fn(&parser, &tok, &mode);
			if (0 != ret) {
				goto cleanup;
			}
		}

		if (UNDEFINED != next->next && orig_mode == mode) {
			mode = next->next;
		}
	}
	while (DONE != mode);

	cleanup: {
		toml2_parse_free(&parser);
		toml2_lex_free(&lexer);
		return ret;
	}
}
