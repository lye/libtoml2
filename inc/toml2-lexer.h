#pragma once
#include <sys/types.h>
#include <unicode/utypes.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <time.h>

// toml2_lex_t encapsulates both the decoded UTF8 buffer and the current
// lexer state for an in-progress lex. The lexer is currently streaming --
// rather than lexing the entire document, the next token can be retrieved via
// toml2_lex_token. The toml2_lex_tokens contain references to the original
// buffer and provide functionality to extract the underlying data in the
// format desired (e.g. UTF8).
//
// It should be noted that the underlying decoded buffer *will* be modified
// in-place to remove extraneous characters (e.g., '["foo"]' will be rewritten
// as '[foo]  '; escape codes will be handled in a similar manner. The lexer
// will overwrite extra with ' ' and advance the lexer state past the decoded
// characters (and the decoded tokens will only have the not-extra bits).
//
typedef struct {
	// buf_start is the beginning of the heap-allocation for the decoded UTF8
	// string.
	UChar *buf_start;

	// buf is the current position into buf_start that we're lexing.
	UChar *buf;

	// line, col are the current position within buf_start that we're lexing
	// at; these are used for diagnostics and errors.
	size_t line, col;

	// buf_len is the total number of UChars available in buf_start.
	size_t buf_len;

	// buf_left is the total number of UChars remaining in buf.
	size_t buf_left;

	// err contains any error that might be encountered. Stored here rather
	// then passing around an outvalue since this is easier on the hands.
	toml2_err_t err;
}
toml2_lex_t;

typedef enum {
	TOML2_TOKEN_INVALID = 0,
	TOML2_TOKEN_COMMENT = 1,
	TOML2_TOKEN_STRING,
	TOML2_TOKEN_IDENTIFIER,
	TOML2_TOKEN_INT,
	TOML2_TOKEN_DOUBLE,
	TOML2_TOKEN_DATE,
	TOML2_TOKEN_NEWLINE,
	TOML2_TOKEN_EQUALS,
	TOML2_TOKEN_COMMA,
	TOML2_TOKEN_COLON,
	TOML2_TOKEN_DOT,
	TOML2_TOKEN_BRACE_OPEN,
	TOML2_TOKEN_BRACE_CLOSE,
	TOML2_TOKEN_BRACKET_OPEN,
	TOML2_TOKEN_BRACKET_CLOSE,
	TOML2_TOKEN_EOF,
}
toml2_token_type_t;

typedef struct {
	toml2_token_type_t type;
	size_t line, col;
	size_t start, end;

	union {
		int64_t ival;
		double fval;
		struct tm time;
	};
}
toml2_token_t;

// toml2_lex_init initializes a toml2_lex_t with the provided UTF8-encoded data
// at data with datalen bytes. This step decodes the UTF8 data and will return 
// non-zero if invalid UTF8 is provided, so the error code must be checked. The
// error is stored in the toml2_lex_t, which needs to be unconditionally freed
// as allocations may be made regardless of success.
int toml2_lex_init(toml2_lex_t *lex, const char *data, size_t datalen);

// toml2_lex_free releases resources allocated via toml2_lex_t.
void toml2_lex_free(toml2_lex_t *lex);

// toml2_lex_token parses the next token from lex into tok. A non-zero return
// value indicates that there was a lex error.
int toml2_lex_token(toml2_lex_t *lex, toml2_token_t *tok);

// toml2_token_dbg_utf8 returns a staticly-allocated string containing the UTF8
// representation of the underlying data. NULL indicates errors (e.g. 
// unencodable data). As this uses a static allocation, the returned string
// is only valid until the next call.
const char* toml2_token_dbg_utf8(toml2_lex_t *lex, toml2_token_t *tok);

// toml2_token_utf8 works the same way as toml2_token_utf8 but returns a
// heap-allocated string which the caller must free.
char* toml2_token_utf8(toml2_lex_t *lex, toml2_token_t *tok);
