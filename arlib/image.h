#pragma once
#include "global.h"
#include "array.h"
#include "string.h"

enum imagefmt {
	ifmt_none,
	
	//WARNING: only 8888 formats are implemented, do not use the others
	
	ifmt_rgb565, // for most of these, 'pixels' is a reinterpreted array of native-endian u16 or u32, highest bit listed first
	ifmt_rgb888, // exception: this one; it's three u8s, red first
	ifmt_xrgb1555, //x bits can be anything and should be ignored
	ifmt_xrgb8888,
	ifmt_0rgb1555, //0 bits are always 0
	ifmt_0rgb8888,
	ifmt_argb1555, //a=1 is opaque; the rgb values are not premultiplied, and a=0 rgb!=0 is allowed
	ifmt_argb8888, //any non-read-only operation may change rgb values of an a=0 pixel
	ifmt_bargb8888, //'boolean argb'; like argb8888, but only a=00 and a=FF are allowed, anything else is undefined behavior
	
	ifmt_bargb1555 = ifmt_argb1555, // there are only two possible alphas for 1555, so argb and bargb are the same
	
	//an image is considered 'degenerate' if the full power of the format isn't used
	//for example, an argb with all a=1 is a degenerate xrgb (and a degenerate bargb), and the format can safely be set to xrgb
	//this can be intentional if the image is a render target
};

struct font;
struct image : nocopy {
	uint32_t width;
	uint32_t height;
	
	imagefmt fmt;
	size_t stride; // Distance, in bytes, between the start positions of each row in 'pixels'. The first row starts at *pixels.
	//If stride isn't equal to width * byteperpix(fmt), the padding contains undefined data. Never access it.
	//Not necessarily writable. It's the caller's job to keep track of that.
	
	union {
		void* pixelsv;
		uint8_t* pixels8;
		uint16_t* pixels16;
		uint32_t* pixels32;
	};
	static_assert(sizeof(void*) == sizeof(uint32_t*));
	
	//Contains nothing useful, it's for internal memory management only.
	autofree<uint8_t> storage;
	
	//Converts the image to the given image format.
	void convert(imagefmt newfmt);
	//Inserts the given image at the given coordinates. If that would place the new image partially outside the target,
	// the excess pixels are ignored.
	//Attempting to create impossible values (by rendering ARGB a=80 into BARGB a=0) is undefined behavior.
	//If source overlaps target, undefined behavior. However, they don't need to be distinct allocations.
	void insert(int32_t x, int32_t y, const image& other);
	//Inserts the given image, with every pixel turned into a scalex*scaley rectangle, nearest neighbor.
	//Can also mirror the image, by using negative scalex/scaley. Zero is not allowed.
	//WARNING: Does not blend alpha properly. It just copies the source pixels. Do not use with 0rgb target and non-0rgb source.
	//WARNING: Does not check for overflow. If the scaled source doesn't fit in the target,
	//         or the target coordinate is negative, undefined behavior.
	void insert_scale_unsafe(int32_t x, int32_t y, const image& other, int32_t scalex, int32_t scaley);
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
	
	
	//If xspace is nonzero, that many pixels (not multiplied by scale) are added after every space.
	//If align is true, a letter may only start at x + (integer * fnt.scale). If false, anywhere is fine.
	//Returns the width (in pixels) of the widest line.
	uint32_t insert_text(int32_t x, int32_t y, const font& fnt, cstring text, float xspace = 0, bool align = false);
	//If the line is at least width1 pixels, spaces are resized such that the line is approximately width2 pixels.
	//If the line is longer than width2, it overflows.
	void insert_text_justified(int32_t x, int32_t y, uint32_t width1, uint32_t width2,
	                           const font& fnt, cstring text, bool align = false);
	//Automatically inserts linebreaks to ensure everything stays within the given width.
	//Wrapped lines are justified, non-wrapped are left-aligned.
	void insert_text_wrap(int32_t x, int32_t y, uint32_t width, const font& fnt, cstring text);
	
	
	//Result is undefined for ifmt_none and unknown formats.
	static uint8_t byteperpix(imagefmt fmt)
	{
		//yes, this code is silly, but compiler gives shitty code for more obvious implementations
		//'magic' is, of course, optimized into a constant
		uint32_t magic = 0;
		
		magic |= (2-1) << (ifmt_rgb565*2);
		magic |= (3-1) << (ifmt_rgb888*2);
		magic |= (2-1) << (ifmt_xrgb1555*2);
		magic |= (4-1) << (ifmt_xrgb8888*2);
		magic |= (2-1) << (ifmt_0rgb1555*2);
		magic |= (4-1) << (ifmt_0rgb8888*2);
		magic |= (2-1) << (ifmt_argb1555*2);
		magic |= (4-1) << (ifmt_argb8888*2);
		magic |= (2-1) << (ifmt_bargb1555*2);
		magic |= (4-1) << (ifmt_bargb8888*2);
		
		return 1 + ((magic >> (fmt*2)) & 3);
	}
	
	//0x? and ?x0 images are undefined behavior.
	void init_new(uint32_t width, uint32_t height, imagefmt fmt)
	{
		size_t stride = byteperpix(fmt)*width;
		size_t nbytes = stride*height;
		storage = malloc(nbytes);
		init_ptr(storage, width, height, stride, fmt);
	}
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
	//Scaling algorithm is nearest neighbor. Only integral factors are allowed, but negative is fine.
	//other may not be equal to *this, and may not be a part of *this. More technically, *this may not own the memory of other.
	//0x? and ?x0 images are undefined behavior, so neither scalex nor scaley can be zero.
	void init_clone(const image& other, int32_t scalex = 1, int32_t scaley = 1);
	
	//Acts like the applicable init_decode_<fmt>.
	bool init_decode(arrayview<byte> data);
	
	//Always emits valid argb8888. May (but is not required to) report bargb or xrgb instead,
	// if inspecting the image header proves it to be degenerate.
	//Always emits a packed image, where stride = width*byteperpix.
	bool init_decode_png(arrayview<byte> pngdata);
};

struct font {
	//one byte per row, maximum 8 rows
	//byte&1 is leftmost pixel, byte&0x80 is rightmost
	//1 is solid, 0 is transparent
	uint8_t characters[128][8];
	uint8_t width[128];
	uint8_t height;
	
	uint32_t color = 0x000000; // High byte is ignored.
	uint8_t scale = 1;
	
	//Called if told to render characters 00-1F, except 0A (LF). Can draw whatever it wants, or change the font color.
	//If it draws, and the drawn item should have a width, width[ch] should be nonzero after this callback returns.
	function<void(image& out, const font& fnt, int32_t x, int32_t y, uint8_t ch)> fallback;
	
	//Expects spacesize in pixels, and returns the same. text may not contain linebreaks.
	uint32_t measure(cstring text, float spacesize = 0);
	
	//The image must be pure black and white, containing 16x8 tiles of equal size.
	//Each tile must contain one left-aligned symbol, corresponding to its index in the ASCII table.
	//Each tile must be 8x8 or smaller. Maximum allowed image size is 128x64.
	void init_from_image(const image& img);
};
