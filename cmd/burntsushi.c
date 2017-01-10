#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "toml2.h"

static void
emit_escaped(const char *str)
{
	fprintf(stdout, "\"");
	for (size_t i = 0; i < strlen(str); i += 1) {
		char c = str[i];

		if ('"' == c) {
			fprintf(stdout, "\\\"");
		}
		else if ('\\' == c) {
			fprintf(stdout, "\\\\");
		}
		else {
			fprintf(stdout, "%.1s", &c);
		}
	}
	fprintf(stdout, "\"");
}

static void
emit_doc(toml2_t *doc)
{
	if (TOML2_TABLE == toml2_type(doc)) {
		fprintf(stdout, "{");
		for (size_t i = 0; i < toml2_len(doc); i += 1) {
			if (0 != i) {
				fprintf(stdout, ",");
			}

			toml2_t *subdoc = toml2_index(doc, i);
			emit_escaped(toml2_name(subdoc));
			fprintf(stdout, ":");
			emit_doc(subdoc);
		}
		fprintf(stdout, "}");
	}
	else if (TOML2_LIST == toml2_type(doc)) {
		fprintf(stdout, "{\"type\":\"array\",\"value\":[");
		for (size_t i = 0; i < toml2_len(doc); i += 1) {
			if (0 != i) {
				fprintf(stdout, ",");
			}

			toml2_t *subdoc = toml2_index(doc, i);
			emit_doc(subdoc);
		}
		fprintf(stdout, "]}");
	}
	else if (TOML2_INT == toml2_type(doc)) {
		fprintf(stdout, "{\"type\":\"integer\",\"value\":%ld}", toml2_int(doc));
	}
	else if (TOML2_FLOAT == toml2_type(doc)) {
		fprintf(stdout, "{\"type\":\"float\",\"value\":%lf}", toml2_float(doc));
	}
	else if (TOML2_STRING == toml2_type(doc)) {
		fprintf(stdout, "{\"type\":\"string\",\"value\":");
		emit_escaped(toml2_string(doc));
		fprintf(stdout, "}");
	}
	else if (TOML2_BOOL == toml2_type(doc)) {
		if (toml2_bool(doc)) {
			fprintf(stdout, "{\"type\":\"string\",\"value\":true}");
		}
		else {
			fprintf(stdout, "{\"type\":\"string\",\"value\":false}");
		}
	}
	else if (TOML2_DATE == toml2_type(doc)) {
		fprintf(stdout, "hello");
	}
	else {
		fprintf(stdout, "undefined");
	}
}

int
main(int argc, char *argv[])
{
	// This is a bit gross, but whatever.
	char data[2048];
	bzero(data, sizeof(data));

	fread(data, 1, sizeof(data) - 1, stdin);
	if (!feof(stdin)) {
		return -1;
	}

	toml2_t doc;
	toml2_init(&doc);

	int ret = toml2_parse(&doc, data, strlen(data));
	if (0 != ret) {
		return ret;
	}

	emit_doc(&doc);
	fprintf(stdout, "\n");
	return 0;
}
