#ifdef ARLIB_SOCKET
#include "runloop2.h"
#include "file.h"
#include "http.h"

async<bytearray> file2::readall_full(cstrnul uri)
{
	if (uri.startswith("http://") || uri.startswith("https://"))
		co_return (co_await http_t::get(uri)).body();
	if (uri.startswith("file:///"))
	{
#ifdef __unix__
		const char * discard_prefix = "file://";  // uris look like file:///bin/sh
#endif
#ifdef _WIN32
		const char * discard_prefix = "file:///"; // uris look like file:///C:/windows/notepad.exe
#endif
		co_return file2::readall(http_t::urldecode(uri.substr_nul(strlen(discard_prefix))));
	}
	co_return file2::readall(uri);
}

co_test("readall", "runloop,http,file", "")
{
	assert_eq(cstring(co_await file2::readall_full("arlib/arlib.h")).substr(0, 12), "#pragma once");
#ifdef __unix__
	assert_eq(cstring(co_await file2::readall_full("/bin/sh")).substr(0, 4), "\x7F""ELF");
	assert_eq(cstring(co_await file2::readall_full("file:///bin%2fsh")).substr(0, 4), "\x7F""ELF");
#endif
#ifdef _WIN32
	assert_eq(cstring(co_await file2::readall_full("C:/windows/notepad.exe")).substr(0, 2), "MZ");
	assert_eq(cstring(co_await file2::readall_full("file:///C:%2Fwindows%2fnotepad.exe")).substr(0, 2), "MZ");
#endif
	assert_eq((co_await file2::readall_full("file://arlib/arlib.h")).size(), 0); // absolute paths only
	test_skip("kinda slow");
	assert_eq(cstring(co_await file2::readall_full("https://floating.muncher.se/arlib/test.txt")), "hello world");
}
#endif
