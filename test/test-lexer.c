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
	ck_assert_str_eq("hello", toml2_token_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(squote_bs)
{
	toml2_lex_t lexer = check_init("'h\\ello'");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("h\\ello", toml2_token_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(squote_bs2)
{
	toml2_lex_t lexer = check_init("'hello\\'");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	ck_assert_str_eq("hello\\", toml2_token_utf8(&lexer, &tok));
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
	ck_assert_str_eq("hello", toml2_token_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_bs)
{
	toml2_lex_t lexer = check_init("\" \\t \\b \\n \\r \\\\ \\\" \"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	const char *utf8 = toml2_token_utf8(&lexer, &tok);
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
	ck_assert_str_eq("x\nx\nx", toml2_token_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
}
END_TEST

START_TEST(dquote_octopus)
{
	toml2_lex_t lexer = check_init("\"\\U0001F419\"");
	toml2_token_t tok = check_token(&lexer, TOML2_TOKEN_STRING);
	const char *utf8 = toml2_token_utf8(&lexer, &tok);
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
	ck_assert_str_eq("###", toml2_token_utf8(&lexer, &tok));
	check_token(&lexer, TOML2_TOKEN_EOF);
	toml2_lex_free(&lexer);
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
	};

	return tcase_build_suite("lexer", tests, sizeof(tests));
}
