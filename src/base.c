#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

// 3rd party dependencies
#include <mongoose.h>
#include <zlib.h>
#include <jansson.h>

#if JANSSON_MAJOR_VERSION != 2 || JANSSON_MINOR_VERSION != 13 || \
    JANSSON_MICRO_VERSION != 1
#warning "You are not building against jansson version 2.13.1. May bob be with you"
#endif

#if ZLIB_VERNUM != 0x1310
#warning "You are not building against zlib version 1.3.1. May bob be with you"
#endif
