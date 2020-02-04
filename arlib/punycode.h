#pragma once
#include "string.h"

//If input is invalid, output will be blank, or contain U+FFFD.
string puny_decode_label(cstring puny);
string puny_decode(cstring domain);
