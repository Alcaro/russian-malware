#include "image.h"

//returns undefined for in=0
static int log2(uint32_t in)
{
#ifdef __GNUC__
	static_assert(sizeof(uint32_t) == sizeof(unsigned));
	// keep it as ^ despite - making more sense, gcc optimizes 31^clz way better than 31-clz
	return 31 ^ __builtin_clz(in);
#else
	int ret = -1;
	if (in >= 0x00010000) { ret += 16; in >>= 16; }
	if (in >= 0x00000100) { ret += 8; in >>= 8; }
	if (in >= 0x00000010) { ret += 4; in >>= 4; }
	if (in >= 0x00000004) { ret += 2; in >>= 2; }
	//at this point, in is 1 2 or 3 (0 is UB), so let's make use of that fact
	return ret + in>>1 + 1;
#endif
}

uint32_t image::insert_text(int32_t x, int32_t y, const font& fnt, cstring text, float xspace, bool align)
{
	uint32_t color;
	if (fmt == ifmt_0rgb8888) color = fnt.color & 0x00FFFFFF;
	else color = fnt.color | 0xFF000000;
	
	uint32_t maxwidth = 0;
	
	float xoff = 0;
	for (size_t i=0;text[i];i++)
	{
		uint8_t ch = text[i];
		int32_t xstart = x + (align ? ((int)xoff/fnt.scale)*fnt.scale : (int)xoff);
		
		if (ch == '\n')
		{
			y += fnt.height * fnt.scale;
			xoff = 0;
			continue;
		}
		else if (ch <= 0x1F)
		{
			fnt.fallback(*this, fnt, xstart, y, ch);
			//refresh the color, in case the fallback changed it
			if (fmt == ifmt_0rgb8888) color = fnt.color & 0x00FFFFFF;
			else color = fnt.color | 0xFF000000;
		}
		else
		{
			for (size_t yp=0;yp<8;yp++)
			{
				for (size_t yps=0;yps<fnt.scale;yps++)
				{
					uint32_t yt = y + yp*fnt.scale + yps;
					if (yt < 0 || yt >= this->height) continue;
					
					uint32_t* targetpx = this->pixels32 + yt*this->stride/sizeof(uint32_t);
					
					for (size_t xp=0;xp<8;xp++)
					{
						if (fnt.characters[ch][yp] & (1<<xp))
						{
							for (size_t xps=0;xps<fnt.scale;xps++)
							{
								int32_t xt = xstart + xp*fnt.scale + xps;
								if (xt < 0 || (uint32_t)xt >= this->width) continue;
								
								targetpx[xt] = color;
							}
						}
					}
				}
			}
		}
		
		xoff += fnt.width[ch] * fnt.scale;
		if (ch == ' ') xoff += xspace;
		
		//-scale to remove the inter-letter spacing
		maxwidth = max(maxwidth, xoff-fnt.scale);
	}
	
	return maxwidth;
}

void image::insert_text_justified(int32_t x, int32_t y, uint32_t width1, uint32_t width2, const font& fnt, cstring text, bool align)
{
	size_t linestart = 0;
	size_t numspaces = 0;
	
	uint32_t linewidth = 0;
	for (size_t i=0;i<=text.length();i++)
	{
		if (i == text.length() || text[i] == '\n')
		{
			linewidth *= fnt.scale;
			
			float spacesize;
			if (linewidth >= width1 && linewidth <= width2) spacesize = ((width2 - linewidth) / (float)numspaces);
			else spacesize = 0;
			
			this->insert_text(x, y, fnt, text.substr(linestart, i), spacesize, align);
			
			y += fnt.height * fnt.scale;
			linestart = i+1;
			linewidth = 0;
			numspaces = 0;
			continue;
		}
		
		if (text[i] == ' ') numspaces++;
		linewidth += fnt.width[(uint8_t)text[i]];
	}
}

void image::insert_text_wrap(int32_t x, int32_t y, uint32_t width, const font& fnt, cstring text)
{
	size_t linestart = 0;
	size_t numspaces = 0;
	
	size_t lastspace = 0; // index to the string
	uint32_t widthlastspace = 0; // text width (pixels) from linestart to lastspace
	
	uint32_t linewidth = 0; // text width (pixels) from linestart to i
	
	for (size_t i=0;i<=text.length();i++)
	{
		if (i == text.length() || text[i] == '\n' || linewidth >= width)
		{
			float spacesize = 0;
			if (linewidth >= width)
			{
				i = lastspace;
				linewidth = widthlastspace;
				spacesize = ((width - linewidth) / (float)(numspaces-1));
			}
			
			this->insert_text(x, y, fnt, text.substr(linestart, i), spacesize, true);
			
			y += fnt.height * fnt.scale;
			linestart = i+1;
			linewidth = 0;
			numspaces = 0;
			continue;
		}
		
		if (text[i] == ' ')
		{
			lastspace = i;
			widthlastspace = linewidth;
			numspaces++;
		}
		linewidth += fnt.width[(uint8_t)text[i]] * fnt.scale;
	}
}

void font::init_from_image(const image& img)
{
	memset(characters, 0, sizeof(characters));
	memset(width, 0, sizeof(uint8_t)*32);
	
	int cw = img.width/16;
	int ch = img.height/6;
	
	for (int cy=0;cy<6;cy++)
	for (int cx=0;cx<16;cx++)
	{
		uint8_t charid = 32 + cy*16 + cx;
		
		uint8_t maxbits = 1;
		for (int y=0;y<ch;y++)
		{
			int ty = cy*ch + y;
			uint32_t* pixels = img.pixels32 + img.stride/sizeof(uint32_t)*ty;
			
			uint8_t bits = 0;
			for (int x=0;x<cw;x++)
			{
				int tx = cx*cw + x;
				bits |= !(pixels[tx]&1) << x;
			}
			characters[charid][y] = bits;
			maxbits |= bits;
		}
		
		width[charid] = log2(maxbits)+2; // +1 because maxbits=1 has width 1 but log2(1)=0, +1 because letter spacing
	}
	
	height = ch;
	scale = 1;
}

uint32_t font::measure(cstring text, float spacesize)
{
	float ret = 0;
	for (size_t i=0;i<=text.length();i++)
	{
		if (text[i] == ' ') ret += spacesize;
		ret += width[(uint8_t)text[i]] * scale;
	}
	return ret;
}

#include "test.h"
test("log2", "", "")
{
	//assert_eq(log2(0), -1);
	assert_eq(log2(1), 0);
	assert_eq(log2(2), 1);
	assert_eq(log2(3), 1);
	assert_eq(log2(4), 2);
	assert_eq(log2(7), 2);
	assert_eq(log2(8), 3);
	assert_eq(log2(15), 3);
	assert_eq(log2(16), 4);
	assert_eq(log2(31), 4);
	assert_eq(log2(32), 5);
	assert_eq(log2(63), 5);
	assert_eq(log2(64), 6);
	assert_eq(log2(127), 6);
	assert_eq(log2(128), 7);
	assert_eq(log2(255), 7);
}
