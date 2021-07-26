#include "image.h"
#include "bytestream.h"
#include "crc32.h"
#include "simd.h"
#include "deflate.h"

bool oimage::init_decode_png(arrayview<uint8_t> pngdata)
{
	struct pngchunk : public bytestream {
		uint32_t type;
		uint32_t len;
	};
	class pngreader : public bytestream {
	public:
		pngreader(arrayview<uint8_t> bytes) : bytestream(bytes) {}
		
		pngchunk chunk_raw()
		{
			pngchunk ret;
			ret.type = 0; // invalid value, looks like an unrecognized critical chunk and will propagate outwards
			
			if (remaining() < 12) return ret;
			ret.len = u32b();
			
			if (ret.len >= 0x80000000 || remaining()-8 < ret.len) return ret;
			
			uint32_t crc32_actual = crc32(peekbytes(4+ret.len));
			
			ret.type = u32b();
			ret.init(bytes(ret.len));
			
			uint32_t crc32_exp = u32b();
			if (crc32_actual != crc32_exp) ret.type = 0;
			
			return ret;
		}
		
		//Returns the next recognized or critical chunk. (Only tRNS is recognized.)
		pngchunk chunk()
		{
			while (true)
			{
				pngchunk ret = chunk_raw();
				if (ret.type == 0x74524E53) return ret; // tRNS
				if (!(ret.type&0x20000000)) return ret; // critical
			}
		}
	};
	
	
	pngreader reader(pngdata);
	
	if (reader.remaining() < 8 || !reader.signature("\x89PNG\r\n\x1A\n"))
	{
	fail: // putting this up here to avoid 99999 'jump to label foo crosses initialization of bar' errors
		this->storage = NULL; // forcibly deallocate - this can leave this->pixels as a dead pointer, but caller won't use that.
		return false;
	}
	
	pngchunk IHDR = reader.chunk_raw(); // no ancillary chunks allowed before IHDR
	if (IHDR.type != 0x49484452) goto fail; // IHDR must be the first chunk
	if (IHDR.len != 13) goto fail; // the IHDR chunk is always 13 bytes
	
	uint32_t width  = this->width  = IHDR.u32b();
	uint32_t height = this->height = IHDR.u32b();
	
	// Greyscale             - 0
	// Truecolour            - 2
	// Indexed-colour        - 3
	// Greyscale with alpha  - 4
	// Truecolour with alpha - 6
	
	uint8_t bits_per_sample = IHDR.u8();
	uint8_t color_type = IHDR.u8();
	uint8_t comp_meth = IHDR.u8();
	uint8_t filter_meth = IHDR.u8();
	uint8_t interlace_meth = IHDR.u8();
	
	if (width == 0 || height == 0 || width >= 0x80000000 || height >= 0x80000000) goto fail;
	if ((uint64_t)width * height >= 0x80000000 / sizeof(uint32_t)) goto fail; // ensure width*height doesn't overflow
	// huge images will still OOM, but at least they won't corrupt memory
	if (bits_per_sample >= 32 || color_type > 6 || comp_meth != 0 || filter_meth != 0 || interlace_meth > 1) goto fail;
	if (bits_per_sample > 8) goto fail; // bpp=16 is allowed by the png standard, but not by this program
	if (interlace_meth) goto fail; // TODO: implement this
	static const uint32_t bpp_allowed[7] = { 0x68808000, 0x00000000, 0x00808000, 0x68800000, 0x00808000, 0x00000000, 0x00808000 };
	if (!((bpp_allowed[color_type]<<bits_per_sample)&0x80000000)) goto fail; // clear bit - invalid bpp (this also rejects types 1 and 5)
	
	uint32_t palette[256];
	unsigned palettelen = 0;
	
	if (color_type == 3)
	{
		pngchunk PLTE = reader.chunk();
		if (PLTE.type != 0x504C5445) goto fail;
		
		if (PLTE.len == 0 || PLTE.len%3 || PLTE.len/3 > (1u<<bits_per_sample)) goto fail;
		palettelen = PLTE.len/3;
		for (unsigned i=0;i<palettelen;i++)
		{
			// this reads a byte it shouldn't, but it's either previous color or the chunk's type, so not UB
			palette[i] = 0xFF000000 | readu_be32(PLTE.bytes(3).ptr()-1);
		}
	}
	
	bool has_alpha = (color_type >= 4);
	bool has_bool_alpha = false;
	uint32_t transparent = 0; // if you see this value (after filtering), make it transparent
	                          // for rgb, this value is ARGB, A=0xFF
	
	pngchunk tRNS_IDAT = reader.chunk();
	if (tRNS_IDAT.type == 0x504C5445) // PLTE
	{
		if (color_type == 2 || color_type == 6) // it's allowed on those two types
			tRNS_IDAT = reader.chunk();
		//else fall through and let IEND handler whine
	}
	pngchunk IDAT;
	if (!has_alpha && tRNS_IDAT.type == 0x74524E53) // tRNS
	{
		pngchunk tRNS = tRNS_IDAT;
		has_alpha = true;
		
		if (color_type == 0)
		{
			if (tRNS.len != 2) goto fail;
			transparent = tRNS.u16b();
			if (transparent >= 1u<<bits_per_sample) goto fail;
			if (bits_per_sample == 8) transparent = 0xFF000000 + transparent*0x010101;
			has_bool_alpha = true;
		}
		if (color_type == 2)
		{
			if (tRNS.len != 6) goto fail;
			
			uint16_t r = tRNS.u16b();
			uint16_t g = tRNS.u16b();
			uint16_t b = tRNS.u16b();
			
			if ((r|g|b) >= 1<<bits_per_sample) goto fail;
			
			transparent = 0xFF000000 | r<<16 | g<<8 | b;
			has_bool_alpha = true;
		}
		if (color_type == 3)
		{
			if (tRNS.len == 0 || tRNS.len > palettelen) goto fail;
			
			has_bool_alpha = true;
			for (size_t i=0;i<tRNS.len;i++)
			{
				uint8_t newa = tRNS.u8();
				if (newa != 0x00 && newa != 0xFF) has_bool_alpha = false;
				palette[i] = (palette[i]&0x00FFFFFF) | (uint32_t)newa<<24; // extra cast because u8 << 24 is signed
			}
		}
		if (color_type == 4) goto fail;
		if (color_type == 6) goto fail;
		
		IDAT = reader.chunk();
	}
	else IDAT = tRNS_IDAT; // could be neither tRNS or IDAT, in which case the while loop won't enter, and the IEND checker will whine
	
	
	this->stride = sizeof(uint32_t) * width;
	
	int samples_per_px = (0x04021301 >> (color_type*4))&15;
	size_t bytes_per_line_raw = (bits_per_sample*samples_per_px*width + 7)/8;
	
	//+4 and +2 to let filters read outside the image, and to ensure there's enough
	// space to decode and convert the image to the desired output format
	//+7*u32 because the decoder overshoots on low bitdepths
	//unpacking (png -> uint32_t) needs one pixel of buffer, though compiler considerations say minimum one scanline
	//unfiltering needs another scanline of buffer, plus another one must exist in front
	//
	//however, there's another another issue - what scanline size?
	// output must be packed, raw is pngpack+1, unfiltering needs at least pngpack+4
	//the correct offsets are
	//scan_out = width * (3 or 4)
	//scan_packed = whatever+4
	//scan_filtered = whatever+1
	//start_out = 0
	//start_packed = end - height*scan_packed - scan_packed
	//start_filtered = end - height*scan_filtered
	//deinterleaving probably isn't doable in-place at all
	size_t nbytes = max(this->stride, bytes_per_line_raw+4) * (height+2) + sizeof(uint32_t)*7;
	this->storage = xmalloc(nbytes);
	
	this->pixels8 = (uint8_t*)this->storage;
	this->fmt = (has_alpha ? (has_bool_alpha ? ifmt_bargb8888 : ifmt_argb8888) : ifmt_xrgb8888);
	
	uint8_t* inflate_end = (uint8_t*)this->storage + nbytes;
	uint8_t* inflate_start = inflate_end - (bytes_per_line_raw+1)*height;
	
	inflator::zlibhead infl;
	infl.set_output_first(bytesw(inflate_start, inflate_end-inflate_start));
	inflator::ret_t infl_status = inflator::ret_more_input;
	
	while (IDAT.type == 0x49444154 && infl_status == inflator::ret_more_input)
	{
		infl.set_input(IDAT.bytes(IDAT.len), false);
		infl_status = infl.inflate();
		IDAT = reader.chunk_raw(); // all IDAT chunks must be consecutive
	}
	
	if (infl_status == inflator::ret_more_input)
	{
		infl.set_input(NULL, true);
		infl_status = infl.inflate();
	}
	if (infl_status != inflator::ret_done) goto fail;
	if (inflate_start+infl.output_in_last() != inflate_end) goto fail;
	
	pngchunk IEND = IDAT;
	if (IEND.type != 0x49454E44) IEND = reader.chunk(); // ancillary between IDAT and IEND is fine, just discard that
	if (IEND.type != 0x49454E44 || IEND.len != 0) goto fail;
	
	
	if (interlace_meth == 1)
	{
puts("unsupported: Adam7");
goto fail;
	}
	
//for(uint8_t*n=inflate_start;n<inflate_at;n++)printf("%.2X ",*n);
//puts("");
	
	size_t defilter_out_line = 4 + bytes_per_line_raw;
	uint8_t* defilter_start = inflate_end - height*defilter_out_line - defilter_out_line;
	
	if (inflate_start[0] >= 2)
		memset(defilter_start - defilter_out_line, 0, defilter_out_line); // y=-1 is all zeroes
	size_t filter_bpp = (bits_per_sample*samples_per_px + 7)/8;
	if (!image::png_defilter(defilter_start, defilter_out_line, inflate_start, filter_bpp, bytes_per_line_raw, height)) goto fail;
	
	defilter_start += 4; // skip the x=-1 = 0 padding
	
	if(0);
	else if (color_type == 0)
	{
		//treating gray as a palette makes things a fair bit simpler, especially with tRNS handling
		//doing it for bpp=8 means an 1KB table which is boring, but that one doesn't pack multiple pixels in a single byte,
		// so it's easy to put in the non-palette decoder (and there's no other sane way to handle gray+alpha)
		if (bits_per_sample == 1)
		{
			static const uint32_t gray_1bpp[] = { 0xFF000000, 0xFFFFFFFF };
			if (has_alpha) transparent = gray_1bpp[transparent];
			png_unpack_plte<1>(defilter_start, defilter_out_line, gray_1bpp, 2);
		}
		else if (bits_per_sample == 2)
		{
			static const uint32_t gray_2bpp[] = { 0xFF000000, 0xFF555555, 0xFFAAAAAA, 0xFFFFFFFF };
			if (has_alpha) transparent = gray_2bpp[transparent];
			png_unpack_plte<2>(defilter_start, defilter_out_line, gray_2bpp, 4);
		}
		else if (bits_per_sample == 4)
		{
			static const uint32_t gray_4bpp[] = { 0xFF000000, 0xFF111111, 0xFF222222, 0xFF333333,
			                                      0xFF444444, 0xFF555555, 0xFF666666, 0xFF777777,
			                                      0xFF888888, 0xFF999999, 0xFFAAAAAA, 0xFFBBBBBB,
			                                      0xFFCCCCCC, 0xFFDDDDDD, 0xFFEEEEEE, 0xFFFFFFFF };
			if (has_alpha) transparent = gray_4bpp[transparent];
			png_unpack_plte<4>(defilter_start, defilter_out_line, gray_4bpp, 16);
		}
		else /* bits_per_sample == 8 */ png_unpack_rgb<0>(defilter_start, defilter_out_line);
	}
	
	else if (color_type == 2 /* bits_per_sample == 8 */) png_unpack_rgb<2>(defilter_start, defilter_out_line);
	else if (color_type == 3)
	{
		if      (bits_per_sample == 1) { if (!png_unpack_plte<1>(defilter_start, defilter_out_line, palette, palettelen)) goto fail; }
		else if (bits_per_sample == 2) { if (!png_unpack_plte<2>(defilter_start, defilter_out_line, palette, palettelen)) goto fail; }
		else if (bits_per_sample == 4) { if (!png_unpack_plte<4>(defilter_start, defilter_out_line, palette, palettelen)) goto fail; }
		else /* bits_per_sample = 8 */ { if (!png_unpack_plte<8>(defilter_start, defilter_out_line, palette, palettelen)) goto fail; }
	}
	else if (color_type == 4 /* bits_per_sample == 8 */) png_unpack_rgb<4>(defilter_start, defilter_out_line);
	else  /* color_type == 6 && bits_per_sample == 8 */  png_unpack_rgb<6>(defilter_start, defilter_out_line);
	
	if (transparent >= 0xFF000000)
	{
		uint32_t* pixels = (uint32_t*)(uint8_t*)this->storage;
		for (size_t i=0;i<width*height;i++)
		{
			if (pixels[i] == transparent) pixels[i] &= 0x00FFFFFF;
		}
	}
	
	return true;
}

// Requires that out[-out_stride-bytes_per_pixel .. -1] (inclusive) exists and is zeroes, unless in[i] is known to be 0 or 1, not 2-4.
// Useful output data will start 4 bytes after 'out'.
bool image::png_defilter(uint8_t * out, size_t out_stride, const uint8_t * in, int bytes_per_pixel, size_t width_bytes, size_t height)
{
	for (size_t y=0;y<height;y++)
	{
		const uint8_t * defilter_in = in + y*(width_bytes+1) + 1;
		uint8_t filter_type = defilter_in[-1]; // cache this, in case input and output ends at same place
		
		uint8_t * defilter_out = out + y*out_stride;
		*(defilter_out++) = 0; // x=-1 is all zeroes (always use 4, not width_bytes, makes the math easier)
		*(defilter_out++) = 0;
		*(defilter_out++) = 0;
		*(defilter_out++) = 0;
		
		memmove(defilter_out, defilter_in, width_bytes);
		switch (filter_type)
		{
		case 0: // None
			break;
		
		case 1: // Sub
			for (size_t x=0;x<width_bytes;x++)
				defilter_out[x] += defilter_out[x-bytes_per_pixel];
			break;
		
		case 2: // Up
			for (size_t x=0;x<width_bytes;x++)
				defilter_out[x] += defilter_out[x-out_stride];
			break;
		
		case 3: // Average
			for (size_t x=0;x<width_bytes;x++)
			{
				int a = defilter_out[x-bytes_per_pixel];
				int b = defilter_out[x-out_stride];
				defilter_out[x] += (a+b)/2;
			}
			break;
		
		case 4: // Paeth
		{
#ifdef __SSE2__
#ifndef __SSSE3__
			auto _mm_abs_epi16 = [](__m128i a) -> __m128i {
				// abs(x) = (x^(x<0)) - (x<0)
				// however, that's four insns (three obvious, plus have to copy the argument)
				// instead, this one is three (or two, if first call reuses a zeroed reg from the a/b/c calculation)
				// abs(x) = max_i16(x, -x) (or min_u16(x, -x))
				return _mm_max_epi16(a, _mm_sub_epi16(_mm_setzero_si128(), a)); 
			};
#endif
			// ignore simding the other filters; Paeth is the most common, and also the most expensive per pixel
			// it can only be SIMDed to a degree of bytes_per_pixel, so ignore bytes_per_pixel=1 (and =2, gray+alpha, too rare)
			// (it's possible to interlace processing of two consecutive Paeth scanlines by having the upper one 1px ahead of the
			//    one below, but the required data structures would be quite annoying, and combining the bytes would be annoying too)
			// (it'd also be possible to assume the previous pixel was predicted Up aka B and discard the calculation if wrong,
			//    but it's 84.6% Left/A, 13% Up/B, 2.4% Diag/C, so such a thing would be wrong most of the time and not accomplish much)
			// macro instead of function is 7% faster
#define SIMD_PAETH(bytes_per_pixel) do { \
					uint32_t a32 = *(uint32_t*)(defilter_out+x           -bytes_per_pixel);          \
					uint32_t b32 = *(uint32_t*)(defilter_out+x-out_stride                );          \
					uint32_t c32 = *(uint32_t*)(defilter_out+x-out_stride-bytes_per_pixel);          \
					__m128i a = _mm_unpacklo_epi8(_mm_cvtsi32_si128(a32), _mm_setzero_si128());      \
					__m128i b = _mm_unpacklo_epi8(_mm_cvtsi32_si128(b32), _mm_setzero_si128());      \
					__m128i c = _mm_unpacklo_epi8(_mm_cvtsi32_si128(c32), _mm_setzero_si128());      \
					                                                                                 \
					__m128i p = _mm_sub_epi16(_mm_add_epi16(a, b), c);                               \
					__m128i pa = _mm_abs_epi16(_mm_sub_epi16(p, a));                                 \
					__m128i pb = _mm_abs_epi16(_mm_sub_epi16(p, b));                                 \
					__m128i pc = _mm_abs_epi16(_mm_sub_epi16(p, c));                                 \
					                                                                                 \
					/* 0 to use pa, -1 for something else */                                         \
					__m128i pa_skip = _mm_cmplt_epi16(_mm_min_epi16(pb, pc), pa);                    \
					/* -1 to use pc, 0 for something else */                                         \
					__m128i pc_use = _mm_and_si128(pa_skip, _mm_cmplt_epi16(pc, pb));                \
					__m128i pb_use = _mm_andnot_si128(pc_use, pa_skip);                              \
					                                                                                 \
					__m128i prediction = _mm_or_si128(_mm_andnot_si128(pa_skip, a),                  \
					                     _mm_or_si128(_mm_and_si128(   pb_use, b),                   \
					                                  _mm_and_si128(   pc_use, c)));                 \
					                                                                                 \
					uint32_t* outp = (uint32_t*)(defilter_out+x);                                    \
					__m128i out = _mm_add_epi8(_mm_cvtsi32_si128(*outp),                             \
					                           _mm_packus_epi16(prediction, _mm_undefined_si128())); \
					uint32_t out32 = _mm_cvtsi128_si32(out);                                         \
					if (bytes_per_pixel == 3)                                                        \
						out32 = (out32&0xFFFFFF00) | (*outp&0x000000FF);                             \
					*outp = out32;                                                                   \
				} while(0)
			if (bytes_per_pixel == 3)
			{
				//one pixel at the time, no need for a scalar tail loop
				//shift one byte to the left, so we don't overflow the malloc at the last scanline (x=-1 is safe, it's all zero)
				//we don't actually change that byte, but it's UB anyways
				defilter_out--;
				for (size_t x=0;x<width_bytes;x+=3)
				{
					SIMD_PAETH(3);
				}
				break;
			}
			if (bytes_per_pixel == 4)
			{
				for (size_t x=0;x<width_bytes;x+=4)
				{
					SIMD_PAETH(4);
				}
				break;
			}
			
			// not SIMD_LOOP_TAIL, the below runs for bytes_per_pixel=1 aka palette
#endif
			for (size_t x=0;x<width_bytes;x++)
			{
				int a = defilter_out[x-bytes_per_pixel];
				int b = defilter_out[x-out_stride];
				int c = defilter_out[x-out_stride-bytes_per_pixel];
				
				int p = a+b-c;
				int pa = abs(p-a);
				int pb = abs(p-b);
				int pc = abs(p-c);
				
				int prediction;
				if (pa <= pb && pa <= pc) prediction = a;
				else if (pb <= pc) prediction = b;
				else prediction = c;
				
				defilter_out[x] += prediction;
			}
		}
		break;
		
		default:
			return false;
		}
	}
	return true;
}

// Returns false if it uses anything outside the palette.
// Will always unpack a complete number of bytes, which may yield up to 7 pixels of overflow after each scanline.
// (The overflow will usually be overwritten by the next scanline, though the last scanline requires some buffer space.)
template<int bpp_in>
bool image::png_unpack_plte(const uint8_t * source, size_t srcstride, const uint32_t * palette, uint32_t pallen) const
{
	uint32_t * pixels = this->pixels32;
	uint32_t width = this->width;
	uint32_t height = this->height;
	
	uint32_t nbytes = ((uint64_t)width*bpp_in+7)/8;
	
#ifdef __SSE2__
	__m128i bpp1_bit0 = _mm_set1_epi32(palette[0]); // hoist those (gcc won't automatically, can't prove height/nbytes != 0)
	__m128i bpp1_bitxor = _mm_set1_epi32(palette[0]^palette[1]);
	uint64_t bpp1_fanout_magic = 0x8040201008040201; // no clue know why this isn't hoisted though
#endif
	uint8_t bpp1_max_bits = 0;
	
	for (uint32_t y=0;y<height;y++)
	{
		//in this function, all pixels have one sample per pixel
		
		//this will write 7 pixels out of bounds for bpp=1 and size=int*8+1; this will be absorbed by our overallocations
		//TODO: test on 1*1, 1*2, 1*7, 1*8, 1*9, 1*1023, 1*1024 and 1*1025, bpp=1
		for (uint32_t byte=0;byte<nbytes;byte++)
		{
			uint8_t packed = source[byte];
#define  WRITE(xs, idx)  pixels[byte*8/bpp_in + (size_t)(xs)] = palette[(idx)]
			
			if (bpp_in == 1)
			{
				bpp1_max_bits |= packed;
				
#ifdef __SSE2__
				uint64_t fanout = (uint64_t)packed * bpp1_fanout_magic; // expand bits of packed to 0x80 bits of 64bit reg
				__m128i mask = _mm_cvtsi64_si128(fanout); // the other 56 bits of fanout are don't care terms
				mask = _mm_unpacklo_epi8(mask, mask); // left term is don't care, use mask because _mm_undefined_si128() optimizes poorly
				mask = _mm_srai_epi16(mask, 15); // replicate 0x80 bits of fanout to every bit of int16s
				
				_mm_storeu_si128((__m128i*)(pixels+byte*8+0),
					_mm_xor_si128(bpp1_bit0, _mm_and_si128(bpp1_bitxor, _mm_unpacklo_epi16(mask,mask))));
				_mm_storeu_si128((__m128i*)(pixels+byte*8+4),
					_mm_xor_si128(bpp1_bit0, _mm_and_si128(bpp1_bitxor, _mm_unpackhi_epi16(mask,mask))));
#else
				WRITE(0, (packed>>7)&1);
				WRITE(1, (packed>>6)&1);
				WRITE(2, (packed>>5)&1);
				WRITE(3, (packed>>4)&1);
				WRITE(4, (packed>>3)&1);
				WRITE(5, (packed>>2)&1);
				WRITE(6, (packed>>1)&1);
				WRITE(7, (packed>>0)&1);
#endif
			}
			if (bpp_in == 2)
			{
				// overflow if
				// (((x+y)^((x^y)&HIGH)) & HIGH) != 0
				// HIGH = bits to detect overflow into
				// x = a byte
				// y = 1<<nbits - pallen, repeated
				// only used for 2bpp, more straightforward algorithms are better for the other three
				uint32_t addend_2bpp = (4-pallen) * 0x55;
				if (UNLIKELY( ((packed+addend_2bpp)^((packed^addend_2bpp)&0x154)) & 0x154 )) return false;
				
				WRITE(0, (packed>>6)&3);
				WRITE(1, (packed>>4)&3);
				WRITE(2, (packed>>2)&3);
				WRITE(3, (packed>>0)&3);
			}
			if (bpp_in == 4)
			{
				if (UNLIKELY((uint32_t)(packed&15) >= pallen)) return false;
				if (UNLIKELY((uint32_t)(packed>>4) >= pallen)) return false;
				
				WRITE(0, (packed>>4)&15);
				WRITE(1, (packed>>0)&15);
			}
			if (bpp_in == 8)
			{
				if (UNLIKELY(packed >= pallen)) return false;
				
				WRITE(0, (packed>>0)&255);
			}
#undef WRITE
		}
		
		pixels += width;
		source += srcstride;
	}
	
	if (bpp_in == 1 && UNLIKELY(pallen == 1 && bpp1_max_bits != 0)) return false;
	return true;
}
template bool image::png_unpack_plte<1>(const uint8_t *, size_t, const uint32_t *, uint32_t) const;
template bool image::png_unpack_plte<2>(const uint8_t *, size_t, const uint32_t *, uint32_t) const;
template bool image::png_unpack_plte<4>(const uint8_t *, size_t, const uint32_t *, uint32_t) const;
template bool image::png_unpack_plte<8>(const uint8_t *, size_t, const uint32_t *, uint32_t) const;

template<int color_type>
void image::png_unpack_rgb(const uint8_t * source, size_t srcstride) const
{
	uint32_t * pixels = this->pixels32;
	uint32_t width = this->width;
	uint32_t height = this->height;
	
	size_t nsamp;
	if (color_type == 0) nsamp = 1; // gray
	if (color_type == 2) nsamp = 3; // rgb
	if (color_type == 4) nsamp = 2; // gray+alpha
	if (color_type == 6) nsamp = 4; // rgba
	
	for (uint32_t y=0;y<height;y++)
	{
		uint32_t x = 0;
		if (color_type == 2)
		{
			image::convert_scanline<ifmt_rgb888_by, ifmt_bargb8888>(pixels, source, width);
			x = width;
		}
		
#ifdef __SSE2__
		if (color_type == 6)
		{
			while (x+3 < width)
			{
				const uint8_t * sourceat = source + x*nsamp;
				__m128i rgba = _mm_loadu_si128((__m128i*)sourceat);
				__m128i _g_a = _mm_and_si128(rgba, _mm_set1_epi32(0xFF00FF00));
				__m128i bxgx = _mm_shufflehi_epi16(_mm_shufflelo_epi16(rgba, _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1));
				__m128i bgra = _mm_or_si128(_g_a, _mm_and_si128(bxgx, _mm_set1_epi32(0x00FF00FF)));
				_mm_storeu_si128((__m128i*)(pixels+x), bgra);
				x += 4;
			}
		}
		SIMD_LOOP_TAIL
#endif
		
		while (x < width)
		{
			const uint8_t * sourceat = source + x*nsamp;
			
			if (color_type == 0)
				pixels[x] = 0xFF000000 | sourceat[0]*0x010101;
			// type 2 fully handled above
			if (color_type == 4)
				pixels[x] = (uint32_t)sourceat[1]<<24 | sourceat[0]*0x010101;
			if (color_type == 6)
				pixels[x] = (uint32_t)sourceat[3]<<24 | sourceat[0]<<16 | sourceat[1]<<8 | sourceat[2]<<0;
			x++;
		}
		
		pixels += width;
		source += srcstride;
	}
}
template void image::png_unpack_rgb<0>(const uint8_t *, size_t) const;
template void image::png_unpack_rgb<2>(const uint8_t *, size_t) const;
template void image::png_unpack_rgb<4>(const uint8_t *, size_t) const;
template void image::png_unpack_rgb<6>(const uint8_t *, size_t) const;

#include "test.h"
#include "file.h"

//PngSuite is a good start, but it leaves a lot of stuff untested:
// Prefix key:
// [+] rare, but it's a valid PNG
// [-] violates the PNG standard, should be rejected
// [+?] valid per the spec, but can't be decoded in practice
// [-?] invalid per spec, but are in otherwise-ignored parts, so it's fine to accept these PNGs
// [+6] valid per the spec, but the image uses 16bit color; if that's unimplemented, rejecting is fine
// [-T] invalid per the spec, but it's in the ancillary tRNS chunk; if that's unimplemented, accepting is fine
//[+] funky but legit sizes like 1*1024 and 1024*1
//[+] non-square interlaced images, at least 1*n and n*1 for every n<=9, but preferably n*m for all n,m<=9
//[-] invalid sizes: 32*0px, 2^31*32px, etc (spec says 2^31-1 is max)
//[+?] 2^31-1*2^31-1px image that's technically legal but takes several exabytes of RAM to render
//[+?] 2^31-1*1 image, may provoke a few integer overflows (or out-of-memory)
//[+?] actually, every 2^[20..30]*1 should be tested. and 1*2^[20..30]. and those minus 1. and 2*?.
//      valid per spec, but anyone trying to decode them will run out of memory; as such, rejecting is acceptable
//[-?] checksum error in an ancillary chunk
//      invalid per spec, but unknown ancillary chunks must be skipped, and 'skip' can be argued to include skipping checksum calculation
//[-] PLTE chunk length not divisible by 3
//[-] PLTE with 0 colors
//[-] PLTE with 257 colors
//[-] PLTE on grayscale
//[-] multiple PLTEs in paletted image
//[-] paletted image where some pixels use colors not in the PLTE (4bpp, 9-entry PLTE, but color 12 is used)
//[-] critical chunks in wrong order
//[+] overlong encoding of palette data - <=16-color PLTE but 8 bits per pixel
//[+] PLTE in RGB - a recommendation for how to render in low-color contexts, should be ignored by high-color renderers
//[-?] misplaced PLTE in RGB; ignoring PLTE in RGB means this won't be detected
//[-?] multiple PLTEs in RGB
//[-?] 257-color PLTE in RGB
//[-] paletted image with too long tRNS
//[+6] for bit width 16, a tRNS chunk saying RGB 0x0001*3 is transparent, and image contains u16[]{1,1,2} that should not be transparent
//[+6] for bit width 16, some filtering shenanigans that yield different results if 16->8bpp conversion is done before filtering
//      (filtering should be able to amplify differences)
//[+] size-0 tRNS with paletted data
//      the PNG specification does not mention whether this is allowed; in most RFCs, such special cases tend to follow the general rule,
//      but this specific RFC calls out one- and zero-byte IDATs as valid, so that's an oversight in the spec. Still, probably valid.
//[+] tRNS on grayscale (both 4bpp and 8bpp, since my decoder treats them differently)
//[-T] tRNS on gray+alpha / RGBA
//[-T] tRNS with values out of bounds
//[-T] tRNS with wrong size (not multiple of 3, or for types 0 or 2, not 2 resp. 6 bytes)
//[-T] tRNS larger than the palette
//[-T] tRNS before PLTE
//[-T] tRNS after IDAT
//[-] filter type other than 0
//[-] filter type 0, but some scanlines have filters > 4
//[-] filter type 0, but some scanlines have filters >= 128 (to make sure they don't treat it as signed)
//[+] filter != 0 on y=0 (prior scanline must be treated as all-zero)
//[+] test whether the Paeth filter breaks ties properly
//[+] IDAT/ordering shenanigans with bit width < 16
//[-] actual ordering shenanigans, as opposed to just splitting the IDATs
//[-] truncated compressed data in IDATs
//[-] various invalid DEFLATE data, like trying to read outside the sliding window
//[-] a chunk of size (uint32_t)-42 (not -1 since that's a common sentinel)
//[-] a chunk of size 0x7FFFFF42 (not gonna make the actual file 2GB)
//[+] a size-0 IDAT chunk (completely missing IDAT is tested)
//[-] IDATs decompressing to too much or too little data
//[-?] garbage bytes after IEND
//[-?] an ancillary chunk after IEND
//      invalid per spec, but sometimes file length isn't available; as such, ignoring trailing garbage must be deemed acceptable
//      any such placed chunks must, however, be ignored
//[-] ancillary chunk before IHDR
//[-] bad IHDR size
//[-] nonzero IEND size
//[-] IEND checksum error
//[-] unexpected EOFs
//finally, the reference images are inconvenient; they're not bulk downloadable, and they're GIF, rather than PNG without fancy features
//instead, I ran all of them through 'pngout -c6 -f0 -d8 -s4 -y -force'
//I also ran the invalid x*.png, and pngout-unsupported *16.png, through 'truncate -s0' instead
//most png decoders are tested only with PngSuite
//
//I will create these once I have a png encoder to manipulate,
// except bitwidth 16 (rare and unsupported), interlaced (rare and unsupported), and invalid DEFLATE (need a deflater to manipulate)

test("png", "array,imagebase,file", "png")
{
	test_skip("kinda slow");
	
	array<string> tests = file::listdir("test/png/");
	assert_gt(tests.size(), 100); // make sure the tests exist, no vacuous truths allowed
	
	//TODO: check https://code.google.com/p/imagetestsuite/ and figure out why it disagrees with pngout on whether they're valid
	//(there are plenty where different png decoders disagree about its validity)
	
	{
		oimage img;
		assert(img.init_decode_png(file::readall("test/png/ps-s05n3p02.png")));
		assert_eq(img.width, 5);
		assert_eq(img.height, 5);
		assert_eq(img.fmt, ifmt_xrgb8888);
		assert_eq(img.stride, 5*sizeof(uint32_t));
		uint32_t expected[] = { 0xFFFF0000,0xFFFF0000,0xFFFF0000,0xFFFF0000,0xFFFF0000,
		                        0xFFFF0000,0xFF7700FF,0xFF7700FF,0xFF7700FF,0xFFFF0000,
		                        0xFFFF0000,0xFF7700FF,0xFF00FFFF,0xFF7700FF,0xFFFF0000,
		                        0xFFFF0000,0xFF7700FF,0xFF7700FF,0xFF7700FF,0xFFFF0000,
		                        0xFFFF0000,0xFFFF0000,0xFFFF0000,0xFFFF0000,0xFFFF0000};
		assert_eq(arrayview<uint32_t>(img.pixels32, 25), expected);
	}
	
	for (size_t i=0;i<tests.size();i++)
	{
//puts(tests[i]);
//printf("\r                                        \r(%d) %s... ", i, (const char*)tests[i]);
		oimage im;
		oimage ref;
		
		if (tests[i].endswith("-n.png") || !tests[i].endswith(".png")) continue;
		
		testctx(tests[i]) {
			string refname = tests[i];
			string testname = tests[i].replace("/test/png/", "/test/png/reference/");
			
			bool expect = ref.init_decode_png(file::readall(refname));
			
			if (expect)
			{
				assert(im.init_decode_png(file::readall(testname)));
				assert_eq(im.width, ref.width);
				assert_eq(im.height, ref.height);
				
				uint32_t* imp = im.pixels32;
				uint32_t* refp = ref.pixels32;
				
//if(tests[i].contains("unknown"))
//{
//for (size_t i=0;i<ref.height*ref.width;i++)
//{
//if(i%ref.width==0)puts("");
//printf("%.8X ",refp[i]);
//}
//puts("\n");
//for (size_t i=0;i<im.height*im.width;i++)
//{
//if(i%dm.width==0)puts("");
//printf("%.8X ",imp[i]);
//}
//puts("\n");
//}
				for (size_t i=0;i<ref.height*ref.width;i++)
				{
//printf("%lu,%u,%u\n",i,imp[i],refp[i]);
					if (refp[i]&0xFF000000) assert_eq(imp[i], refp[i]);
					else assert_eq(imp[i]&0xFF000000, 0);
				}
			}
			else
			{
				assert(!im.init_decode_png(file::readall(testname)));
			}
		}
	}
}
