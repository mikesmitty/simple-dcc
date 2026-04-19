#include "lcc/lcc_frame.h"

/*
 * All frame helpers are `static inline` in lcc_frame.h. This TU exists as a
 * linkable anchor — non-trivial helpers (e.g. an addressed-message payload
 * iterator) will move here as the stack grows.
 */
