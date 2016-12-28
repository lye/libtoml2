#include "toml2.h"
#include "toml2-lexer.h"
#include <stdlib.h>
#include <strings.h>

typedef int (*toml2_lex_fn)(toml2_lex_t*, toml2_token_t*, uint32_t); 

typedef enum {
	TOML2_QUOTE_SINGLE = 1,
}
toml2_lex_flags_t;

static int
toml2_check_uerr(toml2_lex_t *lex, UErrorCode uerr)
{
	if (0 == uerr) {
		return 0;
	}

	// Don't care about null termination; all this code uses lengths.
	if (U_STRING_NOT_TERMINATED_WARNING == uerr) {
		return 0;
	}

	lex->err.err = TOML2_ERR_ICUUC;
	lex->err.code = uerr;
	return 1;
}

int
toml2_lex_init(toml2_lex_t *lex, const char *data, size_t datalen)
{
	bzero(lex, sizeof(lex));

	int32_t srclen = (int32_t) datalen;
	int32_t dstlen = 0;
	int ret;

	UErrorCode uerr = 0;

	u_strFromUTF8(NULL, 0, &dstlen, data, srclen, &uerr);
	// This is annoying, but U_BUFFER_OVERFLOW_ERROR is unconditionally
	// returned if we're preflighting. Ignore that, obviously we're overflowing
	// an empty buffer.
	if (U_BUFFER_OVERFLOW_ERROR != uerr) {
		if (0 != (ret = toml2_check_uerr(lex, uerr))) {
			return ret;
		}
	}

	lex->buf_start = calloc(dstlen, sizeof(UChar));
	lex->buf_len = (size_t) dstlen;
	lex->buf_left = lex->buf_len;

	// Manually clear the error since it's not done for us (and will be
	// set automatically unless datalen==0).
	uerr = 0;

	lex->buf = u_strFromUTF8(
		lex->buf_start,
		dstlen,
		NULL,
		data,
		srclen,
		&uerr
	);
	if (0 != (ret = toml2_check_uerr(lex, uerr))) {
		return ret;
	}
	
	// One-index the line/columns.
	lex->line = 1;
	lex->col = 1;
	
	return 0;
}

void
toml2_lex_free(toml2_lex_t *lex)
{
	free(lex->buf_start);
	bzero(lex, sizeof(toml2_lex_t));
}

static void
toml2_lex_advance(toml2_lex_t *lex, bool newline)
{
	lex->buf_left -= 1;
	lex->buf += 1;

	if (newline) {
		lex->col = 1;
		lex->line += 1;

	} else {
		lex->col += 1;
	}
}

static void
toml2_lex_eat_whitespace(toml2_lex_t *lex)
{
	while (lex->buf_left > 0) {
		UChar p = lex->buf[0];

		if (' ' != p && '\r' != p && '\t' != p) {
			break;
		}

		toml2_lex_advance(lex, false);
	}
}

static int
toml2_lex_comment(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
	tok->type = TOML2_TOKEN_COMMENT;
	tok->start = lex->buf - lex->buf_start;

	do {
		UChar p = lex->buf[0];
		if (p == '\n') {
			// NOTE: Leave the newline in the buf so that a newline token gets
			// generated next.
			break;
		}
		toml2_lex_advance(lex, p == '\n');
	}
	while (lex->buf_left > 0);

	tok->end = lex->buf - lex->buf_start;
	return 0;
}

static int
toml2_lex_quote(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
	return 1;
}

static int
toml2_lex_value(toml2_lex_t *lex, toml2_token_t *tok)
{
	return 1;
}

static int
toml2_lex_id(toml2_lex_t *lex, toml2_token_t *tok)
{
	return 1;
}

int
toml2_lex_token(toml2_lex_t *lex, toml2_token_t *tok)
{
	toml2_lex_eat_whitespace(lex);

	if (0 == lex->buf_left) {
		tok->type = TOML2_TOKEN_EOF;
		return 0;
	}

	UChar p = lex->buf[0];
	
	// Try the single-char tokens first.
	const struct {
		char               val;
		toml2_token_type_t type;
		bool               advance;
	}
	singles[] = {
		{ '\n', TOML2_TOKEN_NEWLINE,       true, },
		{ '[',  TOML2_TOKEN_BRACKET_OPEN,  false },
		{ ']',  TOML2_TOKEN_BRACKET_CLOSE, false },
		{ '{',  TOML2_TOKEN_BRACE_OPEN,    false },
		{ '}',  TOML2_TOKEN_BRACE_CLOSE,   false },
		{ '=',  TOML2_TOKEN_EQUALS,        false },
	};
	for (size_t i = 0; i < sizeof(singles) / sizeof(singles[0]); i += 1) {
		if (singles[i].val == p) {
			tok->type = singles[i].type;
			toml2_lex_advance(lex, singles[i].advance);
			return 0;
		}
	}

	// Then the more complicated tokens.
	const struct {
		char         val;
		toml2_lex_fn fn;
		uint32_t     flags;
	}
	multis[] = {
		{ '#',  &toml2_lex_comment, 0                  },
		{ '\'', &toml2_lex_quote,   TOML2_QUOTE_SINGLE },
		{ '"',  &toml2_lex_quote,   0                  },
	};
	for (size_t i = 0; i < sizeof(multis) / sizeof(multis[0]); i += 1) {
		if (multis[i].val == p) {
			return multis[i].fn(lex, tok, multis[i].flags);
		}
	}

	// Left are identifiers and values; values always start with either
	// [.+-1-9] and are ints, doubles or dates. For the lexer's purposes,
	// booleans are left as identifiers since they're context-specific.
	const char v_starts[] = {
		'+', '-', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.'
	};
	for (size_t i = 0; i < sizeof(v_starts) / sizeof(v_starts[0]); i += 1) {
		if (v_starts[i] == p) {
			return toml2_lex_value(lex, tok);
		}
	}

	// Identifiers are tried last.
	return toml2_lex_id(lex, tok);
}
