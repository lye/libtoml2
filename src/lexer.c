#include "toml2.h"
#include "toml2-lexer.h"
#include <stdlib.h>
#include <string.h>
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
toml2_lex_advance_n(toml2_lex_t *lex, size_t count)
{
	if (count > lex->buf_left) {
		count = lex->buf_left;
	}

	lex->buf_left -= count;
	lex->buf += count;
	lex->col += count;
}

static UChar
toml2_lex_peek(toml2_lex_t *lex, size_t off)
{
	if (off > lex->buf_left) {
		return 0;
	}

	return lex->buf[off];
}

static size_t
toml2_lex_pos(toml2_lex_t *lex)
{
	return lex->buf - lex->buf_start;
}

static int
toml2_lex_emit(
	toml2_lex_t *lex,
	toml2_token_t *tok,
	size_t len,
	toml2_token_type_t type
) {
	tok->start = toml2_lex_pos(lex);
	tok->end = tok->start + len;
	tok->type = type;
	tok->line = lex->line;
	tok->col = lex->col;
	lex->err.err = 0;
	return 0;
}

static bool
toml2_is_whitespace(UChar ch)
{
	return ' ' == ch || '\r' == ch || '\t' == ch;
}

static void
toml2_lex_eat_whitespace(toml2_lex_t *lex)
{
	while (lex->buf_left > 0) {
		UChar p = lex->buf[0];

		if (!toml2_is_whitespace(p)) {
			break;
		}

		toml2_lex_advance(lex, false);
	}
}

static int
toml2_lex_comment(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
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

	return toml2_lex_emit(lex, tok, 0, TOML2_TOKEN_COMMENT);
}

static UChar
toml2_lex_unescape_table(toml2_lex_t *lex, UChar ch)
{
	switch (ch) {
		case 'b': return '\b';
		case 't': return '\t';
		case 'n': return '\n';
		case 'f': return '\f';
		case 'r': return '\r';
		case '\\': return '\\';
		case '"': return '"';
		// u/U handled in toml2_lex_unescape.
		default: return 0;
	}
}

static int
toml2_lex_unescape_code(toml2_lex_t *lex, size_t pos, size_t len, UChar **wpos)
{
	UChar32 v = 0;

	for (size_t i = 0; i < len; i += 1) {
		v <<= 4;

		UChar ch = toml2_lex_peek(lex, pos + i);
		if ('a' <= ch && 'f' >= ch) {
			v += (ch - 'a') + 10;
		}
		else if ('A' <= ch && 'F' >= ch) {
			v += (ch - 'A') + 10;
		}
		else if ('0' <= ch && '9' >= ch) {
			v += ch - '0';
		}
		else {
			return 1;
		}
	}

	int32_t dstout = 2;
	UErrorCode uerr = 0;

	// This is cheating but I don't care.
	u_strFromUTF32(*wpos, 2, &dstout, &v, 1, &uerr);
	if (uerr && uerr != U_STRING_NOT_TERMINATED_WARNING) {
		return 1;
	}

	*wpos += dstout;
	return 0;
}

static int
toml2_lex_unescape(toml2_lex_t *lex, size_t pos, size_t len, UChar **wpos) {
	UChar ch = toml2_lex_peek(lex, pos);
	UChar rep = toml2_lex_unescape_table(lex, ch);
	if (0 != rep) {
		**wpos = rep;
		*wpos += 1;
		return 1;
	}

	if ('u' != ch && 'U' != ch) {
		return 0;
	}

	// So, our underlying buffer is UTF-16; the \u escape is a single
	// UTF-16 scalar and \U is a UTF-32 scalar. I'm *pretty* sure that in the
	// latter case we can just treat it as two separate UTF-16 code points.
	// That might be totally wrong though :sadface:.

	int ret;
	size_t inlen = 'u' == ch ? 4 : 8;

	if (inlen + 1 > len) {
		return 0;
	}
	if (0 != (ret = toml2_lex_unescape_code(lex, pos + 1, inlen, wpos))) {
		return 0;
	}

	return inlen + 1;
}

// toml2_lex_demangle does an in-place modification from pos:pos+*orig_len and 
// replaces any escape codes and such with their corresponding byte strings.
// The underlying position is unchanged, but the new length is written out
// to orig_len.
static int
toml2_lex_demangle(
	toml2_lex_t *lex,
	size_t *orig_len
) {
	UChar *wpos = lex->buf;

	for (size_t i = 0; i < *orig_len; i += 1) {
		UChar ch = toml2_lex_peek(lex, i);
		if (0 == ch) {
			// This should be impossible.
			lex->err.err = TOML2_ERR_INTERNAL;
			return 1;
		}

		if ('\\' == ch) {
			size_t off = toml2_lex_unescape(
				lex,
				i + 1,
				*orig_len - i - 1,
				&wpos
			);
			if (0 == off) {
				lex->err.err = TOML2_INVALID_ESCAPE;
				return 1;
			}

			i += off;
			continue;
		}

		*wpos = ch;
		wpos += 1;
	}

	*orig_len = wpos - lex->buf;
	return 0;
}

static int
toml2_lex_quote(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
	// Consume until we hit EOF (error), NL (error) or a close quote.
	// When !TOML2_QUOTE_SINGLE, we need to be aware of escaped quotes.
	const char q = (flags & TOML2_QUOTE_SINGLE) ? '\'' : '"';

	size_t len = 0;
	bool escape = false;

	// Skip the leading quote.
	toml2_lex_advance(lex, false);

	for (;; len += 1) {
		UChar next = toml2_lex_peek(lex, len);
		if (0 == next) {
			lex->err.err = q == '"'
				? TOML2_UNCLOSED_DQUOTE
				: TOML2_UNCLOSED_SQUOTE;
			return 1;
		}

		if (q == '"' && next == '\\') {
			escape = !escape;
			continue;
		}

		if ('\n' == next) {
			lex->err.err = q == '"'
				? TOML2_UNCLOSED_DQUOTE
				: TOML2_UNCLOSED_SQUOTE;
			return 1;
		}

		if (q == next) {
			if (q == '"' && escape) {
				escape = false;
				continue;
			}
			break;
		}

		escape = false;
	};

	if (q == '"') {
		size_t new_len = len;

		int ret = toml2_lex_demangle(lex, &new_len);
		if (0 != ret) {
			return ret;
		}

		toml2_lex_emit(lex, tok, new_len, TOML2_TOKEN_STRING);
	}
	else {
		toml2_lex_emit(lex, tok, len, TOML2_TOKEN_STRING);
	}

	toml2_lex_advance_n(lex, len + 1);
	return 0;
}

static int
toml2_lex_tquote(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
	toml2_lex_advance_n(lex, 3);

	const char q = (flags & TOML2_QUOTE_SINGLE) ? '\'' : '"';
	
	// If the first character is a newline or a backspace+newline, trim off
	// the leading whitespace.
	UChar ch = toml2_lex_peek(lex, 0);
	if (0 == ch) {
		lex->err.err = (q == '"')
			? TOML2_UNCLOSED_TDQUOTE
			: TOML2_UNCLOSED_TSQUOTE;
		return 1;
	}
	else if ('\n' == ch) {
		toml2_lex_advance(lex, true);
		toml2_lex_eat_whitespace(lex);
	}
	else if ('\\' == ch && '\n' == toml2_lex_peek(lex, 1)) {
		// XXX: The spec is a bit ambiguous; not sure if this requires triple
		// double quotes or also works with triple singles. Just making it
		// work with both now.
		toml2_lex_advance(lex, false);
		toml2_lex_advance(lex, true);
		toml2_lex_eat_whitespace(lex);
	}

	// Then look for the triple end, noting that with """ \\n trims whitespace
	// after the newline.
	size_t pos = 0;
	size_t quotes = 0;
	UChar *wpos = lex->buf;

	for (;; pos += 1) {
		ch = toml2_lex_peek(lex, pos);
		if (0 == ch) {
			lex->err.err = (q == '"')
				? TOML2_UNCLOSED_TDQUOTE
				: TOML2_UNCLOSED_TSQUOTE;
			return 1;
		}

		if (ch == '\n') {
			lex->line += 1;
			lex->col = 1;
		} else {
			lex->col += 1;
		}

		// With """, \\n trims whitespace until the next char isn't whitespace.
		if ('\\' == ch && '"' == q && '\n' == toml2_lex_peek(lex, pos + 1)) {
			pos += 2;
			lex->line += 1;
			lex->col = 1;

			while (toml2_is_whitespace(toml2_lex_peek(lex, pos + 1))) {
				lex->col += 1;
				pos += 1;
			}

			continue;
		}

		if (q == ch) {
			quotes += 1;
		} else {
			quotes = 0;
		}

		if (quotes == 3) {
			// The previous two quotes don't count, back up.
			wpos -= 2;
			pos -= 2;
			break;
		}

		*wpos++ = ch;
	}

	// For the emission, ignore the trailing '''/""".
	size_t new_len = wpos - lex->buf;

	// Then """ needs to be demangled.
	if ('"' == q) {
		if (0 != toml2_lex_demangle(lex, &new_len)) {
			return 1;
		}
	}

	toml2_lex_emit(lex, tok, new_len, TOML2_TOKEN_STRING);
	lex->buf += pos + 3;
	lex->buf_left -= pos + 3;
	return 0;
}

static int
toml2_lex_quote_any(toml2_lex_t *lex, toml2_token_t *tok, uint32_t flags)
{
	const char q = (flags & TOML2_QUOTE_SINGLE) ? '\'' : '"';

	// If the next char is a quote, it's either an empty string or
	// a triple quote.
	UChar next = toml2_lex_peek(lex, 1);
	if (0 == next) {
		lex->err.err = q == '"'
			? TOML2_UNCLOSED_DQUOTE
			: TOML2_UNCLOSED_SQUOTE;
		return 1;
	}

	if (next != q) {
		return toml2_lex_quote(lex, tok, flags);
	}

	next = toml2_lex_peek(lex, 2);
	if (q == next) {
		// Triple quote.
		return toml2_lex_tquote(lex, tok, flags);
	}
	else {
		// Empty quote.
		return toml2_lex_emit(lex, tok, 0, TOML2_TOKEN_STRING);
	}
}

static int
toml2_lex_value(toml2_lex_t *lex, toml2_token_t *tok)
{
	return 1; // TODO
}

static int
toml2_lex_id(toml2_lex_t *lex, toml2_token_t *tok)
{
	return 1; // TODO
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
		{ '#',  &toml2_lex_comment,     0                  },
		{ '\'', &toml2_lex_quote_any,   TOML2_QUOTE_SINGLE },
		{ '"',  &toml2_lex_quote_any,   0                  },
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

const char*
toml2_token_utf8(toml2_lex_t *lex, toml2_token_t *tok)
{
	int32_t dstlen = 0;
	int32_t srclen = tok->end - tok->start;
	if (0 == srclen) {
		return strdup("");
	}

	UErrorCode uerr = 0;
	u_strToUTF8(NULL, 0, &dstlen, lex->buf_start + tok->start, srclen, &uerr);
	// See note on toml2_lex_init.
	if (U_BUFFER_OVERFLOW_ERROR != uerr) {
		if (0 != toml2_check_uerr(lex, uerr)) {
			return NULL;
		}
	}

	char *out = calloc(dstlen, sizeof(char));
	uerr = 0;

	u_strToUTF8(out, dstlen, NULL, lex->buf_start + tok->start, srclen, &uerr);
	if (0 != toml2_check_uerr(lex, uerr)) {
		return NULL;
	}

	return out;
}
