#include "string.h"

// splitting this function from string.cpp allows the .a optimization to delete the strstr import

bool strtoken(const char * haystack, const char * needle, char separator)
{
	//token lists are annoyingly complex to parse
	//I suspect 'people using fixed-size buffers, then extension list grows and app explodes'
	// isn't the only reason GL_EXTENSIONS string was deprecated from OpenGL
	size_t nlen = strlen(needle);
	
	while (true)
	{
		const char * found = strstr(haystack, needle);
		if (!found) break;
		
		if ((found==haystack || found[-1]==separator) && // ensure the match is the start of a word
				(found[nlen]==separator || found[nlen]=='\0')) // ensure the match is the end of a word
		{
			return true;
		}
		
		haystack = strchr(found, separator); // try again, could've found GL_foobar_limited when looking for GL_foobar
		if (!haystack) return false;
	}
	return false;
}
