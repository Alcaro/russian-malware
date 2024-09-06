#include "image.h"
#include "set.h"
#include "bytestream.h"
#include "crc32.h"
#define MINIZ_HEADER_FILE_ONLY
#include "deps/miniz.c"

bytearray image::encode_png()
{
	if (width == 0 || height == 0 || width >= 0x80000000 || height >= 0x80000000)
		return {};
	
	bytestreamw_dyn ret;
	
	map<uint32_t, uint8_t> palette;
	uint32_t palette_idx[256];
	uint32_t transparent = -1u; // -1 - zero transparent colors; -2 - at least one semi-opaque, or multiple transparencies
	bool use_palette = true;
	
	for (uint32_t y=0;y<height;y++)
	for (uint32_t x=0;x<width;x++)
	{
		uint32_t col = pixels[y*stride + x];
		if (use_palette && !palette.contains(col))
		{
			if (palette.size() < 256)
			{
				palette_idx[palette.size()] = col;
				palette.insert(col) = palette.size();
			}
			else
			{
				use_palette = false;
			}
		}
		if (col < 0xFF000000 && transparent != -2u)
		{
			if (col < 0x01000000 && (transparent == -1u || col == transparent))
				transparent = col;
			else
				transparent = -2u;
		}
	}
	
	ret.u8s(0x89,'P','N','G','\r','\n',0x1A,'\n');
	
	size_t chunk_startpos;
	auto chunk_begin = [&ret, &chunk_startpos](const char * type)
	{
		chunk_startpos = ret.size();
		ret.u32b(0);
		ret.u8s(type[0],type[1],type[2],type[3]);
	};
	auto chunk_end = [&ret, &chunk_startpos]()
	{
		ret.u32b_at(chunk_startpos, ret.size() - chunk_startpos - 8);
		ret.u32b(crc32(ret.peek().skip(chunk_startpos + 4)));
	};
	
	uint8_t color_type;
	uint8_t bits_per_channel = 8;
	if (use_palette)
	{
		color_type = 3; // palette
		if (palette.size() <= 16) bits_per_channel = 4;
		if (palette.size() <= 4) bits_per_channel = 2;
		if (palette.size() <= 2) bits_per_channel = 1;
	}
	else if (transparent == -2u)
		color_type = 6; // rgba
	else
		color_type = 2; // rgb
	
	chunk_begin("IHDR");
	ret.u32b(width);
	ret.u32b(height);
	ret.u8(bits_per_channel);
	ret.u8(color_type);
	ret.u8s(0, 0, 0); // compression, filter, interlace (former two must be zero in a legal png, last isn't particularly useful)
	chunk_end();
	
	if (use_palette)
	{
		chunk_begin("PLTE");
		for (size_t n=0;n<palette.size();n++)
		{
			uint32_t col = palette_idx[n];
			ret.u8s(col>>16, col>>8, col>>0); // no ret.u24b
		}
		chunk_end();
	}
	if (use_palette && transparent != -1u)
	{
		chunk_begin("tRNS");
		for (size_t n=0;n<palette.size();n++)
		{
			uint32_t col = palette_idx[n];
			ret.u8(col>>24);
		}
		chunk_end();
	}
	if (color_type == 2 && transparent < 0x80000000)
	{
		chunk_begin("tRNS");
		ret.u16b((transparent>>16) & 255);
		ret.u16b((transparent>>8) & 255);
		ret.u16b((transparent>>0) & 255);
		chunk_end();
	}
	
	bytearray line; // contains an extra zero-value pixel at the start
	bytearray prev_line;
	bytearray filtered_lines[5];
	bytearray to_deflate;
	
	size_t filter_pixel_size = (color_type == 6 ? 4 : color_type == 2 ? 3 : 1);
	size_t line_size = ((filter_pixel_size * bits_per_channel * width) + 7) / 8;
	size_t unfiltered_line_size = filter_pixel_size + line_size;
	size_t line_buf_size = max(unfiltered_line_size, width+8); // extra buffer space for indexed encoding
	line.resize(line_buf_size);
	prev_line.resize(line_buf_size);
	for (int i=0;i<5;i++)
		filtered_lines[i].resize(line_size);
	
	for (uint32_t y=0;y<height;y++)
	{
		uint32_t * line_pixels = pixels + y*stride;
		if (color_type == 3)
		{
			for (uint32_t x=0;x<width;x++)
				line[1+x] = palette.get(line_pixels[x]);
			// I could simd optimize these, but that's unlikely to be the bottleneck so whatever
			if (bits_per_channel == 1)
			{
				for (uint32_t x=0;x<=width/8;x++)
				{
					uint8_t n = 0;
					for (int i=0;i<8;i++)
						n |= (line[1+x*8+i]) << (i^7);
					line[1+x] = n;
				}
			}
			if (bits_per_channel == 2)
			{
				for (uint32_t x=0;x<=width/4;x++)
				{
					uint8_t n = 0;
					n |= (line[1+x*4+0]) << 6;
					n |= (line[1+x*4+1]) << 4;
					n |= (line[1+x*4+2]) << 2;
					n |= (line[1+x*4+3]) << 0;
					line[1+x] = n;
				}
			}
			if (bits_per_channel == 4)
			{
				for (uint32_t x=0;x<=width/2;x++)
				{
					uint8_t n = 0;
					n |= (line[1+x*2+0]) << 4;
					n |= (line[1+x*2+1]) << 0;
					line[1+x] = n;
				}
			}
			if (bits_per_channel == 8)
			{
				// nothing, it's already packed
			}
		}
		if (color_type == 2)
		{
			for (uint32_t x=0;x<width;x++)
			{
				uint32_t col = line_pixels[x];
				line[(1+x)*3+0] = col>>16;
				line[(1+x)*3+1] = col>>8;
				line[(1+x)*3+2] = col>>0;
			}
		}
		if (color_type == 6)
		{
			for (uint32_t x=0;x<width;x++)
			{
				uint32_t col = line_pixels[x];
				line[(1+x)*4+0] = col>>16;
				line[(1+x)*4+1] = col>>8;
				line[(1+x)*4+2] = col>>0;
				line[(1+x)*4+3] = col>>24;
			}
		}
		
		if (color_type != 3)
		{
			uint8_t used_bytes[256] = {};
			uint64_t prediction_distance[5] = {};
			used_bytes[0] |= 1<<0; // the prediction byte itself
			used_bytes[1] |= 1<<1;
			used_bytes[2] |= 1<<2;
			used_bytes[3] |= 1<<3;
			used_bytes[4] |= 1<<4;
			
			for (uint32_t x=0;x<line_size;x++)
			{
				uint8_t by_upleft = prev_line[filter_pixel_size + x - filter_pixel_size];
				uint8_t by_up = prev_line[filter_pixel_size + x];
				uint8_t by_left = line[filter_pixel_size + x - filter_pixel_size];
				uint8_t by_this = line[filter_pixel_size + x];
				
				uint8_t predict_none = 0;
				uint8_t predict_sub = 0;
				uint8_t predict_up = 0;
				uint8_t predict_average = 0;
				
				int a = by_left;
				int b = by_up;
				int c = by_upleft;
				
				int p = a+b-c;
				int pa = abs(p-a);
				int pb = abs(p-b);
				int pc = abs(p-c);
				
				int prediction;
				if (pa <= pb && pa <= pc) prediction = a;
				else if (pb <= pc) prediction = b;
				else prediction = c;
				uint8_t predict_paeth = prediction;
				
				prediction_distance[0] += abs(by_this - predict_none);
				prediction_distance[1] += abs(by_this - predict_sub);
				prediction_distance[2] += abs(by_this - predict_up);
				prediction_distance[3] += abs(by_this - predict_average);
				prediction_distance[4] += abs(by_this - predict_paeth);
				
				filtered_lines[0][x] = by_this; // None
				filtered_lines[1][x] = by_this - by_left; // Sub
				filtered_lines[2][x] = by_this - by_up; // Up
				filtered_lines[3][x] = by_this - (by_left+by_up)/2; // Average
				filtered_lines[4][x] = by_this - prediction; // Paeth
				
				used_bytes[filtered_lines[0][x]] |= 1<<0;
				used_bytes[filtered_lines[1][x]] |= 1<<1;
				used_bytes[filtered_lines[2][x]] |= 1<<2;
				used_bytes[filtered_lines[3][x]] |= 1<<3;
				used_bytes[filtered_lines[4][x]] |= 1<<4;
			}
			
			size_t num_distinct_bytes[5] = {};
			for (size_t n=0;n<256;n++)
			{
				if (used_bytes[n] & (1<<0)) num_distinct_bytes[0]++;
				if (used_bytes[n] & (1<<1)) num_distinct_bytes[1]++;
				if (used_bytes[n] & (1<<2)) num_distinct_bytes[2]++;
				if (used_bytes[n] & (1<<3)) num_distinct_bytes[3]++;
				if (used_bytes[n] & (1<<4)) num_distinct_bytes[4]++;
			}
			
			int best_filter = 0;
			for (int i=0;i<5;i++)
			{
				if (num_distinct_bytes[i] < num_distinct_bytes[best_filter])
					best_filter = i;
				if (num_distinct_bytes[i] == num_distinct_bytes[best_filter] && prediction_distance[i] < prediction_distance[best_filter])
					best_filter = i;
			}
			
			to_deflate.append(best_filter);
			to_deflate += filtered_lines[best_filter];
			
			bytearray tmp = std::move(line);
			line = std::move(prev_line);
			prev_line = std::move(tmp);
		}
		else
		{
			to_deflate.append(0);
			to_deflate += line.slice(filter_pixel_size, line_size);
		}
	}
	
	chunk_begin("IDAT");
	size_t buf_len;
	void* buf = tdefl_compress_mem_to_heap(to_deflate.ptr(), to_deflate.size(), &buf_len,
	                                       (int)TDEFL_DEFAULT_MAX_PROBES|TDEFL_WRITE_ZLIB_HEADER);
	if (!buf) return {}; // out of mem probably
	ret.bytes(bytesr((uint8_t*)buf, buf_len));
	free(buf);
	chunk_end();
	
	chunk_begin("IEND");
	chunk_end();
	
	return ret.finish();
}

#include "test.h"
#include "file.h"

test("png encoder", "array,imagebase,file", "png")
{
	test_skip("kinda slow");
	if (RUNNING_ON_VALGRIND)
		test_skip_force("tdefl uses uninitialized data");
	
	array<string> tests = file::listdir("test/png/");
	assert_gt(tests.size(), 100); // make sure the tests exist, no vacuous truths allowed
	
	for (size_t i=0;i<tests.size();i++)
	{
		if (tests[i].endswith("-n.png") || !tests[i].endswith(".png")) continue;
		
		testctx(tests[i]) {
			string testname = tests[i].replace("/test/png/", "/test/png/reference/");
			
			oimage ref = image::decode_png(file::readall(testname));
			if (!ref.pixels)
				continue;
			bytearray encoded = ref.encode_png();
			oimage decoded = image::decode_png(encoded);
			assert(decoded.pixels);
			assert_eq(decoded.width, ref.width);
			assert_eq(decoded.height, ref.height);
			
			uint32_t* imp = decoded.pixels;
			uint32_t* refp = ref.pixels;
			
			for (size_t i=0;i<ref.height*ref.width;i++)
			{
				if (refp[i]&0xFF000000) assert_eq(imp[i], refp[i]);
				else assert_eq(imp[i]&0xFF000000, 0);
			}
		}
	}
}
