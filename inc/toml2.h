#pragma once
#include <sys/types.h>
#include <stdbool.h>

typedef struct toml2_t toml2_t;
typedef struct toml2_iter_t toml2_iter_t;
typedef struct toml2_err_t toml2_err_t;
typedef enum toml2_type_t toml2_type_t;
typedef enum toml2_errcode_t toml2_errcode_t;

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
	TOML2_ERR_NONE = 0,
	TOML2_ERR_ERRNO,
	TOML2_ERR_ICUUC,
	TOML2_ERR_INTERNAL,
	TOML2_UNCLOSED_DQUOTE,
	TOML2_UNCLOSED_SQUOTE,
	TOML2_UNCLOSED_TDQUOTE,
	TOML2_UNCLOSED_TSQUOTE,
	TOML2_INVALID_ESCAPE,
	TOML2_INVALID_INT,
	TOML2_INVALID_DOUBLE,
	TOML2_INVALID_DATE,
	TOML2_INVALID_UNDERSCORE,
};

struct toml2_err_t {
	// line, col contain the position within the buffer that the error was
	// encountered.
	size_t line, col;

	// err contains a TOML2_ERR_* value; for errors that come from other
	// sources (TOML2_ERR_ERRNO, TOML2_ERR_ICUUC), the actual error code is
	// stored in code.
	toml2_errcode_t err;

	// code contains the actual error if err is TOML2_ERR_ERRNO or
	// TOML2_ERR_ICUUC.
	int code;
};

struct toml2_t {
	// err contains the details of the last-encountered error.
	toml2_err_t err;
};

// toml2_init initalizes an allocated toml2_t object. The toml2_t object may
// be stack-allocated, but must be freed with toml2_free after use (as
// additional heap allocations will be made during use).
void toml2_init(toml2_t *doc);

// toml2_parse attempts to parse datalen bytes of TOML-formatted data from
// data onto the heap, referenced from doc. A non-zero return value indicates
// an error. doc must be initialized with toml2_init before use, and cannot
// be re-used for subsequent parses.
int toml2_parse(toml2_t *doc, char *data, size_t datalen);

// toml2_free frees all resources referenced by doc. The doc must be
// reinitialized by toml2_init before re-use.
void toml2_free(toml2_t *doc);

// toml2_type_name returns a human-readable string for the given type.
const char* toml2_type_name(toml2_type_t type);

// toml2_type returns the toml2_type_t for the given node.
toml2_type_t toml2_type(toml2_t *node);

// toml2_name returns the UTF8-encoded string for the name of the passed
// node. NULL will be returned unless the parent node is a TOML2_TABLE.
// The name is valid until the node is freed with toml2_free; the caller must
// copy if longer lifetimes are desired.
const char* toml2_name(toml2_t *node);

// toml2_value_float returns the underlying double value, or 0 if the value
// is not TOML2_FLOAT.
double toml2_value_float(toml2_t *node);

// toml2_value_bool returns the underlying boolean value, or false if the
// value is not TOML2_BOOL.
bool toml2_value_bool(toml2_t *node);

// toml2_value_int returns the underlying int value, or 0 if the value is
// not TOML2_INT.
int64_t toml2_value_int(toml2_t *node);

// toml2_value_string returns the underlying string value, or NULL if the 
// node is not a TOML2_STRING. The string is UTF8-encoded, and has a lifetime
// bound to the toml2_t -- callers desiring longer lifetimes must copy the
// string.
const char* toml2_value_string(toml2_t *node);

// XXX: toml2_value_date.

// toml2_value_bool returns the underlying boolean value, or false if
// the node is not TOML2_BOOL.
bool toml2_value_bool(toml2_t *node);
