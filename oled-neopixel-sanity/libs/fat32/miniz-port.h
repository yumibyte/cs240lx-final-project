#pragma once

void *kmalloc(unsigned nbytes);

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_INFLATE_APIS
#define MZ_MALLOC(x) kmalloc(x)
#define MZ_FREE(x) ((void)(x))
#define MZ_REALLOC(p, x) kmalloc(x)
