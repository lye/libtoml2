#pragma once
#include <sys/types.h>
#include <sys/tree.h>
#include <stdbool.h>
#include <time.h>

typedef struct toml2_t toml2_t;
typedef struct toml2_iter_t toml2_iter_t;
typedef struct toml2_err_t toml2_err_t;
typedef enum toml2_type_t toml2_type_t;
typedef enum toml2_errcode_t toml2_errcode_t;

typedef RB_HEAD(toml2_tree_t, toml2_t) toml2_tree_t;

enum toml2_type_t {
	TOML2_TABLE = 1,
	TOML2_LIST,
	TOML2_INT,
	TOML2_FLOAT,
	TOML2_STRING,
	TOML2_DATE,
	TOML2_BOOL,
};

enum toml2_errcode_t {
	TOML2_NO_ERROR             = 0,
	TOML2_ICUUC_ERROR          = 1,
	TOML2_INTERNAL_ERROR       = 2,
	TOML2_NO_MEMORY            = 3,
	TOML2_UNCLOSED_DQUOTE      = 4,
	TOML2_UNCLOSED_SQUOTE      = 5,
	TOML2_UNCLOSED_TDQUOTE     = 6,
	TOML2_UNCLOSED_TSQUOTE     = 7,
	TOML2_INVALID_ESCAPE       = 8,
	TOML2_INVALID_INT          = 9,
	TOML2_INVALID_DOUBLE       = 10,
	TOML2_INVALID_DATE         = 11,
	TOML2_INVALID_UNDERSCORE   = 12,
	TOML2_TABLE_REASSIGNED     = 13,
	TOML2_VALUE_REASSIGNED     = 14,
	TOML2_PARSE_ERROR          = 15,
	TOML2_MISPLACED_IDENTIFIER = 16,
	TOML2_LIST_REASSIGNED      = 17,
	TOML2_MIXED_LIST           = 18,
};

struct toml2_err_t {
	// line, col contain the position within the buffer that the error was
	// encountered.
	size_t line, col;

	// err contains a TOML2_ERR_* value; for errors that come from other
	// sources (TOML2_ICUUC_ERROR), the actual error code is stored in code.
	toml2_errcode_t err;

	// code contains the actual error if err is TOML2_ERR_ERRNO or
	// TOML2_ICUUC_ERROR.
	int code;
};

struct toml2_t {
	toml2_type_t type;
	const char *name;
	RB_ENTRY(toml2_t) link;
	bool declared;

	union {
		struct {
			size_t ary_len, ary_cap;
			toml2_t *ary;
		};

		struct {
			size_t tree_len;
			toml2_tree_t tree;
		};

		const char *sval;
		int64_t ival;
		double fval;
		bool bval;
		struct tm tval;
	};
};

// toml2_init initalizes an allocated toml2_t object. The toml2_t object may
// be stack-allocated, but must be freed with toml2_free after use (as
// additional heap allocations will be made during use).
void toml2_init(toml2_t *doc);

// toml2_free frees all resources referenced by doc. The doc must be
// reinitialized by toml2_init before re-use.
void toml2_free(toml2_t *doc);

// toml2_parse attempts to parse datalen bytes of TOML-formatted data from
// data onto the heap, referenced from doc. A non-zero return value indicates
// an error. doc must be initialized with toml2_init before use, and cannot
// be re-used for subsequent parses.
int toml2_parse(toml2_t *doc, const char *data, size_t datalen);

// toml2_type_name returns a human-readable string for the given type.
const char* toml2_type_name(toml2_type_t type);

// toml2_type returns the toml2_type_t for the given node.
toml2_type_t toml2_type(toml2_t *node);

// toml2_name returns the UTF8-encoded string for the name of the passed
// node. NULL will be returned unless the parent node is a TOML2_TABLE.
// The name is valid until the node is freed with toml2_free; the caller must
// copy if longer lifetimes are desired.
const char* toml2_name(toml2_t *node);

// toml2_get returns the toml2_t for the corresponding key of the passed node.
// If there is no such node or the input node is not a table, NULL is returned.
// If NULL is passed in, NULL is passed out.
toml2_t* toml2_get(toml2_t *node, const char *key);

// toml2_get_path takes a .-delimited string and returns the corresponding
// subdocument. If there is no such subdocument, it returns NULL. If there
// are type errors (e.g., non-tables along the path) NULL is returned. This
// won't work with keys that have .'s in them -- for that, you'll need to
// use toml2_get.
// 
// As an extra bonus, you can use numbers to indicate array offets, e.g.
// toml2_get_path(node, "foo.1.bar") returns the field "bar" in the second
// table of the "foo" array.
toml2_t* toml2_get_path(toml2_t *node, const char *path);

// toml2_float returns the underlying double value, or 0 if the value
// is not TOML2_FLOAT or TOML2_INT. In the latter case, the value is cast.
double toml2_float(toml2_t *node);

// toml2_bool returns the underlying boolean value, or false if the
// value is not TOML2_BOOL.
bool toml2_bool(toml2_t *node);

// toml2_int returns the underlying int value, or 0 if the value is
// not TOML2_INT or TOML2_FLOAT. In the latter case, the value is cast.
int64_t toml2_int(toml2_t *node);

// toml2_string returns the underlying string value, or NULL if the 
// node is not a TOML2_STRING. The string is UTF8-encoded, and has a lifetime
// bound to the toml2_t -- callers desiring longer lifetimes must copy the
// string.
const char* toml2_string(toml2_t *node);

// toml2_date returns the underlying date value as a C struct tm.
// If the node is note a TOML2_DATE, a zero'd object is returned. The
// tm_wday/tm_yday/tm_isdst/tm_zone fields are never filled out.
struct tm toml2_date(toml2_t *node);

// toml2_len returns the number of subelements. Zero is returned if
// the passed node is NULL, or neither a TOML2_TABLE nor a TOML2_LIST.
size_t toml2_len(toml2_t *node);

// toml2_index returns the N-th element within the passed TOML2_TABLE
// or TOML2_LIST. If the passed node is NULL, or the index is out of bounds,
// NULL is returned. NOTE: For tables this is incredibly inefficient; consider
// using an iterator instead (the entire table is enumerated).
toml2_t* toml2_index(toml2_t *node, size_t idx);
