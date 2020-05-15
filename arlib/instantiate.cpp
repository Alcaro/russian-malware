#define COMMON_INST(T) template class T
#include "string.h"
#include "array.h"
#include "set.h"
template class array<cstring>; // placed here since string.h includes array.h, so array.h can't instantiate this by itself
template class array<string>;
