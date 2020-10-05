#pragma once

#include "global.h"
#include "array.h"
#include "string.h"

inline size_t base64_dec_len(size_t len) { return (len+3)/4*3; }
inline size_t base64_enc_len(size_t len) { return (len+2)/3*4; }

//if the entire buffer was successfully processed, returns number of bytes written; if not, returns 0
//'out' must be at least base64_dec_len(text.length()) bytes; otherwise, undefined behavior
//accepts standard base64, as well as base64url; ignores all \t\n\r and space in input; padding is mandatory
//may return less than the expected number of bytes, if it contains padding or whitespace
size_t base64_dec_raw(arrayvieww<uint8_t> out, cstring text);
//returns blank array if input is invalid
array<uint8_t> base64_dec(cstring text);

//will write exactly base64_enc_len(bytes.size()) bytes
//'out' must either be identical to, or not overlap, 'bytes'
//always succeeds
void base64_enc_raw(arrayvieww<uint8_t> out, arrayview<uint8_t> bytes);
string base64_enc(arrayview<uint8_t> bytes);
