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

Suite*
suite_lexer()
{
	tcase_t tests[] = {
		{ "empty",        &empty        },
		{ "comment1",     &comment1     },
		{ "comment_nest", &comment_nest },
		{ "comment_nl",   &comment_nl   },
		{ "nl_comment",   &nl_comment   },
	};

	return tcase_build_suite("lexer", tests, sizeof(tests));
}
