#include "toml2.h"
#include "toml2-lexer.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

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

	lex->err.err = TOML2_ICUUC_ERROR;
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
	if (off >= lex->buf_left) {
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
			lex->err.err = TOML2_INTERNAL_ERROR;
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
		while ('\n' == toml2_lex_peek(lex, 0)) {
			toml2_lex_advance(lex, true);
			toml2_lex_eat_whitespace(lex);
		}
	}
	else if ('\\' == ch && '\n' == toml2_lex_peek(lex, 1)) {
		// XXX: The spec is a bit ambiguous; not sure if this requires triple
		// double quotes or also works with triple singles. Just making it
		// work with both now.
		toml2_lex_advance(lex, false);
		while ('\n' == toml2_lex_peek(lex, 0)) {
			toml2_lex_advance(lex, true);
			toml2_lex_eat_whitespace(lex);
		}
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

		if ('\n' == ch) {
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

			for (
				UChar tmp = toml2_lex_peek(lex, pos + 1);
				toml2_is_whitespace(tmp) || '\n' == tmp;
				tmp = toml2_lex_peek(lex, pos + 1)
			) {
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
		toml2_lex_advance_n(lex, 2);
		return toml2_lex_emit(lex, tok, 0, TOML2_TOKEN_STRING);
	}
}

static int
toml2_lex_int(toml2_lex_t *lex, toml2_token_t *tok, size_t len)
{
	uint64_t val = 0;
	int64_t sign = 1;
	bool prev_number = false;

	for (size_t pos = 0; pos < len; pos += 1) {
		UChar ch = toml2_lex_peek(lex, pos);

		if (pos == 0) {
			if ('-' == ch) {
				sign = -1;
				continue;

			} else if ('+' == ch) {
				// pass
				continue;
			}
		}

		if ('_' == ch) {
			if (!prev_number) {
				lex->err.err = TOML2_INVALID_UNDERSCORE;
				return 1;
			}

			prev_number = false;
			continue;
		}

		if ('0' > ch || '9' < ch) {
			lex->err.err = TOML2_INVALID_INT;
			return 1;
		}

		prev_number = true;
		val *= 10;
		val += ch - '0';
	}

	if (!prev_number) {
		lex->err.err = TOML2_INVALID_UNDERSCORE;
		return 1;
	}

	toml2_lex_emit(lex, tok, len, TOML2_TOKEN_INT);
	tok->ival = ((int64_t) val) * sign;
	toml2_lex_advance_n(lex, len);

	return 0;
}

static int
toml2_lex_double(toml2_lex_t *lex, toml2_token_t *tok, size_t len)
{
	// NOTE: This implementation explicitly does not use strtod because it
	// needs to be locale-independent; the behavior of strtod varies by locale.
	// There's probably a better way to implement this but I'm too lazy to 
	// re-read IEEE754 to make a bit-twiddly version.

	double val = 0;
	double exponent = 0;
	double sign = 1;
	double sign_exp = 1;
	double relpos = 0;
	bool prev_number = false;
	enum {
		MODE_INTEGER,
		MODE_FRACTION,
		MODE_EXPONENT,
	}
	mode = MODE_INTEGER;

	UChar ch;

	for (size_t pos = 0; pos < len; pos += 1) {
		ch = toml2_lex_peek(lex, pos);

		if (relpos == 0 && '-' == ch && mode != MODE_FRACTION) {
			if (MODE_INTEGER == mode) {
				sign = -1;
			}
			else if (MODE_EXPONENT == mode) {
				sign_exp = -1;
			}
			continue;
		}

		if ('.' == ch) {
			if (mode != MODE_INTEGER || 0 == pos) {
				lex->err.err = TOML2_INVALID_DOUBLE;
				return 1;
			}
			if (!prev_number) {
				lex->err.err = TOML2_INVALID_UNDERSCORE;
				return 1;
			}

			mode = MODE_FRACTION;
			prev_number = false;
			relpos = 0;
			continue;
		}

		if ('e' == ch || 'E' == ch) {
			if (mode != MODE_INTEGER && mode != MODE_FRACTION) {
				lex->err.err = TOML2_INVALID_DOUBLE;
				return 1;
			}
			if (!prev_number) {
				lex->err.err = TOML2_INVALID_UNDERSCORE;
				return 1;
			}

			mode = MODE_EXPONENT;
			prev_number = false;
			relpos = 0;
			continue;
		}

		if ('_' == ch) {
			if (!prev_number) {
				lex->err.err = TOML2_INVALID_UNDERSCORE;
				return 1;
			}

			prev_number = false;
			continue;
		}

		if ('0' <= ch && '9' >= ch) {
			switch (mode) {
				case MODE_INTEGER:
					val *= 10;
					val += ch - '0';
					break;

				case MODE_FRACTION:
					val += ((double) (ch - '0')) / pow(10, (relpos + 1));
					break;

				case MODE_EXPONENT:
					exponent *= 10;
					exponent += ch - '0';
					break;
			}

			relpos += 1;
			prev_number = true;
			continue;
		}

		lex->err.err = TOML2_INVALID_DOUBLE;
		return 1;
	}
	if ('_' == ch) {
		lex->err.err = TOML2_INVALID_UNDERSCORE;
		return 1;
	}
	if (!prev_number) {
		lex->err.err = TOML2_INVALID_DOUBLE;
		return 1;
	}

	val *= sign;
	exponent *= sign_exp;

	toml2_lex_emit(lex, tok, len, TOML2_TOKEN_DOUBLE);
	tok->fval = val * pow(10, exponent);
	toml2_lex_advance_n(lex, len);

	return 0;
}

static int
toml2_lex_date(toml2_lex_t *lex, toml2_token_t *tok, size_t len)
{
	enum mode_t {
		MODE_YEAR,
		MODE_MONTH,
		MODE_DAY,
		MODE_HOUR,
		MODE_MINUTE,
		MODE_SECOND,
		MODE_NANOSECOND,
		MODE_OFF_HOUR,
		MODE_OFF_MINUTE,
		MODE_DONE,
	}
	mode = MODE_YEAR;

	int32_t val = 0, spare = 0, sign = 1;
	size_t num_digits = 0;
	UChar ch = 0, prev_ch;

	bzero(&tok->time, sizeof(struct tm));

	// XXX: This could just be implemented as a state table, but alas.
#	define NEXT_MODE(NEXT, FIELD, NUM_DIGIT) \
		if (0 < NUM_DIGIT && NUM_DIGIT != num_digits) { \
			lex->err.err = TOML2_INVALID_DATE; \
			return 1; \
		} \
		num_digits = 0; \
		spare = 0; \
		FIELD = val; \
		val = 0; \
		mode = NEXT; \
		continue

	for (size_t i = 0; i < len; i += 1) {
		prev_ch = ch;
		ch = toml2_lex_peek(lex, i);

		if ('-' == ch) {
			if (MODE_YEAR == mode) {
				NEXT_MODE(MODE_MONTH, tok->time.tm_year, 4);
			}

			if (MODE_MONTH == mode) {
				val -= 1; // erhgh
				NEXT_MODE(MODE_DAY, tok->time.tm_mon, 2);
			}

			if (MODE_SECOND == mode) {
				sign = -1;
				NEXT_MODE(MODE_OFF_HOUR, tok->time.tm_sec, 2);
			}

			if (MODE_NANOSECOND == mode) {
				sign = -1;
				NEXT_MODE(MODE_OFF_HOUR, spare, 0);
			}
		}

		if ('+' == ch) {
			if (MODE_SECOND == mode) {
				NEXT_MODE(MODE_OFF_HOUR, tok->time.tm_sec, 2);
			}

			if (MODE_NANOSECOND == mode) {
				NEXT_MODE(MODE_OFF_HOUR, spare, 0);
			}
		}

		if ('T' == ch || 't' == ch) {
			if (MODE_DAY == mode) {
				NEXT_MODE(MODE_HOUR, tok->time.tm_mday, 2);
			}
		}

		if ('Z' == ch || 'z' == ch) {
			if (MODE_SECOND == mode) {
				NEXT_MODE(MODE_DONE, tok->time.tm_sec, 2);
			}

			if (MODE_NANOSECOND == mode) {
				NEXT_MODE(MODE_DONE, spare, 0);
			}
		}

		if (':' == ch) {
			if (MODE_HOUR == mode) {
				NEXT_MODE(MODE_MINUTE, tok->time.tm_hour, 2);
			}

			if (MODE_MINUTE == mode) {
				NEXT_MODE(MODE_SECOND, tok->time.tm_min, 2);
			}

			if (MODE_OFF_HOUR == mode) {
				NEXT_MODE(MODE_OFF_MINUTE, spare, 2);
			}
		}

		if ('.' == ch) {
			if (MODE_SECOND == mode) {
				NEXT_MODE(MODE_NANOSECOND, tok->time.tm_sec, 0);
			}
		}

		if ('0' <= ch && '9' >= ch) {
			val *= 10;
			val += ch - '0';
			num_digits += 1;
			continue;
		}

		lex->err.err = TOML2_INVALID_DATE;
		return 1;
	}

	for (size_t i = 0; i < 1; i += 1) {
		switch (mode) {
			case MODE_DAY: NEXT_MODE(MODE_DONE, tok->time.tm_mday, 2);
			case MODE_SECOND: NEXT_MODE(MODE_DONE, tok->time.tm_sec, 2);
			case MODE_NANOSECOND: NEXT_MODE(MODE_DONE, spare, 0);

			case MODE_OFF_MINUTE:
				if (2 != num_digits) {
					break;
				}

				tok->time.tm_gmtoff = sign * ((60 * 60 * spare) + val);
				mode = MODE_DONE;
				break;

			default: break;
		}
	}
	if (MODE_DONE != mode) {
		lex->err.err = TOML2_INVALID_DATE;
		return 1;
	}
#	undef NEXT_MODE

	toml2_lex_emit(lex, tok, len, TOML2_TOKEN_DATE);
	toml2_lex_advance_n(lex, len);
	return 0;
}

static int
toml2_lex_value(toml2_lex_t *lex, toml2_token_t *tok)
{
	// Scan the value to see what type and length it is.
	size_t pos = 0;
	toml2_token_type_t type = TOML2_TOKEN_INT;

	UChar ch = 0, prev_ch;

	for (;; pos += 1) {
		prev_ch = ch;
		ch = toml2_lex_peek(lex, pos);

		if ('-' == ch) {
			// A date has a '-' anywhere in it, except as the first character
			// (which is valid for ints+doubles) or after an 'eE' (which is
			// valid for a double as well). Both of the latter cases are
			// invalid dates.
			if (pos != 0 && prev_ch !=  'e' && prev_ch != 'E') {
				type = TOML2_TOKEN_DATE;
			}
		}
		else if ('t' == ch || 'T' == ch || 'z' == ch || 'Z' == ch || ':' == ch) {
			// Date-only chars.
			type = TOML2_TOKEN_DATE;
		}
		else if ('e' == ch || 'E' == ch || '.' == ch) {
			// A '.' can appear in both dates and doubles -- the dates will
			// always have it appear after '-', so give it higher points.
			if (type != TOML2_TOKEN_DATE) {
				type = TOML2_TOKEN_DOUBLE;
			}
		}
		else if ('0' <= ch && '9' >= ch) {
			// pass.
		}
		else if ('+' == ch || '_' == ch) {
			// pass.
		}
		else {
			// No more characters left to parse.
			break;
		}
	}

	if (0 == pos) {
		// This should be impossible.
		lex->err.err = TOML2_INTERNAL_ERROR;
		return 1;
	}

	switch (type) {
		case TOML2_TOKEN_INT: return toml2_lex_int(lex, tok, pos);
		case TOML2_TOKEN_DOUBLE: return toml2_lex_double(lex, tok, pos);
		case TOML2_TOKEN_DATE: return toml2_lex_date(lex, tok, pos);
		default: break;
	}

	// Should also be unreachable.
	lex->err.err = TOML2_INTERNAL_ERROR;
	return 1;
}

static int
toml2_lex_id(toml2_lex_t *lex, toml2_token_t *tok)
{
	// An identifier is any non-empty string of characters that are not 
	// control characters or whitespace.
	//
	// The spec is ambiguous as to which unicode classes are allowed to
	// be in identifiers and strictly defines "whitespace" as a 
	// whitelist, so this implementation just includes literally anything
	// as a valid identifier.
	const UChar reserved[] = {
		'.', ',', '=', '[', ']', '{', '}', ':', '#'
	};
	
	size_t pos = 0;

	for (;; pos += 1) {
		UChar ch = toml2_lex_peek(lex, pos);

		if (0 == ch) {
			break;
		}
		
		if (toml2_is_whitespace(ch) || '\n' == ch) {
			break;
		}

		for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i += 1) {
			if (reserved[i] == ch) {
				goto found;
			}
		}
	}
	found:

	if (0 == pos) {
		// This *should* be unreachable since we should have chomped all 
		// leading whitespace.
		lex->err.err = TOML2_INTERNAL_ERROR;
		return 1;
	}

	toml2_lex_emit(lex, tok, pos, TOML2_TOKEN_IDENTIFIER);
	toml2_lex_advance_n(lex, pos);
	return 0;
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
		{ ',',  TOML2_TOKEN_COMMA,         false },
		{ '.',  TOML2_TOKEN_DOT,           false },
		{ ':',  TOML2_TOKEN_COLON,         false },
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
toml2_token_dbg_utf8(toml2_lex_t *lex, toml2_token_t *tok)
{
	static char buf[256];

	int32_t srclen = tok->end - tok->start;
	if (0 == srclen) {
		return "";
	}

	UErrorCode uerr = 0;
	u_strToUTF8(buf, sizeof(buf), NULL, lex->buf_start + tok->start, srclen, &uerr);
	if (0 != toml2_check_uerr(lex, uerr)) {
		return NULL;
	}

	return buf;
}

char*
toml2_token_utf8(toml2_lex_t *lex, toml2_token_t *tok)
{
	UErrorCode uerr = 0;
	int32_t srclen = tok->end - tok->start;
	int32_t dstlen = 0;
	int ret;

	if (0 == srclen) {
		return strdup("");
	}

	u_strToUTF8(NULL, 0, &dstlen, lex->buf_start + tok->start, srclen, &uerr);
	if (U_BUFFER_OVERFLOW_ERROR != uerr) {
		if (0 != (ret = toml2_check_uerr(lex, uerr))) {
			return NULL;
		}
	}

	char *buf = malloc(dstlen + 1);
	buf[dstlen] = 0;
	uerr = 0;

	u_strToUTF8(buf, dstlen, NULL, lex->buf_start + tok->start, srclen, &uerr);
	if (0 != (ret = toml2_check_uerr(lex, uerr))) {
		free(buf);
		return NULL;
	}

	return buf;
}
