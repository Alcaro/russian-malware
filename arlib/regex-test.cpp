#include "regex.h"

#if __cplusplus > 201703
#error "switch to string literal as template parameter, it's legal now"
#error "add some concepts, for type checking; if it hurts performance, undo it"
#endif

#ifdef ARLIB_TEST // discard this if not testing, for compile time reasons
#include "test.h"

template<typename Tres, typename... Args>
static void testfn(const char * re, const char * input, Tres result, Args... args)
{
	const char * exp_capture_raw[] = { args... };
	string exp_capture;
	string capture;
	for (size_t n=0;n<sizeof...(args);n++)
	{
		if (n) exp_capture += "/";
		exp_capture += (exp_capture_raw[n] ? exp_capture_raw[n] : "(null)");
	}
	
	for (size_t n=0;n<result.size;n++)
	{
		if (n) capture += "/";
		auto actual = result[n];
		if (actual.start && actual.end) capture += cstring(arrayview<char>(actual.start, actual.end-actual.start));
		else if (!actual.start && !actual.end) capture += "(null)";
		else capture += "<ERROR>"; // probably caused by something returning neither null nor next submatch
	}
	
	testctx(re)
		assert_eq(capture, exp_capture);
}
#define test1(exp, input, ...) testcall(testfn(exp, input, REGEX(exp).match(input), __VA_ARGS__))

test("regex", "string", "regex")
{
	//REGEX_DEBUG_STR("abc");
	//REGEX_DEBUG_TREE("abc");
	//REGEX_DEBUG_FLATIN("abc");
	//REGEX_DEBUG_FLAT("abc");
	//REGEX_DEBUG_FLAT("[a-\\s]");
	//REGEX_DEBUG_FLAT("(abc)?\\1");
	//REGEX_DEBUG_FLAT("([ab])*");
	//REGEX_DEBUG_FLAT("((a)|(b))+");
	
	//REGEX_DEBUG_FLAT("(?:a){5}");
	//REGEX_DEBUG_FLAT("(?:a){0,5}");
	
	//REGEX_DEBUG_FLAT("(a)|(b)|(c)");
	
	test1("abc", "abc", "abc");
	test1("abc", "abcd", "abc");
	test1("(ab)c", "abc", "abc", "ab");
	test1("abc", "def", nullptr);
	test1("[Aa]", "A", "A");
	test1("[Aa][Bb][Cc]", "Abc", "Abc");
	test1("[Aa][Bb][Cc]", "bcd", nullptr);
	test1("\xC3[\xB8\x98]", "ø", "ø");
	test1("(a){5}", "aaaaaa", "aaaaa", "a");
	test1("(abc|def)", "abcx", "abc", "abc");
	test1("(abc|def)", "defx", "def", "def");
	test1("(abc|def)", "ghix", nullptr, nullptr);
	test1("(abc|abcd)de", "abcde",  "abcde",  "abc");
	test1("(abc|abcd)de", "abcdde", "abcdde", "abcd");
	test1("(abcd|abc)de", "abcde",  "abcde",  "abc");
	test1("(abcd|abc)de", "abcdde", "abcdde", "abcd");
	test1("(abc|def)(ghi|jkl)", "abcghix", "abcghi", "abc", "ghi");
	test1("(abc|def)(ghi|jkl)", "abcjklx", "abcjkl", "abc", "jkl");
	test1("(abc|def)(ghi|jkl)", "defghix", "defghi", "def", "ghi");
	test1("(abc|def)(ghi|jkl)", "defjklx", "defjkl", "def", "jkl");
	test1("(abc|def)(ghi|jkl)", "abcdef", nullptr, nullptr, nullptr);
	test1("(abc|def)(ghi|jkl)", "abcgkl", nullptr, nullptr, nullptr);
	test1("(abc)?\\1", "", "", nullptr);
	test1("(abc)?\\1", "abc", "", nullptr);
	test1("(abc)?\\1", "abcabc", "abcabc", "abc");
	test1("([ab])*", "ab", "ab", "b");
	test1("([ab])*", "a", "a", "a");
	test1("([ab])*", "", "", nullptr);
	test1("([ab])+?c", "abc", "abc", "b");
	test1("((.)\\2){3}", "aabbccddeeff", "aabbcc", "cc", "c");
	test1("((.)\\2){2,4}", "aabbcc", "aabbcc", "cc", "c");
	test1("((.)\\2){2,4}?", "aabbcc", "aabb", "bb", "b");
	test1("((.)..)+", "12345678", "123456", "456", "4");
	test1("((.)..)+...", "12345678", "123456", "123", "1");
	test1("((.)..){1,5}", "12345678", "123456", "456", "4");
	test1("((.)..){1,5}...", "12345678", "123456", "123", "1");
	test1("((?=(.b)))a", "ab", "a", "", "ab");
	test1("((?!(.b)))a", "ab", nullptr, nullptr, nullptr);
	test1("((?=(.b)))a", "ac", nullptr, nullptr, nullptr);
	test1("((?!(.b)))a", "ac", "a", "", nullptr);
	test1("(?!(.)\\1)a", "ab", "a", nullptr);
	test1("(?!(.)\\1)a", "aa", nullptr, nullptr);
	test1("\\b.\\b.\\B", "a+", "a+");
	test1("\\B.\\b.\\b", "+a", "+a");
	test1(".\\b.", "++", nullptr);
	test1(".\\b.", "aa", nullptr);
	test1("\\b.", "+", nullptr);
	test1("\\B.", "a", nullptr);
	test1(".\\b", "+", nullptr);
	test1(".\\B", "a", nullptr);
	test1("((a)|(b))+", "ab", "ab", "b", nullptr, "b");
	test1("((a)|(b))+", "ba", "ba", "a", "a", nullptr);
	test1("((a)|(b)){2}", "ab", "ab", "b", nullptr, "b");
	test1("((a)|(b)){2}", "ba", "ba", "a", "a", nullptr);
	test1("((a)\\2|(b)\\3){2}", "aabb", "aabb", "bb", nullptr, "b");
	test1("((a)\\2|(b)\\3){2}", "bbaa", "bbaa", "aa", "a", nullptr);
	
	assert_eq((cstring)(REGEX("bc").search("abc")[0].start), "bc");
	assert_eq(REGEX("f(oo)").replace("foofoobarfoo", "\\1"), "oooobaroo");
	
	assert_eq(cstring("foo bar baz").csplit(REGEX("\\b|a")).join(","), "foo, ,b,r, ,b,z");
	assert_eq(cstring("foo bar baz").csplit<1>(REGEX(" |(?=\\n)")).join(","), "foo,bar baz");
	assert_eq(cstring("foo\nbar baz").csplit<1>(REGEX(" |(?=\\n)")).join(","), "foo,\nbar baz");
}
#endif
