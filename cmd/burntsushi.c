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
		else if ('\n' == c) {
			fprintf(stdout, "\\n");
		}
		else if ('\b' == c) {
			fprintf(stdout, "\\b");
		}
		else if ('\t' == c) {
			fprintf(stdout, "\\t");
		}
		else if ('\r' == c) {
			fprintf(stdout, "\\r");
		}
		else if ('\f' == c) {
			fprintf(stdout, "\\f");
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
		char buf[256];
		snprintf(buf, sizeof(buf), "%ld", toml2_int(doc));
		fprintf(stdout, "{\"type\":\"integer\",\"value\":\"%s\"}", buf);
	}
	else if (TOML2_FLOAT == toml2_type(doc)) {
		char buf[256];
		snprintf(buf, sizeof(buf), "%lf", toml2_float(doc));
		fprintf(stdout, "{\"type\":\"float\",\"value\":\"%s\"}", buf);
	}
	else if (TOML2_STRING == toml2_type(doc)) {
		fprintf(stdout, "{\"type\":\"string\",\"value\":");
		emit_escaped(toml2_string(doc));
		fprintf(stdout, "}");
	}
	else if (TOML2_BOOL == toml2_type(doc)) {
		if (toml2_bool(doc)) {
			fprintf(stdout, "{\"type\":\"bool\",\"value\":\"true\"}");
		}
		else {
			fprintf(stdout, "{\"type\":\"bool\",\"value\":\"false\"}");
		}
	}
	else if (TOML2_DATE == toml2_type(doc)) {
		char buf[256];
		struct tm tm = toml2_date(doc);
		snprintf(
			buf, sizeof(buf),
			"%04d-%02d-%02dT%02d:%02d:%02dZ", 
			tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec
			// XXX: tm_gmtoff.
		);

		fprintf(stdout, "{\"type\":\"datetime\", \"value\":\"%s\"}", buf);
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
		fprintf(stderr, "Error %d\n", ret);
		return ret;
	}

	emit_doc(&doc);
	fprintf(stdout, "\n");
	return 0;
}
