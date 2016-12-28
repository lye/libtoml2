#include "toml2.h"
#include <strings.h>

void
toml2_init(toml2_t *doc)
{
	bzero(doc, sizeof(toml2_t));
}
