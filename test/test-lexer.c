#include "util.h"
#include "toml2.h"
#include "toml2-lexer.h"
#include <string.h>

static toml2_lex_t
check_init(const char *str)
{
	toml2_lex_t lexer;
	ck_assert_int_eq(0, toml2_lex_init(&lexer, str, strlen(str)));
	return lexer;
}

static toml2_token_t
check_token(toml2_lex_t *lexer, toml2_token_type_t type)
{
	toml2_token_t tok;
	ck_assert_int_eq(0, toml2_lex_token(lexer, &tok));
	ck_assert_int_eq(type, tok.type);
	return tok;
}

static void
check_token_err(toml2_lex_t *lexer, toml2_errcode_t err)
{
	toml2_token_t tok;
	ck_assert_int_eq(1, toml2_lex_token(lexer, &tok));
	ck_assert_int_eq(err, lexer->err.err);
}

START_TEST(empty)
{
	toml2_lex_t lexer = check_init("");
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(comment1)
{
	toml2_lex_t lexer = check_init("# hello");
	check_token(&lexer, TOML2_TOKEN_COMMENT);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(comment_nest)
{
	toml2_lex_t lexer = check_init("### hello");
	check_token(&lexer, TOML2_TOKEN_COMMENT);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(comment_nl)
{
	toml2_lex_t lexer = check_init("# hello\n");
	check_token(&lexer, TOML2_TOKEN_COMMENT);
	check_token(&lexer, TOML2_TOKEN_NEWLINE);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(nl_comment)
{
	toml2_lex_t lexer = check_init("\n#hello\n");
	check_token(&lexer, TOML2_TOKEN_NEWLINE);
	check_token(&lexer, TOML2_TOKEN_COMMENT);
	check_token(&lexer, TOML2_TOKEN_NEWLINE);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(squote)
{
	toml2_lex_t lexer = check_init("'hello'");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(squote_bs)
{
	toml2_lex_t lexer = check_init("'h\\ello'");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("h\\ello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(squote_bs2)
{
	toml2_lex_t lexer = check_init("'hello\\'");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello\\", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_squote_nl)
{
	toml2_lex_t lexer = check_init("'h\nello");
	check_token_err(&lexer, TOML2_UNCLOSED_SQUOTE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_squote_eof)
{
	toml2_lex_t lexer = check_init("'hello");
	check_token_err(&lexer, TOML2_UNCLOSED_SQUOTE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote)
{
	toml2_lex_t lexer = check_init("\"hello\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_bs)
{
	toml2_lex_t lexer = check_init("\" \\t \\b \\n \\r \\\\ \\\" \"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	const char *utf8 = toml2_token_dbg_utf8(&lexer, &tok);
	const char *expected = " \t \b \n \r \\ \" ";
	ck_assert_int_eq(strlen(expected), strlen(utf8));
	ck_assert_str_eq(expected, utf8);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_dquote_bs)
{
	toml2_lex_t lexer = check_init("\"\\'\"");
	check_token_err(&lexer, TOML2_INVALID_ESCAPE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_u)
{
	toml2_lex_t lexer = check_init("\"x\\u000Ax\\u000ax\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("x\nx\nx", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_octopus)
{
	toml2_lex_t lexer = check_init("\"\\U0001F419\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	const char *utf8 = toml2_token_dbg_utf8(&lexer, &tok);
	const char *expected = "\xF0\x9F\x90\x99";
	ck_assert_int_eq(strlen(expected), strlen(utf8));
	ck_assert_str_eq(expected, utf8);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_dquote_u_bad)
{
	toml2_lex_t lexer = check_init("\"\\uabcq\"");
	check_token_err(&lexer, TOML2_INVALID_ESCAPE);
	toml2_lex_free(&lexer);

	lexer = check_init("\"\\U0000abcq\"");
	check_token_err(&lexer, TOML2_INVALID_ESCAPE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_dquote_u_eof)
{
	toml2_lex_t lexer = check_init("\"\\uabc\"");
	check_token_err(&lexer, TOML2_INVALID_ESCAPE);
	toml2_lex_free(&lexer);

	lexer = check_init("\"\\U00abc\"");
	check_token_err(&lexer, TOML2_INVALID_ESCAPE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_comment)
{
	toml2_lex_t lexer = check_init("\"###\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("###", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tdquote)
{
	toml2_lex_t lexer = check_init("\"\"\"hello\"\"\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tdquote_nl)
{
	toml2_lex_t lexer = check_init("\"\"\"\nhello\"\"\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tdquote_bsnl)
{
	toml2_lex_t lexer = check_init("\"\"\"\\\nhello\"\"\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tdquote_nlnl)
{
	toml2_lex_t lexer = check_init("\"\"\"\n \thello\\\n \tworld\"\"\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("helloworld", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tdquote_bs)
{
	toml2_lex_t lexer = check_init("\"\"\"\\\"\\n\\t\"\"\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("\"\n\t", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_tdquote_eof)
{
	toml2_lex_t lexer = check_init("\"\"\"foo\"\"");
	check_token_err(&lexer, TOML2_UNCLOSED_TDQUOTE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tsquote)
{
	toml2_lex_t lexer = check_init("'''hello'''");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tsquote_nl)
{
	toml2_lex_t lexer = check_init("'''\nhello'''");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tsquote_bsnl)
{
	toml2_lex_t lexer = check_init("'''\\\nhello'''");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tsquote_nlnl)
{
	toml2_lex_t lexer = check_init("'''\nhello \n world'''");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello \n world", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(tsquote_bs)
{
	toml2_lex_t lexer = check_init("'''\\n\\t'''");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("\\n\\t", toml2_token_dbg_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_tsquote_eof)
{
	toml2_lex_t lexer = check_init("'''foo''");
	check_token_err(&lexer, TOML2_UNCLOSED_TSQUOTE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(ival)
{
	toml2_lex_t lexer = check_init("42");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_INT);
	ck_assert_int_eq(42, tok.ival);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(ival_plus)
{
	toml2_lex_t lexer = check_init("+42");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_INT);
	ck_assert_int_eq(42, tok.ival);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(ival_neg)
{
	toml2_lex_t lexer = check_init("-42");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_INT);
	ck_assert_int_eq(-42, tok.ival);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(ival_us)
{
	toml2_lex_t lexer = check_init("4_2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_INT);
	ck_assert_int_eq(42, tok.ival);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(ival_space_nl)
{
	toml2_lex_t lexer = check_init("42 \n");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_INT);
	ck_assert_int_eq(42, tok.ival);
	check_token(&lexer, TOML2_TOKEN_NEWLINE);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_ival_us)
{
	toml2_lex_t lexer = check_init("4__2");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_ival_last_us)
{
	toml2_lex_t lexer = check_init("42_");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_ival_neg)
{
	toml2_lex_t lexer = check_init("4-2");
	check_token_err(&lexer, TOML2_INVALID_INT);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_ival_neg2)
{
	toml2_lex_t lexer = check_init("--42");
	check_token_err(&lexer, TOML2_INVALID_DATE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval)
{
	toml2_lex_t lexer = check_init("42.");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(42, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_us)
{
	toml2_lex_t lexer = check_init("4_2.");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(42, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_e)
{
	toml2_lex_t lexer = check_init("4e2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(4e2, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_e2)
{
	toml2_lex_t lexer = check_init("4E2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(4e2, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_d)
{
	toml2_lex_t lexer = check_init("4.2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(4.2, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_de)
{
	toml2_lex_t lexer = check_init("4.2e2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(420, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_neg)
{
	toml2_lex_t lexer = check_init("-4.2");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(-4.2, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(fval_neg_e)
{
	toml2_lex_t lexer = check_init("40e-1");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DOUBLE);
	ck_assert_double_eq(4, tok.fval);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_neg_f)
{
	toml2_lex_t lexer = check_init("4.-2");
	check_token_err(&lexer, TOML2_INVALID_DATE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_us)
{
	toml2_lex_t lexer = check_init("4__2.");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_last_us)
{
	toml2_lex_t lexer = check_init("4.2_");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_mid_us)
{
	toml2_lex_t lexer = check_init("4_.2");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_mid_us2)
{
	toml2_lex_t lexer = check_init("4._2");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_end_us)
{
	toml2_lex_t lexer = check_init("4.2_");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_e_us)
{
	toml2_lex_t lexer = check_init("4_e1");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_e_us2)
{
	toml2_lex_t lexer = check_init("4e_1");
	check_token_err(&lexer, TOML2_INVALID_UNDERSCORE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_ee)
{
	toml2_lex_t lexer = check_init("4ee2");
	check_token_err(&lexer, TOML2_INVALID_DOUBLE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_fval_neg2)
{
	toml2_lex_t lexer = check_init("--4.");
	check_token_err(&lexer, TOML2_INVALID_DATE);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(date)
{
	toml2_lex_t lexer = check_init("1928-01-02T12:04:06-08:12");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DATE);
	ck_assert_int_eq(1928, tok.time.tm_year);
	ck_assert_int_eq(0, tok.time.tm_mon);
	ck_assert_int_eq(2, tok.time.tm_mday);
	ck_assert_int_eq(12, tok.time.tm_hour);
	ck_assert_int_eq(4, tok.time.tm_min);
	ck_assert_int_eq(6, tok.time.tm_sec);
	ck_assert_int_eq(-1 * (8 * 60 * 60 + 12), tok.time.tm_gmtoff);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(date2)
{
	toml2_lex_t lexer = check_init("2001-02-03t04:05:06.789Z");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DATE);
	ck_assert_int_eq(2001, tok.time.tm_year);
	ck_assert_int_eq(1, tok.time.tm_mon);
	ck_assert_int_eq(3, tok.time.tm_mday);
	ck_assert_int_eq(4, tok.time.tm_hour);
	ck_assert_int_eq(5, tok.time.tm_min);
	ck_assert_int_eq(6, tok.time.tm_sec);
	ck_assert_int_eq(0, tok.time.tm_gmtoff);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(date_short)
{
	toml2_lex_t lexer = check_init("2001-02-03");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_DATE);
	ck_assert_int_eq(2001, tok.time.tm_year);
	ck_assert_int_eq(1, tok.time.tm_mon);
	ck_assert_int_eq(3, tok.time.tm_mday);
	ck_assert_int_eq(0, tok.time.tm_hour);
	ck_assert_int_eq(0, tok.time.tm_min);
	ck_assert_int_eq(0, tok.time.tm_sec);
	ck_assert_int_eq(0, tok.time.tm_gmtoff);
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(err_date_short)
{
	const char *tests[] = {
		"2001-",
		"2001-02",
		"2001-02-",
		"2001-02-03T",
		"2001-02-03T05",
		"2001-02-03T05:",
		"2001-02-03T05:06",
		"2001-02-03T05:06:",
		"2001-02-03T05:06:07",
		"2001-02-03T05:06:07.",
		"2001-02-03T05:06:07T",
		"2001-02-03T05:06:07T08",
		"2001-02-03T05:06:07T08:",
		"201-02-03T04:05:06Z",
		"2001-2-03T04:05:06Z",
		"2001-02-3T04:05:06Z",
		"2001-02-03T4:05:06Z",
		"2001-02-03T04:5:06Z",
		"2001-02-03T04:05:6Z",
		"2001-02-03T04:05:06T7:08",
		"2001-02-03T04:05:06T07:8",
	};
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i += 1) {
		toml2_lex_t lexer = check_init(tests[i]);
		toml2_token_t tok;

		if (1 != toml2_lex_token(&lexer, &tok)) {
			ck_assert_msg("Assertion: '%s' should fail to lex", tests[i]);
		}
		if (TOML2_INVALID_DATE != lexer.err.err) {
			ck_assert_msg(
				"Assertion: '%s' should return TOML2_INVALID_DATE (got %d)",
				tests[i],
				lexer.err.err
			);
		}

		toml2_lex_free(&lexer);
	}
}
END_TEST

Suite*
suite_lexer()
{
	tcase_t tests[] = {
		{ "empty",            &empty            },
		{ "comment1",         &comment1         },
		{ "comment_nest",     &comment_nest     },
		{ "comment_nl",       &comment_nl       },
		{ "nl_comment",       &nl_comment       },
		{ "squote",           &squote           },
		{ "squote_bs",        &squote_bs        },
		{ "squote_bs2",       &squote_bs2       },
		{ "err_squote_nl",    &err_squote_nl    },
		{ "err_squote_eof",   &err_squote_eof   },
		{ "dquote",           &dquote           },
		{ "dquote_bs",        &dquote_bs        },
		{ "err_dquote_bs",    &err_dquote_bs    },
		{ "dquote_u",         &dquote_u         },
		{ "dquote_octopus",   &dquote_octopus   },
		{ "err_dquote_u_bad", &err_dquote_u_bad },
		{ "err_dquote_u_eof", &err_dquote_u_eof },
		{ "dquote_comment",   &dquote_comment   },
		{ "tdquote",          &tdquote          },
		{ "tdquote_nl",       &tdquote_nl       },
		{ "tdquote_bsnl",     &tdquote_bsnl     },
		{ "tdquote_nlnl",     &tdquote_nlnl     },
		{ "tdquote_bs",       &tdquote_bs       },
		{ "err_tdquote_eof",  &err_tdquote_eof  },
		{ "tsquote",          &tsquote          },
		{ "tsquote_nl",       &tsquote_nl       },
		{ "tsquote_bsnl",     &tsquote_bsnl     },
		{ "tsquote_nlnl",     &tsquote_nlnl     },
		{ "tsquote_bs",       &tsquote_bs       },
		{ "err_tsquote_eof",  &err_tsquote_eof  },
		{ "ival",             &ival             },
		{ "ival_plus",        &ival_plus        },
		{ "ival_neg",         &ival_neg         },
		{ "ival_us",          &ival_us          },
		{ "ival_space_nl",    &ival_space_nl    },
		{ "err_ival_us",      &err_ival_us      },
		{ "err_ival_last_us", &err_ival_last_us },
		{ "err_ival_neg2",    &err_ival_neg2    },
		{ "fval",             &fval             },
		{ "fval_us",          &fval_us          },
		{ "fval_e",           &fval_e           },
		{ "fval_e2",          &fval_e2          },
		{ "fval_d",           &fval_d           },
		{ "fval_de",          &fval_de          },
		{ "fval_neg",         &fval_neg         },
		{ "fval_neg_e",       &fval_neg_e       },
		{ "err_fval_neg_f",   &err_fval_neg_f   },
		{ "err_fval_us",      &err_fval_us      },
		{ "err_fval_last_us", &err_fval_last_us },
		{ "err_fval_mid_us",  &err_fval_mid_us  },
		{ "err_fval_mid_us2", &err_fval_mid_us2 },
		{ "err_fval_end_us",  &err_fval_end_us  },
		{ "err_fval_e_us",    &err_fval_e_us    },
		{ "err_fval_e_us2",   &err_fval_e_us2   },
		{ "err_fval_ee",      &err_fval_ee      },
		{ "err_fval_neg2",    &err_fval_neg2    },
		{ "date",             &date             },
		{ "date2",            &date2            },
		{ "date_short",       &date_short       },
		{ "err_date_short",   &err_date_short   },
	};

	return tcase_build_suite("lexer", tests, sizeof(tests));
}
