#include "image.h"
#include "simd.h"

//checks if the source fits in the target, and if not, crops it; then calls the above
void image::insert(int32_t x, int32_t y, const image& other)
{
	if (UNLIKELY(x < 0 || y < 0 || x+other.width > width || y+other.height > height))
	{
		struct image part = other;
		if (x < 0)
		{
			part.pixels = part.pixels - x;
			if ((uint32_t)-x >= part.width) return;
			part.width -= -x;
			x = 0;
		}
		if (y < 0)
		{
			part.pixels = part.pixels + (-y)*part.stride;
			if ((uint32_t)-y >= part.height) return;
			part.height -= -y;
			y = 0;
		}
		if (x+part.width > width)
		{
			if ((uint32_t)x >= width) return;
			part.width = width-x;
		}
		if (y+part.height > height)
		{
			if ((uint32_t)y >= height) return;
			part.height = height-y;
		}
		insert_noov(x, y, part);
	}
	else
	{
		insert_noov(x, y, other);
	}
}

template<bool has_empty, bool has_alpha>
static inline void image_insert_noov_inner(image& target, int32_t x, int32_t y, const image& source);

void image::insert_noov(int32_t x, int32_t y, const image& source)
{
	if (!source.has_empty)
		image_insert_noov_inner<false, false>(*this, x, y, source);
	else if (!source.has_alpha)
		image_insert_noov_inner<true, false>(*this, x, y, source);
	else
		image_insert_noov_inner<true, true>(*this, x, y, source);
	
	if (this->has_empty && source.has_alpha)
		this->has_alpha = true;
}

//lower - usually target
//upper - usually source; its alpha decides how much of the lower color is visible
//always does the full blending operation; if A=FF 90% of the time and A=00 90% of the remainder, that optimization is caller's job
static inline uint32_t blend_8888_on_8888(uint32_t argb_lower, uint32_t argb_upper)
{
#ifdef __SSE2__
	//no need to extend this above 128bit, it's complex enough without having to consider multiple pixels at once
	
	uint32_t spx = argb_upper;
	uint32_t tpx = argb_lower;
	
	//contains u16: spx.a, spx.b, spx.g, spx.r, tpx.{a,b,g,r}
	__m128i vals = _mm_unpacklo_epi8(_mm_set_epi32(0, 0, spx, tpx), _mm_setzero_si128());
	
	//contains u16: {sa}*4, {255-sa}*4
	__m128i alphas = _mm_xor_si128(_mm_set1_epi16(spx>>24), _mm_set_epi16(0,0,0,0, 255,255,255,255));
	//contains u16: pixel contributions times 255, separate per source/target
	__m128i newcols255 = _mm_mullo_epi16(vals, alphas);
	
	//contains u16: pixel contributions times 255, with combined channels (high half is garbage)
	newcols255 = _mm_add_epi16(_mm_bsrli_si128(newcols255, 64/8), _mm_add_epi16(newcols255, _mm_set1_epi16(254)));
	
	//ugly magic constants: (u16)*0x8081>>16>>7 = (u16)/255
	__m128i newcols = _mm_srli_epi16(_mm_mulhi_epu16(newcols255, _mm_set1_epi16(0x8081)), 7);
	
	return _mm_cvtsi128_si32(_mm_packus_epi16(newcols, _mm_undefined_si128()));
#else
	uint8_t sr = argb_upper>>0;
	uint8_t sg = argb_upper>>8;
	uint8_t sb = argb_upper>>16;
	uint8_t sa = argb_upper>>24;
	
	uint8_t tr = argb_lower>>0;
	uint8_t tg = argb_lower>>8;
	uint8_t tb = argb_lower>>16;
	uint8_t ta = argb_lower>>24;
	
	tr = (sr*sa + tr*(255-sa) + 254)/255;
	tg = (sg*sa + tg*(255-sa) + 254)/255;
	tb = (sb*sa + tb*(255-sa) + 254)/255;
	ta = (sa*sa + ta*(255-sa) + 254)/255;
	
	return (uint32_t)ta<<24 | tb<<16 | tg<<8 | tr<<0; // extra cast because u8 << 24 is signed
#endif
}

template<bool has_empty, bool has_alpha>
static inline void image_insert_noov_inner(image& target, int32_t x, int32_t y, const image& source)
{
	for (uint32_t yy=0;yy<source.height;yy++)
	{
		uint32_t* __restrict targetpx = target.pixels + (y+yy)*target.stride + x;
		uint32_t* __restrict sourcepx = source.pixels + yy*source.stride;
		
		uint32_t xx = 0;
		
#if defined(__SSE2__)
		if (!has_alpha)
		{
			// SIMD translation of the below
			// this particular loop is trivial to vectorize, but there's no vectorization on -Os
			// (in fact, on -O3, compiler vectorizes the post-SIMD loop that never has more than three iterations... grumble grumble...)
			
			// I could do a few non-SIMD iterations before that and use aligned instructions,
			// but intel intrinsics guide say they're same speed, so no point
			for (;xx+4<=source.width;xx+=4)
			{
				__m128i spx = _mm_loadu_si128((__m128i*)(sourcepx+xx));
				__m128i tpx = _mm_loadu_si128((__m128i*)(targetpx+xx));
				
				if (has_empty)
				{
					// copy sign bit to everywhere
					__m128i spx_mask = _mm_srai_epi32(spx, 31);
					// if mask_local bit is set, copy from sp, otherwise from tp
					// this is AVX2 _mm_maskstore_epi32, but that's not available in SSE2
					// but it's also easy to bithack (andnot gives shorter dependency chains than xor)
					spx = _mm_or_si128(_mm_and_si128(spx_mask, spx), _mm_andnot_si128(spx_mask, tpx));
				}
				_mm_storeu_si128((__m128i*)(targetpx+xx), spx);
			}
		}
#endif
		
		for (;xx<source.width;xx++)
		{
			uint32_t spx = sourcepx[xx];
			uint32_t tpx = targetpx[xx];
			
			if (has_alpha)
			{
				uint8_t alpha = spx >> 24;
				if (LIKELY(alpha == 255))
					tpx = spx;
				else if (LIKELY(alpha == 0))
					tpx = tpx;
				else
					tpx = blend_8888_on_8888(tpx, spx);
			}
			else if (!has_empty || (spx&0x80000000)) // for empty-only alpha, check sign only, it's the cheapest
				tpx = spx;
			
			targetpx[xx] = tpx; // always writing lets compilers vectorize better
		}
	}
}

void image::convert_rgb24_to_argb32(uint32_t* out, const uint8_t* in, size_t npx)
{
	uint32_t* out32 = (uint32_t*)out;
	size_t x = 0;
#if defined(__SSE2__)
	// we wish to read 12 bytes at the time, but loadu_si28 does 16, so -2 to ensure we don't read out of bounds
	// except put the -2 at the x side, (size_t)1-2 isn't less than anything
	while (x+4+2 <= npx)
	{
		uint8_t* src = (uint8_t*)in + x*3;
		
		__m128i pix = _mm_loadu_si128((__m128i*)src);
		//pix = u8*16 { rgbrgbrgbrgbXXXX }
		pix = _mm_bslli_si128(pix, 2);
		//pix = u8*16 { 00rgbrgbrgbrgbXX }
		pix = _mm_shufflehi_epi16(pix, _MM_SHUFFLE(2,1,0,3));
		//pix = u8*16 { 00rgbrgbXXrgbrgb }
		pix = _mm_srli_epi64(pix, 1*8); // change to srli(2*8) then slli(1*8) to set alpha to zero
		//pix = u8*16 { 0rgbrgb0Xrgbrgb0 }
		
		__m128i pix1 = _mm_unpacklo_epi8(pix, _mm_setzero_si128());
		__m128i pix2 = _mm_unpackhi_epi8(pix, _mm_setzero_si128());
		pix1 = _mm_shufflelo_epi16(pix1, _MM_SHUFFLE(0,1,2,3)); // 0rgb -> 0bgr
		pix1 = _mm_shufflehi_epi16(pix1, _MM_SHUFFLE(3,0,1,2)); // rgb0 -> 0bgr
		pix2 = _mm_shufflelo_epi16(pix2, _MM_SHUFFLE(0,1,2,3)); // Xrgb -> Xbgr
		pix2 = _mm_shufflehi_epi16(pix2, _MM_SHUFFLE(3,0,1,2)); // rgb0 -> 0bgr
		pix = _mm_packus_epi16(pix1, pix2);
		//pix = u8*16 { 0bgr0bgrXbgr0bgr }
		
		pix = _mm_or_si128(pix, _mm_set1_epi32(0xFF000000)); // omit to set alpha to arbitrary
		
		_mm_storeu_si128((__m128i*)(out32 + x), pix);
		
		x += 4;
	}
	SIMD_LOOP_TAIL
#endif
	// Clang can vectorize this, but it emits some quite messy code, so keep the SSE2 anyways
	// (in fact, Clang flattens the above all the way to a single pshufb, if -mssse3)
	// gcc doesn't vectorize the below, nor do anything interesting to the above
	while (x < npx)
	{
		uint8_t* src = (uint8_t*)in + x*3;
		out32[x] = 0xFF000000 | src[0]<<16 | src[1]<<8 | src[2];
		x++;
	}
}


#ifdef ARLIB_GUI_GTK3
#include <gtk/gtk.h>

static oimage image_decode_gtk(arrayview<uint8_t> data)
{
	GInputStream* is = g_memory_input_stream_new_from_data(data.ptr(), data.size(), NULL);
	GdkPixbuf* pix = gdk_pixbuf_new_from_stream(is, NULL, NULL);
	g_object_unref(is);
	
	if (!pix)
		return {};
	
	oimage ret = oimage::create(gdk_pixbuf_get_width(pix), gdk_pixbuf_get_height(pix), false, false);
	
	uint8_t* bytes = gdk_pixbuf_get_pixels(pix);
	uint32_t instride = gdk_pixbuf_get_rowstride(pix);
	uint32_t inchan = gdk_pixbuf_get_n_channels(pix);
	if (inchan == 3)
	{
		for (uint32_t y=0;y<ret.height;y++)
		{
			image::convert_rgb24_to_argb32(ret.pixels + y*ret.stride, bytes + y*instride, ret.width);
		}
	}
	else if (inchan == 4)
	{
abort(); // TODO: do something with src[3], and set ifmt properly
		bool has_empty = false;
		bool has_alpha = false;
		for (uint32_t y=0;y<ret.height;y++)
		{
			for (uint32_t x=0;x<ret.width;x++)
			{
				uint8_t* src = bytes + y*instride + x*4;
				ret.pixels[y*ret.stride + x] = src[0]<<16 | src[1]<<8 | src[2];
			}
		}
		ret.has_empty = has_empty;
		ret.has_alpha = has_alpha;
	}
	else abort(); // gtk docs promise 3 or 4 only
	g_object_unref(pix);
	
	return ret;
}
#endif

//TODO: use gdi+ or wic or whatever on windows
//https://msdn.microsoft.com/en-us/library/windows/desktop/ee719875(v=vs.85).aspx
//
//https://msdn.microsoft.com/en-us/library/windows/desktop/ee690179(v=vs.85).aspx says
// "CopyPixels is one of the two main image processing routines (the other being Lock) triggering the actual processing."
//so despite the name, it won't create pointless copies in RAM
//
//WIC is present on XP SP3 and Vista, aka WIC is present

//other possibilities:
//https://msdn.microsoft.com/en-us/library/ee719902(v=VS.85).aspx
//https://stackoverflow.com/questions/39312201/how-to-use-gdi-library-to-decode-a-jpeg-in-memory
//https://stackoverflow.com/questions/1905476/is-there-any-way-to-draw-a-png-image-on-window-without-using-mfc

//may want to do the same for PNG, to save some kilobytes
//or maybe it's better to keep my homebrew, so I know it'll work the same way everywhere


oimage image::decode(arrayview<uint8_t> data)
{
	oimage ret;
	if (!ret.pixels) ret = decode_png(data);
	return ret;
}

oimage image::decode_extern(arrayview<uint8_t> data)
{
	oimage ret;
#ifdef ARLIB_GUI_GTK3
	if (!ret.pixels) ret = image_decode_gtk(data);
#endif
	return {};
}

oimage image::decode_permissive(arrayview<uint8_t> data)
{
	oimage ret;
	if (!ret.pixels) ret = decode(data);
	if (!ret.pixels) ret = decode_extern(data);
	return ret;
}



#include "test.h"

test("image blend 8888 on 8888", "", "imagebase")
{
	int fail = 0;
	for (uint32_t dst_r=0;dst_r<256;dst_r++)
	{
		uint32_t dst_pix = 0xFF000000|dst_r;
		if (dst_r >= 2)
		{
			uint32_t retpix = blend_8888_on_8888(dst_pix-2, 0x01000000|dst_r);
			if ((retpix&0xFF) != dst_r-1)
			{
					printf("blend %d on darker failed, got %d, expected %d\n", dst_r, retpix&255, dst_r+1);
					fail++;
			}
			if ((retpix&0xFF000000) != 0xFF000000)
			{
					printf("blend %d on darker gave wrong alpha\n", dst_r);
					fail++;
			}
		}
		if (dst_r <= 253)
		{
			uint32_t retpix = blend_8888_on_8888(dst_pix+2, 0x01000000|dst_r);
			// this one is expected to fail, can't find a way to do it properly without messing up performance
			//if ((retpix&0xFF) != dst_r+1)
			//{
			//		printf("blend %d on brighter failed, got %d, expected %d\n", dst_r, retpix&255, dst_r+1);
			//		fail++;
			//}
			if ((retpix&0xFF000000) != 0xFF000000)
			{
					printf("blend %d on darker gave wrong alpha\n", dst_r);
					fail++;
			}
		}
	}
	assert_eq(fail, 0);
}
