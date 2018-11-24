#include "image.h"
#ifdef __SSE2__
// anything SIMDed wants maximum performance, which often requires quite specialized operations that don't exist anywhere else
// any attempt at a platform-neutral SIMD layer would yield dissatisfactory performance for all but the most trivial operations,
//  which are trivial to reimplement for each platform anyways - or let compiler autovectorize
// if there are many enough platforms and trivial SIMDable locations, it would be worthwhile to find some suitable subset
//  of SIMD operations that are 'always' available, but not for something as small as Arlib
// therefore, trying to introduce a platform-neutral SIMD layer is a waste of time

#include <emmintrin.h>
#endif

static inline void image_insert_noov(image& target, int32_t x, int32_t y, const image& source);
//checks if the source fits in the target, and if not, crops it; then calls the above
void image::insert(int32_t x, int32_t y, const image& other)
{
	if (UNLIKELY(x < 0 || y < 0 || x+other.width > width || y+other.height > height))
	{
		struct image part;
		part.init_ref(other);
		if (x < 0)
		{
			part.pixels8 = part.pixels8 + (-x)*byteperpix(part.fmt);
			if ((uint32_t)-x >= part.width) return;
			part.width -= -x;
			x = 0;
		}
		if (y < 0)
		{
			part.pixels8 = part.pixels8 + (-y)*part.stride;
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
		image_insert_noov(*this, x, y, part);
	}
	else
	{
		image_insert_noov(*this, x, y, other);
	}
}

//newalpha can be 0 to set alpha to 0, 0xFF000000 to set alpha to 255, or -1 to not change alpha
//  other newalpha are undefined behavior
//alpha in the target must be known equal to the value implied by newalpha;
//  the functions are allowed to copy alpha from target instead
template<bool checksrcalpha, uint32_t newalpha>
static inline void image_insert_noov_8888_to_8888(image& target, int32_t x, int32_t y, const image& source);
template<uint32_t newalpha> // 0x??000000 to overwrite the alpha channel, -1 to ignore
static inline void image_insert_noov_8888_to_8888_blend(image& target, int32_t x, int32_t y, const image& source);

static inline void image_insert_noov(image& target, int32_t x, int32_t y, const image& source)
{
	//dst\src  x  0  a   ba
	//  x      =  =  C0  ?=
	//  0      0  =  C0  ?=0
	//  a      1  1  Ca  ?=
	//  ba     1  1  C0  ?=
	// = - bitwise copy
	// 0 - bitwise copy, but set alpha to 0
	// 1 - bitwise copy, but set alpha to 1
	// ?= - bitwise copy if opaque, otherwise leave unchanged
	// ?=0 - if opaque, bitwise copy but set alpha to 0; otherwise leave unchanged
	// C0 - composite, but set output alpha to zero
	// Ca - composite
	// dst=ba src=a with non-ba-compliant src will yield shitty results,
	//  but there is no possible answer so the only real solution is banning it in the API.
	
	if (source.fmt == ifmt_bargb8888 && target.fmt == ifmt_0rgb8888)
		return image_insert_noov_8888_to_8888<true, 0x00000000>(target, x, y, source);
	if (source.fmt == ifmt_bargb8888)
		return image_insert_noov_8888_to_8888<true, (uint32_t)-1>(target, x, y, source);
	if (source.fmt == ifmt_argb8888 && target.fmt == ifmt_0rgb8888)
		return image_insert_noov_8888_to_8888_blend<0x00000000>(target, x, y, source);
	if (source.fmt == ifmt_argb8888)
		return image_insert_noov_8888_to_8888_blend<(uint32_t)-1>(target, x, y, source);
	if (target.fmt == ifmt_argb8888 || target.fmt == ifmt_bargb8888)
		return image_insert_noov_8888_to_8888<false, 0xFF000000>(target, x, y, source);
	if (target.fmt == ifmt_0rgb8888 && source.fmt == ifmt_xrgb8888)
		return image_insert_noov_8888_to_8888<false, 0x00000000>(target, x, y, source);
	else
		return image_insert_noov_8888_to_8888<false, (uint32_t)-1>(target, x, y, source);
}

//lower - usually target
//upper - usually source; its alpha decides how much of the lower color is visible
//always does the full blending operation, does not optimize based on A=FF being true 90% of the time and A=00 90% of the remainder
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
	//contains u16: pixel contributions times 255
	__m128i newcols255 = _mm_mullo_epi16(vals, alphas);
	
	//ugly magic constants: (u16)*8081>>16>>7 = (u16)/255
	__m128i newcols = _mm_srli_epi16(_mm_mulhi_epu16(newcols255, _mm_set1_epi16(0x8081)), 7);
	
	//contains u8: {don't care}*8, sac (source alpha contribution), sbc, sgc, src, tac, tbc, tgc, trc
	__m128i newpack = _mm_packus_epi16(newcols, _mm_undefined_si128());
	//contains u8: {don't care}*12, sac+tac = result alpha, sbc+tbc, sgc+tgc, src+trc
	//the components are known to not overflow
	__m128i newpacksum = _mm_add_epi8(newpack, _mm_srli_si128(newpack, 32/8));
	
	return _mm_cvtsi128_si32(newpacksum);
#else
	uint8_t sr = argb_upper>>0;
	uint8_t sg = argb_upper>>8;
	uint8_t sb = argb_upper>>16;
	uint8_t sa = argb_upper>>24;
	
	uint8_t tr = argb_lower>>0;
	uint8_t tg = argb_lower>>8;
	uint8_t tb = argb_lower>>16;
	uint8_t ta = argb_lower>>24;
	
	tr = (sr*sa/255) + (tr*(255-sa)/255);
	tg = (sg*sa/255) + (tg*(255-sa)/255);
	tb = (sb*sa/255) + (tb*(255-sa)/255);
	ta = (sa*sa/255) + (ta*(255-sa)/255);
	
	return ta<<24 | tb<<16 | tg<<8 | tr<<0;
#endif
}

// valid newalpha values: 0x00000000 (set alpha to 0, used for 0rgb), 0xFF000000 (opaque, for bargb), -1 (calculate alpha properly)
template<uint32_t newalpha>
static inline void image_insert_noov_8888_to_8888_blend(image& target, int32_t x, int32_t y, const image& source)
{
	for (uint32_t yy=0;yy<source.height;yy++)
	{
		uint32_t* targetpx = target.pixels32 + (y+yy)*target.stride/sizeof(uint32_t) + x;
		uint32_t* sourcepx = source.pixels32 + yy*source.stride/sizeof(uint32_t);
		
		for (uint32_t xx=0;xx<source.width;xx++)
		{
			uint8_t alpha = sourcepx[xx] >> 24;
			
			if (alpha != 255)
			{
				if (alpha != 0)
				{
					uint32_t newpx = blend_8888_on_8888(targetpx[xx], sourcepx[xx]);
					if (newalpha == 0x00000000) newpx &= 0x00FFFFFF;
					if (newalpha == 0xFF000000) newpx |= 0xFF000000;
					targetpx[xx] = newpx;
				}
				//else a=0 -> do nothing
			}
			else
			{
				if (newalpha != (uint32_t)-1)
					targetpx[xx] = newalpha | (sourcepx[xx]&0x00FFFFFF);
				else
					targetpx[xx] = sourcepx[xx];
			}
		}
	}
}

template<bool checksrcalpha, uint32_t newalpha>
static inline void image_insert_noov_8888_to_8888(image& target, int32_t x, int32_t y, const image& source)
{
	for (uint32_t yy=0;yy<source.height;yy++)
	{
		uint32_t* __restrict targetpx = target.pixels32 + (y+yy)*target.stride/sizeof(uint32_t) + x;
		uint32_t* __restrict sourcepx = source.pixels32 + yy*source.stride/sizeof(uint32_t);
		
		//strangely enough, doing this slows things down.
		//if (!checksrcalpha && newalpha==-1)
		//{
		//	memcpy(targetpx, sourcepx, sizeof(uint32_t)*source.width);
		//	continue;
		//}
		
		// TODO: enable AUTOVECTORIZE on -O3 - Gcc autovectorizes the post-SIMD loop...
#if defined(__SSE2__) && !defined(AUTOVECTORIZE)
		//SIMD translation of the below
		//this particular loop is trivial to vectorize, but there's no vectorization on -Os
		//(in fact, on -O3, compiler vectorizes the post-SIMD loop that never has more than three iterations... grumble grumble...)
		size_t nsimd = 4;
		
		__m128i* __restrict targetpxw = (__m128i*)targetpx;
		__m128i* __restrict sourcepxw = (__m128i*)sourcepx;
		uint32_t xxew = source.width/nsimd;
		
		__m128i mask_or  = (newalpha == 0xFF000000 ? _mm_set1_epi32(0xFF000000) : _mm_set1_epi32(0x00000000));
		__m128i mask_and = (newalpha == 0x00000000 ? _mm_set1_epi32(0x00FFFFFF) : _mm_set1_epi32(0xFFFFFFFF));
		
		//I could do a few non-SIMD iterations before that and use aligned instructions,
		// but intel intrinsics guide say they're same speed, so yawn
		for (uint32_t xx=0;xx<xxew;xx++)
		{
			__m128i px = _mm_loadu_si128(&sourcepxw[xx]);
			
			//copy sign bit to everywhere
			__m128i mask_local = _mm_srai_epi32(px, 31);
			
			px = _mm_and_si128(mask_and, _mm_or_si128(mask_or, px));
			
			if (checksrcalpha)
			{
				__m128i tpx = _mm_loadu_si128(&targetpxw[xx]);
				//if mask_local bit is set, copy from sp, otherwise from tp
				//this is AVX2 _mm_maskstore_epi32, but that's not available in SSE2
				//but it's also easy to bithack (either with xor or andnot; latter gives shorter dependency chains)
				px = _mm_or_si128(_mm_and_si128(mask_local, px), _mm_andnot_si128(mask_local, tpx));
			}
			_mm_storeu_si128(&targetpxw[xx], px);
		}
		
#else
		//the one-pixel loop is needed to handle the last few pixels without overflow
		//if there's no SIMD, just run it for everything
		size_t xxew = 0;
		size_t nsimd = 0;
#endif
		
		for (uint32_t xx=xxew*nsimd;xx<source.width;xx++)
		{
			uint32_t spx = sourcepx[xx];
			uint32_t tpx = targetpx[xx];
			if (!checksrcalpha || (spx&0x80000000)) // for bargb, check sign only, it's the cheapest
			{
				if (newalpha == 0xFF000000 && checksrcalpha) // if spx&0x80000000 is set, the entire 0xFF000000 must be set,
					tpx = spx; // so we can just copy that, and save ourselves an OR
				else if (newalpha != (uint32_t)-1)
					tpx = newalpha | (spx&0x00FFFFFF);
				else
					tpx = spx;
			}
			targetpx[xx] = tpx; // don't inline this into the above, always writing lets compilers vectorize better
		}
	}
}



//this one requires height <= other.height
static void image_insert_tile_row(image& target, int32_t x, int32_t y, uint32_t width, uint32_t height, const image& other)
{
	uint32_t xx = 0;
	if (width >= other.width)
	{
		for (xx = 0; xx < width-other.width; xx += other.width)
		{
			target.insert_sub(x+xx, y, other, 0, 0, other.width, height);
		}
	}
	if (xx < width)
	{
		target.insert_sub(x+xx, y, other, 0, 0, width-xx, height);
	}
}
void image::insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height, const image& other)
{
	uint32_t yy = 0;
	if (height >= other.height)
	{
		for (yy = 0; yy < height-other.height; yy += other.height)
		{
			image_insert_tile_row(*this, x, y+yy, width, other.height, other);
		}
	}
	
	if (yy < height)
	{
		image_insert_tile_row(*this, x, y+yy, width, height-yy, other);
	}
}


static void image_insert_tile_row(image& target, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                  const image& other, uint32_t offx, uint32_t offy)
{
	if (offx + width <= other.width)
	{
		target.insert_sub(x, y, other, offx, offy, width, height);
		return;
	}
	
	if (offx != 0)
	{
		target.insert_sub(x, y, other, offx, offy, other.width-offx, height);
		x += other.width-offx;
		width -= other.width-offx;
	}
	uint32_t xx = 0;
	if (width >= other.width)
	{
		for (xx = 0; xx < width-other.width; xx += other.width)
		{
			target.insert_sub(x+xx, y, other, 0, offy, other.width, height);
		}
	}
	if (xx < width)
	{
		target.insert_sub(x+xx, y, other, 0, offy, width-xx, height);
	}
}
void image::insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height,
                        const image& other, int32_t offx, int32_t offy)
{
	if (offx < 0) { offx = -offx; offx %= other.width;  offx = -offx; offx += other.width;  }
	if (offy < 0) { offy = -offy; offy %= other.height; offy = -offy; offx += other.height; }
	if ((uint32_t)offx > other.width)  { offx %= other.width;  }
	if ((uint32_t)offy > other.height) { offy %= other.height; }
	
	if (offy + height <= other.height)
	{
		image_insert_tile_row(*this, x, y, width, height, other, offx, offy);
		return;
	}
	
	if (offy != 0)
	{
		image_insert_tile_row(*this, x, y, width, other.height-offy, other, offx, offy);
		y += other.height-offy;
		height -= other.height-offy;
	}
	
	uint32_t yy = 0;
	if (height >= other.height)
	{
		for (yy = 0; yy < height-other.height; yy += other.height)
		{
			image_insert_tile_row(*this, x, y+yy, width, other.height, other, offx, 0);
		}
	}
	
	if (yy < height)
	{
		image_insert_tile_row(*this, x, y+yy, width, height-yy, other, offx, 0);
	}
}



void image::insert_tile_with_border(int32_t x, int32_t y, uint32_t width, uint32_t height,
                                    const image& other, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2)
{
	uint32_t x3 = other.width;
	uint32_t y3 = other.height;
	
	uint32_t w1 = x1-0;
	uint32_t w2 = x2-x1;
	uint32_t w3 = x3-x2;
	uint32_t h1 = y1-0;
	uint32_t h2 = y2-y1;
	uint32_t h3 = y3-y2;
	
	image sub;
	
	insert_sub(x,          y,           other, 0,  0,  x1, y1); // top left
	sub.init_ref_sub(other, x1, 0,  w2, h1); insert_tile(x+x1,       y,           width-w1-w3, h1,           sub); // top
	insert_sub(x+width-w3, y,           other, x2, 0,  w3, y1); // top right
	
	sub.init_ref_sub(other, 0,  y1, w1, h2); insert_tile(x,          y+y1,        w1,          height-h1-h3, sub); // left
	sub.init_ref_sub(other, x1, y1, w2, h2); insert_tile(x+x1,       y+y1,        width-w1-w3, height-h1-h3, sub); // middle
	sub.init_ref_sub(other, x2, y1, w3, h2); insert_tile(x+width-w3, y+y1,        w3,          height-h1-h3, sub); // right
	
	insert_sub(x,          y+height-h3, other, 0,  y2, x1, h3); // bottom left
	sub.init_ref_sub(other, x1, y2, w2, h3); insert_tile(x+x1,       y+height-h3, width-w1-w3, h3,           sub); // bottom
	insert_sub(x+width-w3, y+height-h3, other, x2, y2, w3, h3); // bottom right
}



void image::insert_scale_unsafe(int32_t x, int32_t y, const image& other, int32_t scalex, int32_t scaley)
{
	bool flipx = (scalex<0);
	bool flipy = (scaley<0);
	scalex = abs(scalex);
	scaley = abs(scaley);
	
	for (uint32_t sy=0;sy<other.height;sy++)
	{
		int ty = y + sy*scaley;
		if (flipy) ty = y + (height-1-sy)*scaley;
		uint32_t* targetpx = this->pixels32 + ty*this->stride/sizeof(uint32_t);
		uint32_t* sourcepx = other.pixels32 + sy*other.stride/sizeof(uint32_t);
		uint32_t* targetpxorg = targetpx;
		sourcepx += x;
		
		if (flipx)
		{
			sourcepx += width;
			for (uint32_t sx=0;sx<other.width;sx++)
			{
				sourcepx--;
				for (int32_t xx=0;xx<scalex;xx++)
				{
					*(targetpx++) = *sourcepx;
				}
			}
		}
		else
		{
			for (uint32_t sx=0;sx<other.width;sx++)
			{
				for (int32_t xx=0;xx<scalex;xx++)
				{
					*(targetpx++) = *sourcepx;
				}
				sourcepx++;
			}
		}
		
		for (int32_t yy=1;yy<scaley;yy++)
		{
			memcpy(targetpxorg+yy*stride/sizeof(uint32_t), targetpxorg, sizeof(uint32_t)*width);
		}
	}
}



void image::insert_rectangle(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t argb)
{
	if (x<0) { if (width  < (uint32_t)-x) return; width  -= -x; x=0; }
	if (y<0) { if (height < (uint32_t)-y) return; height -= -y; y=0; }
	if (x+width  >= this->width)  { if ((uint32_t)x >= this->width)  return; width  = this->width  - x; }
	if (y+height >= this->height) { if ((uint32_t)y >= this->height) return; height = this->height - y; }
	
	if (argb >= 0xFF000000)
	{
		if (!height) return;
		
		if (fmt == ifmt_0rgb8888) argb &= 0x00FFFFFF;
		uint32_t* targetpx = this->pixels32 + y*this->stride/sizeof(uint32_t) + x;
		for (uint32_t xx=0;xx<width;xx++)
		{
			targetpx[xx] = argb;
		}
		for (uint32_t yy=1;yy<height;yy++)
		{
			memcpy(targetpx + yy*this->stride/sizeof(uint32_t), targetpx, width*sizeof(uint32_t));
		}
	}
	else
	{
		for (uint32_t yy=0;yy<height;yy++)
		{
			uint32_t* targetpx = this->pixels32 + (y+yy)*this->stride/sizeof(uint32_t) + x;
			for (uint32_t xx=0;xx<width;xx++)
			{
				targetpx[xx] = blend_8888_on_8888(targetpx[xx], argb);
				if (fmt == ifmt_0rgb8888) targetpx[xx] &= 0x00FFFFFF;
			}
		}
	}
}



void image::init_clone(const image& other, int32_t scalex, int32_t scaley)
{
	init_new(other.width * (scalex<0 ? -scalex : scalex), other.height * (scaley<0 ? -scaley : scaley), other.fmt);
	insert_scale_unsafe(0, 0, other, scalex, scaley);
}



bool image::init_decode(arrayview<byte> data)
{
	this->fmt = ifmt_none;
	return
		init_decode_png(data) ||
		//init_decode_jpg(data) ||
		false;
}



#include "test.h"

test("image byte per pixel", "", "imagebase")
{
	assert_eq(image::byteperpix(ifmt_rgb565), 2);
	assert_eq(image::byteperpix(ifmt_rgb888), 3);
	assert_eq(image::byteperpix(ifmt_xrgb1555), 2);
	assert_eq(image::byteperpix(ifmt_xrgb8888), 4);
	assert_eq(image::byteperpix(ifmt_0rgb1555), 2);
	assert_eq(image::byteperpix(ifmt_0rgb8888), 4);
	assert_eq(image::byteperpix(ifmt_argb1555), 2);
	assert_eq(image::byteperpix(ifmt_argb8888), 4);
	assert_eq(image::byteperpix(ifmt_bargb1555), 2);
	assert_eq(image::byteperpix(ifmt_bargb8888), 4);
	assert(!"uncomment init_decode_jpg");
}
