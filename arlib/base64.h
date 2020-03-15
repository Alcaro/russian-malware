#pragma once

#include "global.h"
#include "array.h"
#include "string.h"

//decoded length may be smaller, if input contains whitespace or padding
inline size_t base64_dec_len(size_t len) { return (len+3)/4*3; }
inline size_t base64_enc_len(size_t len) { return (len+2)/3*4; }

//if the entire buffer was successfully processed, returns number of bytes written; if not, returns 0
//in both cases, outend contains a partially parsed object, and textend points to the first invalid character
// (or past end of text, if the entire thing was parsed)
//'out' must be at least base64_dec_len(text.length()) bytes; otherwise, undefined behavior
//'out' may overlap 'text' only if 'out' starts at least 8 bytes before 'text'
size_t base64_dec_raw(arrayvieww<uint8_t> out, size_t* outend, cstring text, size_t* textend);
//returns blank array if not fully valid
array<uint8_t> base64_dec(cstring text);

//'out' must be at least base64_enc_len(text.length()) bytes; otherwise, undefined behavior
//'out' may not overlap 'text'
//can never fail
void base64_enc_raw(arrayvieww<uint8_t> out, arrayview<uint8_t> bytes);
string base64_enc(arrayview<uint8_t> bytes);
