#pragma once
#include "global.h"
#include "array.h"

struct oimage;
// image does not own its storage. If you need memory management, use oimage.
struct image {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t * pixels = nullptr; // In ARGB format, native endian.
	
	// Distance, in 32bit units, between the start positions of each row in 'pixels'. The first row starts at *pixels.
	// If stride isn't equal to width, the padding contains undefined data. Don't access it.
	// Padding, if existent, is not guaranteed to exist past the last scanline; don't try to copy height*stride bytes.
	// The image is not necessarily writable. It's the caller's job to keep track of that.
	// The stride can be negative, to flip the image upsidedown.
	int32_t stride = 0;
	
	uint32_t * operator[](size_t y) { return pixels + y*stride; }
	const uint32_t * operator[](size_t y) const { return pixels + y*stride; }
	
	// Whether the image may contain any pixels with alpha not equal to FF.
	// If false, faster algorithms may be chosen. If true, all alphas may be FF anyways, if the image creator doesn't know that.
	bool has_empty = true;
	// Whether the image may contain any pixels with alpha not equal to FF or 00.
	// Must be false if has_empty is false.
	bool has_alpha = true;
	
	// Inserts the given image at the given coordinates. If that would place the new image partially outside the target,
	//  the excess pixels are ignored.
	// Attempting to create impossible values (by, for example, rendering ARGB a=80 into BARGB a=0) is undefined behavior.
	// If source overlaps target, undefined behavior. However, they don't need to be distinct allocations.
	void insert(int32_t x, int32_t y, const image& other);
	// Like the above, but doesn't check for overflow.
	void insert_noov(int32_t x, int32_t y, const image& other);
	
	image sub(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
	{
		image ret = *this;
		ret.pixels += x + y*stride;
		ret.width = width;
		ret.height = height;
		return ret;
	}
	
	// Mostly used internally. No real point calling it directly.
	static void convert_rgb24_to_argb32(uint32_t* out, const uint8_t* in, size_t npx);
	
	// Calls every init_decode_<format> until one succeeds. If none does, returns false; if so, every member should be considered invalid.
	static oimage decode(bytesr data);
	
	// Always emits valid argb8888. May (but is not required to) report bargb or xrgb instead, if it's degenerate.
	// Always emits a packed image, where stride = width*byteperpix.
	static oimage decode_png(bytesr pngdata);
	
	// Calls zero or more platform-specific external libraries to decode the image. Not included in init_decode().
	static oimage decode_extern(bytesr data);
	
	// Calls init_decode(). If that fails, tries init_decode_extern().
	static oimage decode_permissive(bytesr data);
	
	// TODO: unimplemented
	// Always emits 0rgb8888.
	static oimage decode_jpg(bytesr jpgdata); // TODO: delete one of these
	static oimage decode_jpg2(bytesr jpgdata);
	
	bytearray encode_png(); // Will fail if the image is 0x? or ?x0.
};

struct oimage : public image {
	oimage() { pixels = nullptr; }
private:
	friend struct image;
	oimage(uint32_t width, uint32_t height, uint32_t* pixels, int32_t stride, bool has_empty, bool has_alpha)
	{
		this->width = width;
		this->height = height;
		this->pixels = pixels;
		this->stride = stride;
		this->has_empty = has_empty;
		this->has_alpha = has_alpha;
	}
public:
	oimage(oimage&& other) { image::operator=(other); other.pixels = nullptr; }
	oimage(const oimage& other) = delete;
	oimage& operator=(const image&) = delete;
	oimage& operator=(const oimage&) = delete;
	oimage& operator=(oimage&& other) { free(pixels); image::operator=(other); other.pixels = nullptr; return *this; }
	~oimage() { free(pixels); }
	
	// 0x? and ?x0 images are weird, but legal. The pixels are uninitialized. The image does not contain padding.
	static oimage create(uint32_t width, uint32_t height, bool has_empty, bool has_alpha)
	{
		return { width, height, xmalloc(sizeof(uint32_t)*width*height), (int32_t)width, has_empty, has_alpha };
	}
};
