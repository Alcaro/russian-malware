#include "global.h"

// Returns length of the given JS v5.1 identifier name, in bytes. If not legal at all, returns zero. Supports \u escapes.
// Invalid UTF-8 is not part of a valid identifier. Input must be NUL terminated; the NUL is not part of the identifier.
// (JS is defined in terms of UTF-16, or UCS-2 in v5.1, but this function uses UTF-8 because of course it does.)
size_t js_identifier_name(const uint8_t * start);
