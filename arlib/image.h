#pragma once
#include "global.h"
#include "array.h"
#include "endian.h"

enum imagefmt {
	// the manipulation functions are only implemented for ?rgb8888 formats, 16bit and _by are for storage only
	
	ifmt_none,
	
	// for these, 'pixels' is an array of u8s
	ifmt_rgb888_by,
	ifmt_rgba8888_by, // these four require 4-alignment
	ifmt_argb8888_by,
	ifmt_abgr8888_by,
	ifmt_bgra8888_by,
	
	// for these, 'pixels' is an array of native-endian u16 or u32, highest bit listed first
	ifmt_xrgb8888, //x bits can be anything and should be ignored (may be copied to other x slots, anything else is illegal)
	ifmt_0rgb8888, //0 bits are always 0
	// ifmt_argb8888, //a=1 is opaque; the rgb values are not premultiplied, a=0 rgb!=0 is allowed (alias for one of the _by formats)
	ifmt_bargb8888, // 'boolean argb'; like argb8888, but only a=00 and a=FF are allowed, anything else is undefined behavior
	// there is no 1rgb8888 format, that should be xrgb
	
	ifmt_rgb565,
	ifmt_xrgb1555,
	ifmt_0rgb1555,
	ifmt_argb1555,
	// ifmt_bargb1555 = ifmt_argb1555, // there are only two possible alphas for 1555, so argb and bargb are the same
	
	// alias formats are down here because C enums are 'special'
	ifmt_argb8888 = (END_LITTLE ? ifmt_bgra8888_by : ifmt_argb8888_by),
	ifmt_bargb1555 = ifmt_argb1555,
	
	// an image is considered 'degenerate' if the full power of the format isn't used
	// for example, an argb with all a=FF is a degenerate xrgb (and a degenerate bargb), and should probably be xrgb instead
	
	// TODO: find a way to ensure unused formats are optimized out
};

// image does not own its storage. If you need memory management, use oimage.
struct image {
	uint32_t width;
	uint32_t height;
	
	imagefmt fmt;
	uint32_t stride; // Distance, in bytes, between the start positions of each row in 'pixels'. The first row starts at *pixels.
	//Must be a multiple of byteperpix(fmt), and must be positive.
	//If stride isn't equal to width * byteperpix(fmt), the padding contains undefined data. Don't access it.
	//Padding, if existent, is not guaranteed to exist past the last scanline; don't try to copy height*stride bytes.
	//The image is not necessarily writable. It's the caller's job to keep track of that.
	
	union {
		void* pixelsv;
		uint8_t* pixels8;
		uint16_t* pixels16;
		uint32_t* pixels32;
	};
	static_assert(sizeof(void*) == sizeof(uint32_t*));
	
	//Converts the image to the given image format.
	void convert(imagefmt newfmt);
	//Like the above, but compile-time fixed formats, to allow dead-code elimination of unused conversions. fmt must be equal to src.
	template<imagefmt src, imagefmt dst> void convert();
	//Inserts the given image at the given coordinates. If that would place the new image partially outside the target,
	// the excess pixels are ignored.
	//Attempting to create impossible values (by, for example, rendering ARGB a=80 into BARGB a=0) is undefined behavior.
	//If source overlaps target, undefined behavior. However, they don't need to be distinct allocations.
	void insert(int32_t x, int32_t y, const image& other);
	//Inserts the subset of the image starting at (offx,offy) continuing for (width,height) pixels.
	//If that's outside 'other', undefined behavior.
	void insert_sub(int32_t x, int32_t y, const image& other, uint32_t offx, uint32_t offy, uint32_t width, uint32_t height)
	{
		image sub;
		sub.init_ref_sub(other, offx, offy, width, height);
		insert(x, y, sub);
	}
	//Repeats the given image for the given size.
	void insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height, const image& other);
	//offx/offy are ignored from the first repetition. Can be less than zero or greater than image size,
	// in which case modulo is applied.
	void insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height,
	                 const image& other, int32_t offx, int32_t offy);
	//Treats the image as nine different images, with cuts at x1/x2/y1/y2.
	//The middle images are repeated until the requested rectangle is covered.
	//If that yields a noninteger number of repetitions, the top/left parts are repeated once more.
	void insert_tile_with_border(int32_t x, int32_t y, uint32_t width, uint32_t height,
	                             const image& other, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2);
	
	//Input color is argb8888 no matter what format the input image is.
	//WARNING: Does not check for overflow.
	void insert_rectangle(int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t argb);
	
	
	//Result is undefined for ifmt_none and unknown formats.
	static uint8_t byteperpix(imagefmt fmt)
	{
		//yes, this code is silly, but compiler gives shitty code for more obvious implementations
		//'magic' is, of course, optimized into a constant
		uint32_t magic = 0;
		
		magic |= (3-1) << (ifmt_rgb888_by*2);
		magic |= (4-1) << (ifmt_rgba8888_by*2);
		magic |= (4-1) << (ifmt_argb8888_by*2);
		magic |= (4-1) << (ifmt_abgr8888_by*2);
		magic |= (4-1) << (ifmt_bgra8888_by*2);
		magic |= (4-1) << (ifmt_xrgb8888*2);
		magic |= (4-1) << (ifmt_0rgb8888*2);
		magic |= (4-1) << (ifmt_bargb8888*2);
		magic |= (2-1) << (ifmt_rgb565*2);
		magic |= (2-1) << (ifmt_xrgb1555*2);
		magic |= (2-1) << (ifmt_0rgb1555*2);
		magic |= (2-1) << (ifmt_argb1555*2);
		
		return 1 + ((magic >> (fmt*2)) & 3);
	}
	
	//If the input image changes after calling one of these three, the callee changes too. Probably better to not do that.
	void init_ptr(const void * pixels, uint32_t width, uint32_t height, size_t stride, imagefmt fmt)
	{
		this->pixelsv = (void*)pixels;
		this->width = width;
		this->height = height;
		this->stride = stride;
		this->fmt = fmt;
	}
	void init_ref(const image& other)
	{
		width = other.width;
		height = other.height;
		fmt = other.fmt;
		stride = other.stride;
		pixelsv = other.pixelsv;
	}
	//Sets *this to a part of other. other may be *this.
	//Going outside the image (x<0, x+width > other.width), or negative sizes (width < 0), is undefined behavior.
	void init_ref_sub(const image& other, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		init_ref(other);
		pixels8 += x*byteperpix(fmt) + y*stride;
		this->width = width;
		this->height = height;
	}
	
	
	// Various parts of the PNG decoder.
	// Read the source code before calling, some of them have some quite peculiar requirements.
	// Or even better, don't call them at all, they're very situational.
	static bool png_defilter(uint8_t * out, size_t out_stride, const uint8_t * in, int bytes_per_pixel, size_t width, size_t height);
	template<int color_type> void png_unpack_rgb(const uint8_t* source, size_t srcstride) const;
	template<int bpp_in> bool png_unpack_plte(const uint8_t* source, size_t srcstride, const uint32_t * palette, uint32_t pallen) const;
	
	//Also used internally.
	template<imagefmt src, imagefmt dst>
	static void convert_scanline(void* out, const void* in, size_t npx);
};

struct oimage : public image {
	autofree<uint8_t> storage;
	
	//0x? and ?x0 images are undefined behavior. The pixels are uninitialized.
	void init_new(uint32_t width, uint32_t height, imagefmt fmt);
	
	//Scaling algorithm is nearest neighbor. Only integral factors are allowed, but negative is fine.
	//other may not be equal to *this, and may not be a part of *this. More technically, *this may not own the memory of other.
	//0x? and ?x0 images are undefined behavior, so neither scalex nor scaley can be zero.
	void init_clone(const image& other, int32_t scalex = 1, int32_t scaley = 1);
	
	//Calls every init_decode_<format> until one succeeds. If none does, returns false; if so, every member should be considered invalid.
	bool init_decode(bytesr data);
	
	//Always emits valid argb8888. May (but is not required to) report bargb or xrgb instead, if it's degenerate.
	//Always emits a packed image, where stride = width*byteperpix.
	bool init_decode_png(bytesr pngdata);
	
	//Calls zero or more platform-specific external libraries to decode the image. Not included in init_decode().
	bool init_decode_extern(bytesr data);
	
	//Calls init_decode(). If that fails, tries init_decode_extern().
	bool init_decode_permissive(bytesr data);
	
	// TODO: unimplemented
	// Always emits 0rgb8888.
	bool init_decode_jpg(bytesr jpgdata); // TODO: delete one of these
	bool init_decode_jpg2(bytesr jpgdata);
};
