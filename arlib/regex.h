#pragma once

//Arlib regexes are similar to ECMAScript regexes, with the following differences:
//- It's C++.
//- The regexes are parsed, validated and compiled by the C++ compiler, not at runtime.
//    This makes matching as fast as, or faster than, a JIT, at the cost of increased compile time.
//    (If invalid, the error messages are unavoidably somewhat arcane. I don't think they can be improved much without
//      increasing compile time for valid regexes, or making it harder to debug the parser.)
//- Nonsensical backreferences to an open capture group, into a negative lookahead, or across parallel choices, are undefined behavior.
//    They are not diagnosed; they may silently do the wrong thing at runtime.
//    Examples: (\1), (?!(a))\1, (a)|\1
//    Possible-at-this-point-but-skipped captures are, of course, null, which is treated as empty. Only impossible captures are UB.
//    For negative lookahead, the returned regex match object will always contain a null for that slot.
//- No Unicode, only bytes. \s matches only ASCII space and some control characters; \u1234 does not exist.
//    Byte values 128-255 are considered non-space, non-alphanumeric; \D \S \W will match them.
//    You can put literal UTF-8 strings in your regex, or for complex cases, match UTF-8 byte values using \x or \\x.
//- I don't support the full C++ regex grammar, nor ECMAScript. The following are absent:
//  - Named capture groups; (?<name>foo) (I don't know how to represent that return value in C++)
//  - Named backreferences; \k<name> (compile-time string comparison is painful)
//  - Anything non-byte; \u (incompatible with my byte-oriented approach, especially in [a-z])
//  - Unicode properties; \p \P (would require large lookup tables, bloating compile time for dubious benefit)
//  - Regex traits; hardcoded to ASCII only (hard to optimize, and of little or no use without Unicode properties)
//  - Named character classes, collation, and equivalence; [[:digit:]], [[.tilde.]], [[=a=]] (pointless without regex traits)
//  - Exceptions; bad regexes yield compile errors, no successfully-compiled input will report error (though some will loop forever)
//  - Backwards compatibility extensions; \7 is not BEL if the 7th capture group isn't seen yet, and {a} is an error, not literal chars.
//- If multiple options are possible, lookbehind makes no guarantees on what's captured. (I don't know what they do in ECMAScript.)
//- Repeating a blank match will repeat it forever. ()+ will overflow the backtracking stack and explode.
//- Lookbehind is currently not implemented. (TODO: fix - but may want to wait for better c++20 and consteval)
//None of the C++-specific changes are present. No regex traits, no exceptions, no named character classes.
//This is a backtracking regex engine; as such, the usual caveats about catastrophic backtracking apply.

//It's also similar to https://github.com/hanickadot/compile-time-regular-expressions/; the main differences are
//- Our implementations are completely different, of course.
//- Mine compiles faster, though we're both pretty slow. Mine seems to inline less; smaller code, but probably slower.
//- Caller syntax is different; instead of user defined literals or constexpr variables, I have a REGEX(".*") macro.
//    But it too has drawbacks, namely a max regex length. We'll both be happier once C++20 string template arguments arrives.
//- We support slightly different regex flavors. For example, hanickadot supports (?<name>foo) and \u, while I support \b.
//- Mine doesn't use consteval, or any other post-C++11 features.
//- There's no way to ask mine if a regex is valid; bad ones are unconditionally a compile error.
//- I only support bytes. No UTF-8, and especially no UTF-16.

//TODO:
//- optimize bridges
//- optimize greedy repeat to possessive, if no possible first char of the repeated part can match what's next
//    if 'next' is the finish instruction, always possessive
//    if 'next' is poorly defined, don't optimize
//- compare runtime performance to CTRE
//- figure out why (bool)REGEX(".*").match(foo) isn't unconditionally true
//- check how (bool)REGEX(".*a.*b.*c.*").match(foo) optimizes under both
//- figure out why bool x(char* y) { return REGEX(".*?").match(y); } on gcc 7.4.0 -O2/s/3 emits an unused function containing ud2
//- figure out if (?:\1([ab]))+ should be error, match aab but not abb, or something else
//- pass capture sets forwards, not just outwards, in the flattener; reject backreferences to anything not-yet-defined

#if defined(__has_include) && __has_include("arlib.h")
#define HAVE_ARLIB
#include "string.h"
#endif
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

// Most of this namespace is considered private. Callers should only use the REGEX macro, regex::match_t<>, and regex::pair.
// It's okay to keep variables of type regex::matcher<>, but they should be declared as function-local auto.
// I could put the others in a detail namespace, but that'd make the already-annoying error messages even longer.
// I could detail it in release builds, but that wouldn't accomplish anything useful.
namespace regex {
#ifdef __OPTIMIZE__ // discard the templates from object files; don't do this in debug builds, it bloats the error messages
namespace {
#endif

template<size_t min, size_t max> struct range_t; // used for char_class and repeat

template<uint8_t... chars> struct literal; // This one can be implemented as a sequence of char_class, but this is faster to process.
template<typename... ranges> struct char_class; // Ranges are left-inclusive, right-exclusive, 0-256. They can be empty (min=max).

template<typename... children> struct sequence;
template<typename Ta, typename Tb> struct choice; // choice<choice<x, y>, z> is a bad idea, should be choice<x, choice<y, z>>

// greed: 0-lazy, 1-greedy, 2-possessive (possessive never backtracks, it fails immediately)
// don't turn into enum, gcc errors just say '(regex::greed_t)1'
template<int greed, typename range, typename child> struct repeat;

template<size_t n, bool start> struct capture_edge;
template<size_t n> struct backreference;

template<bool is_end> struct anchor; // ^, $
template<bool is_boundary> struct word_boundary; // \b, \B
template<bool should_match, bool is_behind, typename child> struct lookahead;

using nothing = sequence<>; // Shortcuts for 'blank string' and 'does not match', respectively.
using impossible = char_class<>;


namespace parser {

template<char... chars> struct str;

template<char expected, typename chars> struct str_match;
template<size_t prior, bool head_is_digit, typename chars> struct parse_num;

template<char expected, char... tail> struct str_match<expected, str<expected, tail...>> {
	using content = str<tail...>;
};

template<size_t prior, char head, char next, char... tail> struct parse_num<prior, true, str<head, next, tail...>> {
	static_assert('0' <= head && head <= '9');
	
	using content = parse_num<prior*10 + head-'0', ('0' <= next && next <= '9'), str<next, tail...>>;
	
	static const size_t out_num = content::out_num;
	using out_tail = typename content::out_tail;
};
template<size_t prior, char... tail> struct parse_num<prior, false, str<tail...>> {
	static const size_t out_num = prior;
	using out_tail = str<tail...>;
};


// An optimized character class is one where all numbers in the ranges are in strictly ascending order.
// char_class<range_t<1,2>,range_t<3,4>> is optimized. <<3,4>,<1,2>> and <<1,3>,<3,4>> are not.
template<typename cclass> struct optimize_class;

template<bool again, typename cclass, typename... tail> struct optimize_class_inner;
template<bool again, typename... sorted, size_t low1, size_t high1, size_t low2, size_t high2, typename... tail>
struct optimize_class_inner<again, char_class<sorted...>, range_t<low1, high1>, range_t<low2, high2>, tail...> {
	static const bool compress = (high1 >= low2 && high2 >= low1);
	static const bool swap = (low2 < low1);
	
	static const size_t nlow1  = (swap ? low2  : low1);
	static const size_t nhigh1 = (swap ? high2 : high1);
	static const size_t nlow2  = (swap ? low1  : low2);
	static const size_t nhigh2 = (swap ? high1 : high2);
	
	static const bool nagain = (again || compress || swap);
	
	// using 256 instead of 257 would make more sense, but trying that makes 'compress' true for the last one if
	// \xFF matches (i.e. most negative ranges), which gives weird results
	using new1 = range_t<nlow1, compress?nhigh2:nhigh1>;
	using new2 = range_t<compress?257:nlow2, compress?257:nhigh2>;
	
	using content = typename optimize_class_inner<nagain, char_class<sorted..., new1>, new2, tail...>::content;
};
template<bool again, typename... sorted, size_t skip, size_t low, size_t high, typename... tail>
struct optimize_class_inner<again, char_class<sorted...>, range_t<skip, skip>, range_t<low, high>, tail...> {
	using content = typename optimize_class_inner<true, char_class<sorted...>, range_t<low, high>, tail...>::content;
};
template<typename... sorted>
struct optimize_class_inner<false, char_class<sorted...>, range_t<257, 257>> {
	using content = char_class<sorted...>;
};
template<typename... sorted>
struct optimize_class_inner<true, char_class<sorted...>, range_t<257, 257>> {
	using content = typename optimize_class_inner<false, char_class<>, sorted..., range_t<257, 257>>::content;
};

template<typename... ranges> struct optimize_class<char_class<ranges...>> {
	using content = typename optimize_class_inner<false, char_class<>, ranges..., range_t<257, 257>>::content;
};


// Requires that the input is optimized. Output is optimized if and only if \x00 and \xFF do not match the input.
template<typename cclass> struct invert_class;

template<typename ranges, size_t start, typename... tail> struct invert_class_inner;
template<typename... ranges> struct invert_class<char_class<ranges...>> {
	using content = typename invert_class_inner<char_class<>, 0, ranges...>::content;
};
template<typename... ranges, size_t start, size_t min, size_t max, typename... tail>
struct invert_class_inner<char_class<ranges...>, start, range_t<min, max>, tail...> {
	using content = typename invert_class_inner<char_class<ranges..., range_t<start, min>>, max, tail...>::content;
};
template<typename... ranges, size_t start> struct invert_class_inner<char_class<ranges...>, start> {
	using content = char_class<ranges..., range_t<start, 256>>;
};


template<size_t n_capture, typename chars> struct parse_term;
template<typename inner, typename chars> struct parse_repeat;
template<size_t n_capture, typename chars> struct parse_sequence;
template<size_t n_capture, typename chars> struct parse_disjunction;

template<size_t left, typename chars> struct parse_repeat_next;
template<typename range, typename child, typename chars> struct parse_repeat_type;

template<size_t n_capture, bool is_digit, typename chars> struct parse_backslash;
template<typename prev, typename chars> struct parse_class;

template<typename child> struct optimizer { using result = child; };
template<typename child> using optimize = typename optimizer<child>::result;

template<typename a> struct optimizer<sequence<a, nothing>> { using result = a; };
template<typename... a, typename b> struct optimizer<sequence<sequence<a...>, b>> { using result = optimize<sequence<a..., b>>; };
template<typename a, typename... b> struct optimizer<sequence<a, sequence<b...>>> { using result = optimize<sequence<a, b...>>; };
// if multiple of the above would match simultaneously, compiler whines; define the combinations too
template<typename... a, typename... b> struct optimizer<sequence<sequence<a...>, sequence<b...>>> {
	using result = optimize<sequence<a..., b...>>;
};
template<typename... a> struct optimizer<sequence<sequence<a...>, nothing>> { using result = optimize<sequence<a...>>; };

template<typename a> struct optimizer<choice<a, impossible>> { using result = a; };
template<typename a, typename b, typename c> struct optimizer<choice<choice<a, b>, c>> {
	using result = optimize<choice<a, choice<b, c>>>;
};

template<size_t n> struct optimizer<char_class<range_t<n, n+1>>> { using result = literal<n>; };
template<uint8_t... a, uint8_t... b> struct optimizer<sequence<literal<a...>, literal<b...>>> { using result = literal<a..., b...>; };
template<uint8_t... a, uint8_t... b, typename... c> struct optimizer<sequence<literal<a...>, literal<b...>, c...>> {
	using result = optimize<sequence<literal<a..., b...>, c...>>;
};

template<char in> struct simple_escape {
	static_assert(
		('!' <= in && in <= '/') ||
		(':' <= in && in <= '@') ||
		('[' <= in && in <= '`') ||
		('{' <= in && in <= '~') ||
		(uint8_t)in >= 128 ||
		false);
	using content = char_class<range_t<in, in+1>>;
};
//must use only char_class, to simplify the [\s\S] parser; optimize<> turns it into literal
template<> struct simple_escape<'f'> { using content = char_class<range_t<'\f', '\f'+1>>; };
template<> struct simple_escape<'n'> { using content = char_class<range_t<'\n', '\n'+1>>; };
template<> struct simple_escape<'r'> { using content = char_class<range_t<'\r', '\r'+1>>; };
template<> struct simple_escape<'t'> { using content = char_class<range_t<'\t', '\t'+1>>; };
template<> struct simple_escape<'v'> { using content = char_class<range_t<'\v', '\v'+1>>; };
template<> struct simple_escape<'0'> { using content = char_class<range_t<'\0', '\0'+1>>; }; // \0 and \b are only used
template<> struct simple_escape<'b'> { using content = char_class<range_t<'\b', '\b'+1>>; }; // by character classes

template<> struct simple_escape<'d'> { using content = char_class<range_t<'0', '9'+1>>; };
template<> struct simple_escape<'D'> { using content = typename invert_class<typename simple_escape<'d'>::content>::content; };
template<> struct simple_escape<'s'> { using content = char_class<range_t<0x09, 0x0D+1>, range_t<' ', ' '+1>>; };
template<> struct simple_escape<'S'> { using content = typename invert_class<typename simple_escape<'s'>::content>::content; };
template<> struct simple_escape<'w'> { using content = char_class<range_t<'0', '9'+1>, range_t<'A', 'Z'+1>,
                                                                  range_t<'_', '_'+1>, range_t<'a', 'z'+1>>; };
template<> struct simple_escape<'W'> { using content = typename invert_class<typename simple_escape<'w'>::content>::content; };


template<size_t n_capture, char head, char... tail> struct parse_term<n_capture, str<head, tail...>> {
	static_assert(
		(uint8_t)head >= 32, "invalid literal character - did you type \\1 instead of \\\\1?");
	static_assert(
		(' ' <= head && head <= '#') ||
		('%' <= head && head <= '\'') ||
		head == ',' || head == '-' || head == '/' ||
		('0' <= head && head <= '9') ||
		(':' <= head && head <= '>') ||
		head == '@' ||
		('A' <= head && head <= 'Z') ||
		('a' <= head && head <= 'z') ||
		head == '_' || head == '`' || head == '~' ||
		(uint8_t)head >= 128 ||
		(uint8_t)head < 32 || // to not assert false twice
		false, "invalid literal character - did you forget escaping something?");
	
	static const size_t out_n_capture = n_capture;
	using out_tail = str<tail...>;
	using out_tree = literal<(uint8_t)head>;
	static const bool out_repeatable = true;
};

template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', tail...>> {
	using content = parse_disjunction<n_capture+1, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = optimize<sequence<capture_edge<n_capture, true>, typename content::out_tree, capture_edge<n_capture, false>>>;
	static const bool out_repeatable = true;
};

template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', tail...>>;// {
//	static_assert(n_capture < 0); // invalid
//};

template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', ':', tail...>> {
	using content = parse_disjunction<n_capture, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = typename content::out_tree;
	static const bool out_repeatable = true;
};

template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', '=', tail...>> {
	using content = parse_disjunction<n_capture, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = lookahead<true, false, typename content::out_tree>;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', '!', tail...>> {
	using content = parse_disjunction<n_capture, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = lookahead<false, false, typename content::out_tree>;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', '<', '=', tail...>> {
	using content = parse_disjunction<n_capture, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = lookahead<true, true, typename content::out_tree>;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'(', '?', '<', '!', tail...>> {
	using content = parse_disjunction<n_capture, str<'|', tail...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tail = typename str_match<')', typename content::out_tail>::content;
	using out_tree = lookahead<false, true, typename content::out_tree>;
};


template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'^', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<tail...>;
	using out_tree = anchor<false>;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'$', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<tail...>;
	using out_tree = anchor<true>;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'.', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<tail...>;
	using out_tree = char_class<range_t<0,'\n'>, range_t<'\n'+1, 256>>;
	static const bool out_repeatable = true;
};


template<typename a, typename b> struct join_class;
template<typename... as, typename... bs> struct join_class<char_class<as...>, char_class<bs...>> {
	using content = char_class<as..., bs...>;
};
template<typename child> struct extract_char;
template<size_t n> struct extract_char<char_class<range_t<n, n+1>>> {
	static const size_t value = n;
};
template<typename tail> struct parse_class_node; // node - x, \d
template<char head, char... tail> struct parse_class_node<str<head, tail...>> {
	using out_tail = str<tail...>;
	using out_val = char_class<range_t<(uint8_t)head, (uint8_t)head+1>>;
};
template<char head, char... tail> struct parse_class_node<str<'\\', head, tail...>> {
	using out_tail = str<tail...>;
	using out_val = typename simple_escape<head>::content;
};
template<char head, char... tail> struct parse_class_node<str<'\\', 'c', head, tail...>> {
	static_assert(('A' <= head && head <= 'Z') || ('a' <= head && head <= 'z'));
	static const size_t n = head&0x1F;
	
	using out_tail = str<tail...>;
	using out_val = char_class<range_t<n, n+1>>;
};
template<char a, char b, char... tail> struct parse_class_node<str<'\\', 'x', a, b, tail...>> {
	static_assert(
		('0' <= a && a <= '9') ||
		('A' <= a && a <= 'F') ||
		('a' <= a && a <= 'f') ||
		false);
	static_assert(
		('0' <= b && b <= '9') ||
		('A' <= b && b <= 'F') ||
		('a' <= b && b <= 'f') ||
		false);
	
	static const size_t av = (a <= '9' ? a-'0' : (a&0x0F)-1+10);
	static const size_t bv = (b <= '9' ? b-'0' : (b&0x0F)-1+10);
	
	static const size_t n = av*16+bv;
	
	using out_tail = str<tail...>;
	using out_val = char_class<range_t<n, n+1>>;
};
template<typename prev, typename tail> struct parse_class_range { // range - x-y, \n-\r
	using out_tail = tail;
	using out_val = prev;
};
template<size_t prev, char... tail> struct parse_class_range<char_class<range_t<prev, prev+1>>, str<'-', tail...>> {
	using next = parse_class_node<str<tail...>>;
	static const size_t next_val = extract_char<typename next::out_val>::value;
	static_assert(next_val >= prev);
	
	using out_tail = typename next::out_tail;
	using out_val = char_class<range_t<prev, next_val+1>>;
};
template<typename... ranges, char... tail> struct parse_class<char_class<ranges...>, str<tail...>> { // class - a-z0-9
	using first = parse_class_node<str<tail...>>;
	using node = parse_class_range<typename first::out_val, typename first::out_tail>;
	
	using next_cclass = typename join_class<char_class<ranges...>, typename node::out_val>::content;
	using next = parse_class<next_cclass, typename node::out_tail>;
	
	using out_tail = typename next::out_tail;
	using out_tree = typename next::out_tree;
};
template<typename... ranges, char... tail> struct parse_class<char_class<ranges...>, str<']', tail...>> {
	using out_tail = str<tail...>;
	using out_tree = char_class<ranges...>;
};


template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'[', tail...>> {
	using content = parse_class<char_class<>, str<tail...>>;
	
	static const size_t out_n_capture = n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = optimize<typename optimize_class<typename content::out_tree>::content>;
	static const bool out_repeatable = true;
};
template<size_t n_capture, char... tail> struct parse_term<n_capture, str<'[', '^', tail...>> {
	using content = parse_class<char_class<>, str<tail...>>;
	using inv_content = typename invert_class<typename optimize_class<typename content::out_tree>::content>::content;
	
	static const size_t out_n_capture = n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = optimize<typename optimize_class<inv_content>::content>;
	static const bool out_repeatable = true;
};


//separate class for backslash escapes, fewer patterns for parse_term means quicker compile
template<size_t n_capture, char head, char... tail> struct parse_term<n_capture, str<'\\', head, tail...>> {
	using content = parse_backslash<n_capture, ('0' <= head && head <= '9'), str<head, tail...>>;
	
	static const size_t out_n_capture = n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = typename content::out_tree;
	static const bool out_repeatable = content::out_repeatable;
};

template<size_t n_capture, char head, char... tail> struct parse_backslash<n_capture, true, str<'0', head, tail...>> {
	static_assert(head < '0' || head > '9');
	using out_tail = str<head, tail...>;
	using out_tree = literal<'\0'>;
	static const bool out_repeatable = true;
};
template<size_t n_capture, char... tail> struct parse_backslash<n_capture, true, str<tail...>> {
	using content = parse_num<0, true, str<tail...>>;
	static_assert(content::out_num < n_capture);
	
	using out_tail = typename content::out_tail;
	using out_tree = backreference<content::out_num>;
	static const bool out_repeatable = true;
};
template<size_t n_capture, char... tail> struct parse_backslash<n_capture, false, str<'b', tail...>> {
	using out_tail = str<tail...>;
	using out_tree = word_boundary<true>;
	static const bool out_repeatable = false;
};
template<size_t n_capture, char... tail> struct parse_backslash<n_capture, false, str<'B', tail...>> {
	using out_tail = str<tail...>;
	using out_tree = word_boundary<false>;
	static const bool out_repeatable = false;
};

template<size_t n_capture, char head, char... tail> struct parse_backslash<n_capture, false, str<'c', head, tail...>> {
	using out_tail = str<tail...>;
	using out_tree = literal<head&0x1F>;
	static const bool out_repeatable = true;
};
template<size_t n_capture, char a, char b, char... tail> struct parse_backslash<n_capture, false, str<'x', a, b, tail...>> {
	static_assert(
		('0' <= a && a <= '9') ||
		('A' <= a && a <= 'F') ||
		('a' <= a && a <= 'f') ||
		false);
	static_assert(
		('0' <= b && b <= '9') ||
		('A' <= b && b <= 'F') ||
		('a' <= b && b <= 'f') ||
		false);
	
	static const size_t av = (a <= '9' ? a-'0' : (a&0x0F)-1+10);
	static const size_t bv = (b <= '9' ? b-'0' : (b&0x0F)-1+10);
	
	using out_tail = str<tail...>;
	using out_tree = literal<av*16+bv>;
	static const bool out_repeatable = true;
};

template<size_t n_capture, char head, char... tail> struct parse_backslash<n_capture, false, str<head, tail...>> {
	using out_tail = str<tail...>;
	using out_tree = optimize<typename simple_escape<head>::content>;
	static const bool out_repeatable = true;
};


template<typename range, typename child, char... tail> struct parse_repeat_type<range, child, str<tail...>> {
	using out_tail = str<tail...>;
	using out_tree = repeat<1, range, child>;
};
template<typename range, typename child, char... tail> struct parse_repeat_type<range, child, str<'?', tail...>> {
	using out_tail = str<tail...>;
	using out_tree = repeat<0, range, child>;
};

template<typename inner, char... tail> struct parse_repeat<inner, str<tail...>> {
	static const size_t out_n_capture = inner::out_n_capture;
	using out_tail = str<tail...>;
	using out_tree = typename inner::out_tree;
};
template<typename inner, char... tail> struct parse_repeat<inner, str<'?', tail...>> {
	static_assert(inner::out_repeatable);
	
	using content = parse_repeat_type<range_t<0, 1>, typename inner::out_tree, str<tail...>>;
	
	static const size_t out_n_capture = inner::out_n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = typename content::out_tree;
};
template<typename inner, char... tail> struct parse_repeat<inner, str<'*', tail...>> {
	static_assert(inner::out_repeatable);
	
	using content = parse_repeat_type<range_t<0, SIZE_MAX>, typename inner::out_tree, str<tail...>>;
	
	static const size_t out_n_capture = inner::out_n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = typename content::out_tree;
};
template<typename inner, char... tail> struct parse_repeat<inner, str<'+', tail...>> {
	static_assert(inner::out_repeatable);
	
	using content = parse_repeat_type<range_t<1, SIZE_MAX>, typename inner::out_tree, str<tail...>>;
	
	static const size_t out_n_capture = inner::out_n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = typename content::out_tree;
};


template<size_t left, char... tail> struct parse_repeat_next<left, str<tail...>> {
	static const size_t out_num = left;
	using out_tail = str<tail...>;
};
template<size_t left, char... tail> struct parse_repeat_next<left, str<',', tail...>> {
	using content = parse_num<0, true, str<tail...>>;
	
	static const size_t out_num = content::out_num;
	using out_tail = typename content::out_tail;
};
template<size_t left, char... tail> struct parse_repeat_next<left, str<',', '}', tail...>> {
	static const size_t out_num = SIZE_MAX;
	using out_tail = str<'}', tail...>;
};
template<typename inner, char... tail> struct parse_repeat<inner, str<'{', tail...>> {
	static_assert(inner::out_repeatable);
	
	using left = parse_num<0, true, str<tail...>>;
	using right = parse_repeat_next<left::out_num, typename left::out_tail>;
	using right_tail = typename str_match<'}', typename right::out_tail>::content;
	static_assert(left::out_num <= right::out_num);
	
	using content = parse_repeat_type<range_t<left::out_num, right::out_num>, typename inner::out_tree, right_tail>;
	
	static const size_t out_n_capture = inner::out_n_capture;
	using out_tail = typename content::out_tail;
	using out_tree = typename content::out_tree;
};

template<size_t n_capture, char... tail> struct parse_sequence<n_capture, str<tail...>> {
	using first_raw = parse_term<n_capture, str<tail...>>;
	using first = parse_repeat<first_raw, typename first_raw::out_tail>;
	using next_raw = parse_sequence<first::out_n_capture, typename first::out_tail>;
	using next = parse_repeat<next_raw, typename next_raw::out_tail>;
	
	static const size_t out_n_capture = next::out_n_capture;
	using out_tail = typename next::out_tail;
	using out_tree = optimize<sequence<typename first::out_tree, typename next::out_tree>>;
};

template<size_t n_capture, char... tail> struct parse_sequence<n_capture, str<')', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<')', tail...>;
	using out_tree = nothing;
};
template<size_t n_capture, char... tail> struct parse_sequence<n_capture, str<'|', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<'|', tail...>;
	using out_tree = nothing;
};


template<size_t n_capture, char... tail> struct parse_disjunction<n_capture, str<tail...>> {
	using content = parse_sequence<n_capture, str<tail...>>;
	
	static const size_t out_n_capture = n_capture;
	using out_tree = typename content::out_tree;
	using out_tail = typename content::out_tail;
};

template<size_t n_capture, char... tail> struct parse_disjunction<n_capture, str<')', tail...>> {
	static const size_t out_n_capture = n_capture;
	using out_tail = str<')', tail...>;
	using out_tree = impossible;
};
template<size_t n_capture, char... tail> struct parse_disjunction<n_capture, str<'|', tail...>> {
	using first = parse_sequence<n_capture, str<tail...>>;
	using next = parse_disjunction<first::out_n_capture, typename first::out_tail>;
	
	static const size_t out_n_capture = next::out_n_capture;
	using out_tail = typename next::out_tail;
	using out_tree = optimize<choice<typename first::out_tree, typename next::out_tree>>;
};

}



template<size_t... ns> struct capture_set; // a set of captures
template<typename Ta, typename Tb> struct merge_capture;
template<size_t... as, size_t... bs> struct merge_capture<capture_set<as...>, capture_set<bs...>> {
	using result = capture_set<as..., bs...>;
};
template<typename T, size_t n> struct has_capture;
template<size_t a, size_t... as, size_t n>
struct has_capture<capture_set<a, as...>, n> {
	static const bool result = has_capture<capture_set<as...>, n>::result;
};
template<size_t... as, size_t n>
struct has_capture<capture_set<n, as...>, n> {
	static const bool result = true;
};
template<size_t n>
struct has_capture<capture_set<>, n> {
	static const bool result = false;
};

template<typename captures, typename... bridges> struct re_flat;
template<size_t target> struct backup;
template<size_t target> struct jump;
template<typename cap_set> struct capture_delete;
struct accept;
//finish is conceptually the last instruction, but C++ won't let me match bridge<id, Ts..., accept>, so it has to go first
template<size_t id, typename finish, typename... stones> struct bridge;

// A flattened regex consists of a numbered sequence of bridges (made up terminology).
// A bridge is a (potentially empty) sequence of stones, followed by a jump or accept instruction (the exit instruction).
// A stone is a rule that either matches or doesn't, no backtracking needed, i.e. one of
//     literal, char_class, capture_edge, backreference, anchor, word_boundary, lookahead, backup
//   (while lookahead can backtrack internally, you can't backtrack into it, so it's still a stone)
//   (TODO: investigate how often choices contain only literal or char_class; if common, and codegen is poor, treat that as a stone)
// A backup instruction stores a bridge ID, and the current character under consideration, to a stack.
//   TODO: use machine stack first and check performance; return predictor probably means manual stack is slower, but worth trying
// On matching failure, the most recent backup is popped from the stack.

template<size_t n_capture, typename T> struct flat_optimizer;

namespace flatten {
// Any re_flat returned from process<> must allow prefixing stuff to its first bridge. jump<start> and backup<start> are prohibited.

template<typename T> struct extract_captures;
template<typename Tc, typename... Ts> struct extract_captures<re_flat<Tc, Ts...>> {
	using result = Tc;
};

template<typename T1, typename T2> struct prefix;
template<typename T1, typename T2c, size_t n, typename T2f, typename... T2b1, typename... T2bs>
struct prefix<T1, re_flat<T2c, bridge<n, T2f, T2b1...>, T2bs...>> {
	using result = re_flat<T2c, bridge<n, T2f, T1, T2b1...>, T2bs...>;
};
template<typename T2c, size_t n, typename T2f, typename... T2b1, typename... T2bs>
struct prefix<capture_delete<capture_set<>>, re_flat<T2c, bridge<n, T2f, T2b1...>, T2bs...>> {
	using result = re_flat<T2c, bridge<n, T2f, T2b1...>, T2bs...>;
};

template<typename T1, typename T2> struct concat_impl;
template<typename T1c, typename... T1s, typename T2c, typename... T2s>
struct concat_impl<re_flat<T1c, T1s...>, re_flat<T2c, T2s...>> {
	using result = re_flat<typename merge_capture<T1c, T2c>::result, T1s..., T2s...>;
};

template<typename T1, typename T2> using concat = typename concat_impl<T1, T2>::result;
template<typename T, size_t id> using concat_accept = concat<T, re_flat<capture_set<>, bridge<id, accept>>>;

template<typename... Ts> struct replace_accept_inner;
template<typename... To, typename Tr, size_t id, typename... Ts, typename... Tbs>
struct replace_accept_inner<re_flat<To...>, Tr, bridge<id, accept, Ts...>, Tbs...> {
	using result = typename replace_accept_inner<re_flat<To..., bridge<id, Tr, Ts...>>, Tr, Tbs...>::result;
};
template<typename... To, typename Tr, typename Tb, typename... Tbs>
struct replace_accept_inner<re_flat<To...>, Tr, Tb, Tbs...> {
	using result = typename replace_accept_inner<re_flat<To..., Tb>, Tr, Tbs...>::result;
};
template<typename To, typename Tr>
struct replace_accept_inner<To, Tr> {
	using result = To;
};
template<typename Tre, typename Tr> struct replace_accept;
template<typename Tc, typename Tr, typename... Tb>
struct replace_accept<re_flat<Tc, Tb...>, Tr> {
	using result = typename replace_accept_inner<re_flat<Tc>, Tr, Tb...>::result;
};

// Much like regex, C++ template metaprogramming is a write-only language.
template<typename... Ts> struct replace_accept_2_inner;
template<typename... To, typename Tr1, typename Tr2, size_t id, typename... Ts, typename... Tbs>
struct replace_accept_2_inner<re_flat<To...>, Tr1, Tr2, bridge<id, accept, Ts...>, Tbs...> {
	using result = typename replace_accept_2_inner<re_flat<To..., bridge<id, Tr2, Ts..., Tr1>>, Tr1, Tr2, Tbs...>::result;
};
template<typename... To, typename Tr1, typename Tr2, typename Tb, typename... Tbs>
struct replace_accept_2_inner<re_flat<To...>, Tr1, Tr2, Tb, Tbs...> {
	using result = typename replace_accept_2_inner<re_flat<To..., Tb>, Tr1, Tr2, Tbs...>::result;
};
template<typename To, typename Tr1, typename Tr2>
struct replace_accept_2_inner<To, Tr1, Tr2> {
	using result = To;
};
template<typename Tre, typename Tr1, typename Tr2> struct replace_accept_2;
template<typename Tc, typename Tr1, typename Tr2, typename... Tb>
struct replace_accept_2<re_flat<Tc, Tb...>, Tr1, Tr2> {
	using result = typename replace_accept_2_inner<re_flat<Tc>, Tr1, Tr2, Tb...>::result;
};


//most stuff -> leave unchanged
template<size_t start, typename T> struct process {
	using result = re_flat<capture_set<>, bridge<start, accept, T>>;
	static const size_t last_bridge = start;
};

//capture_edge -> set the capture set (only for the closing edge)
template<size_t start, size_t cap_id> struct process<start, capture_edge<cap_id, false>> {
	using result = re_flat<capture_set<cap_id>, bridge<start, accept, capture_edge<cap_id, false>>>;
	static const size_t last_bridge = start;
};


//sequence -> append them
template<typename T1, typename T2> struct combine_seq;
template<typename T1c, size_t n, typename... T1s, typename T2c, typename T2f, typename... T2b1, typename... T2bs>
struct combine_seq<re_flat<T1c, bridge<n, accept, T1s...>>, re_flat<T2c, bridge<n, T2f, T2b1...>, T2bs...>> {
	using result = re_flat<typename merge_capture<T1c, T2c>::result, bridge<n, T2f, T1s..., T2b1...>, T2bs...>;
};
template<typename T1, typename T2c, size_t T2i, typename... T2s, typename... T2bs>
struct combine_seq<T1, re_flat<T2c, bridge<T2i, T2s...>, T2bs...>> {
	using part1 = typename replace_accept<T1, jump<T2i>>::result;
	using result = concat<part1, re_flat<T2c, bridge<T2i, T2s...>, T2bs...>>;
};
template<size_t start, typename T1, typename... Ts> struct process<start, sequence<T1, Ts...>> {
	using first = process<start, T1>;
	using next = process<first::last_bridge + (start != first::last_bridge), sequence<Ts...>>;
	
	using result = typename combine_seq<typename first::result, typename next::result>::result;
	static const size_t last_bridge = next::last_bridge;
};
template<size_t start> struct process<start, sequence<>> {
	using result = re_flat<capture_set<>, bridge<start, accept>>;
	static const size_t last_bridge = start;
};


// choice a(b|c)d ->
//   0: 'a', backup<1>, 'b', jump<2>
//   1: 'c', jump<2>
//   2: 'd', accept
template<size_t start, typename T1, typename T2> struct process<start, choice<T1, T2>> {
	using first = process<start, T1>;
	using next = process<first::last_bridge+1, T2>;
	using first_backup = typename prefix<backup<first::last_bridge+1>, typename first::result>::result;
	
	using first_cap = typename extract_captures<typename first::result>::result;
	using next_cap = typename extract_captures<typename next::result>::result;
	
	// TODO: this should only be done if this choice is inside a repeat(max > 1)
	using first_out = typename prefix<capture_delete<next_cap>, first_backup>::result;
	using next_out = typename prefix<capture_delete<first_cap>, typename next::result>::result;
	
	using result = concat<first_out, next_out>;
	static const size_t last_bridge = next::last_bridge;
};


// a? ->
//   0: backup<1>, 'a', jump<1>
//   1: accept
template<size_t start, typename T> struct process<start, repeat<1, range_t<0,1>, T>> {
	using inner = process<start, T>;
	using inner_prefix = typename prefix<backup<inner::last_bridge+1>, typename inner::result>::result;
	using result = concat_accept<inner_prefix, inner::last_bridge+1>;
	static const size_t last_bridge = inner::last_bridge+1;
};
// a* ->
//   0: jump<1>
//   1: backup<2>, 'a', jump<1>
//   2: accept
template<size_t start, typename T> struct process<start, repeat<1, range_t<0,SIZE_MAX>, T>> {
	using part2 = process<start+1, T>;
	using part2_prefix = typename prefix<backup<part2::last_bridge+1>, typename part2::result>::result;
	using part2_loop = typename replace_accept<part2_prefix, jump<start+1>>::result;
	using part1 = re_flat<capture_set<>, bridge<start, jump<start+1>>>;
	
	using result = concat<part1, concat_accept<part2_loop, part2::last_bridge+1>>;
	static const size_t last_bridge = part2::last_bridge+1;
};
// a+ ->
//   0: jump<1>
//   1: 'a', backup<2>, jump<1>
//   2: accept
template<size_t start, typename T> struct process<start, repeat<1, range_t<1,SIZE_MAX>, T>> {
	using part2 = process<start+1, T>;
	using part2_loop = typename replace_accept_2<typename part2::result, backup<part2::last_bridge+1>, jump<start+1>>::result;
	using part1 = re_flat<capture_set<>, bridge<start, jump<start+1>>>;
	
	using result = concat<part1, concat_accept<part2_loop, part2::last_bridge+1>>;
	static const size_t last_bridge = part2::last_bridge+1;
};
// a{0,5} ->
// 0: backup<1>, 'a', jump<1>
// 1: accept
// + process<a{0,4}>
template<size_t start, size_t maxrepeat, typename T> struct process<start, repeat<1, range_t<0,maxrepeat>, T>> {
	using first = process<start, T>;
	using next = process<first::last_bridge+1, repeat<1, range_t<0, maxrepeat-1>, T>>;
	using first_prefix = typename prefix<backup<next::last_bridge+1>, typename first::result>::result;
	using first_fragment = typename replace_accept<first_prefix, jump<first::last_bridge+1>>::result;
	using result = concat_accept<concat<first_fragment, typename next::result>, next::last_bridge+1>;
	static const size_t last_bridge = next::last_bridge+1;
};
// a{5,10} and a{5,10}? ->
// 0: 'a', jump<1>
// 1: accept
// + process<a{4,9}> (5,5 is same)
template<size_t start, int greed, size_t min, size_t max, typename T> struct process<start, repeat<greed, range_t<min,max>, T>> {
	using first = process<start, T>;
	using next = process<first::last_bridge+1, repeat<greed, range_t<min-1, max-(max!=SIZE_MAX)>, T>>;
	using first_fragment = typename replace_accept<typename first::result, jump<first::last_bridge+1>>::result;
	using result = concat<first_fragment, typename next::result>;
	static const size_t last_bridge = next::last_bridge;
};
// a{0,0} -> 0: accept
template<size_t start, typename T> struct process<start, repeat<1, range_t<0,0>, T>> {
	using result = re_flat<capture_set<>, bridge<start, accept>>;
	static const size_t last_bridge = start;
};


// a?? ->
//   0: backup<1>, jump<2>
//   1: 'a', jump<2>
//   2: accept
template<size_t start, typename T> struct process<start, repeat<0, range_t<0,1>, T>> {
	using part2 = process<start+1, T>;
	using part1 = re_flat<capture_set<>, bridge<start, jump<part2::last_bridge+1>, backup<start+1>>>;
	using part2_real = typename replace_accept<typename part2::result, jump<part2::last_bridge+1>>::result;
	
	using result = concat<part1, concat_accept<part2_real, part2::last_bridge+1>>;
	static const size_t last_bridge = part2::last_bridge+1;
};
// a*? ->
//   0: backup<1>, jump<2>
//   1: 'a', backup<1>, jump<2>
//   2: accept
template<size_t start, typename T> struct process<start, repeat<0, range_t<0,SIZE_MAX>, T>> {
	using part2 = process<start+1, T>;
	using part1 = re_flat<capture_set<>, bridge<start, jump<part2::last_bridge+1>, backup<start+1>>>;
	using part2_real = typename replace_accept_2<typename part2::result, backup<start+1>, jump<part2::last_bridge+1>>::result;
	
	using result = concat<part1, concat_accept<part2_real, part2::last_bridge+1>>;
	static const size_t last_bridge = part2::last_bridge+1;
};
// a+? ->
//   0: jump<1>
//   1: 'a', backup<1>, jump<2>
//   2: accept
template<size_t start, typename T> struct process<start, repeat<0, range_t<1,SIZE_MAX>, T>> {
	using part2 = process<start+1, T>;
	using part2_loop = typename replace_accept_2<typename part2::result, backup<start+1>, jump<part2::last_bridge+1>>::result;
	using part1 = re_flat<capture_set<>, bridge<start, jump<start+1>>>;
	
	using result = concat<part1, concat_accept<part2_loop, part2::last_bridge+1>>;
	static const size_t last_bridge = part2::last_bridge+1;
};
// a{0,5}? ->
// 0: backup<1>, jump<2>
// 1: 'a', jump<2>
// 2: accept
// + process<a{0,4}>
template<size_t start, size_t maxrepeat, typename T> struct process<start, repeat<0, range_t<0,maxrepeat>, T>> {
	using part2 = process<start+1, T>;
	using part1 = re_flat<capture_set<>, bridge<start, jump<part2::last_bridge+1>, backup<start+1>>>;
	using part3 = process<part2::last_bridge+1, repeat<0, range_t<0, maxrepeat-1>, T>>;
	using part2_real = typename replace_accept<typename part2::result, jump<part3::last_bridge+1>>::result;
	
	using result = concat<part1, concat<part2_real, concat_accept<typename part3::result, part3::last_bridge+1>>>;
	static const size_t last_bridge = part3::last_bridge;
};
// a{0,0}? - identical to a{0,0}, to avoid some ambiguities
template<size_t start, typename T> struct process<start, repeat<0, range_t<0,0>, T>> {
	using result = re_flat<capture_set<>, bridge<start, accept>>;
	static const size_t last_bridge = start;
};


//lookahead -> flatten contents, create a stone containing that (nested lookaheads remain nested)
template<size_t start, bool is_behind, typename inner, typename inner_cap> struct lookahead_bridge {
	using result = bridge<start, accept, lookahead<false, is_behind, inner>, capture_delete<inner_cap>>;
};
template<size_t start, bool is_behind, typename inner> struct lookahead_bridge<start, is_behind, inner, capture_set<>> {
	using result = bridge<start, accept, lookahead<false, is_behind, inner>>;
};
template<size_t start, bool is_behind, typename child>
struct process<start, lookahead<false, is_behind, child>> {
	using inner = typename process<0, child>::result;
	using inner_cap = typename extract_captures<inner>::result;
	
	using result = re_flat<capture_set<>, typename lookahead_bridge<start, is_behind, inner, inner_cap>::result>;
	static const size_t last_bridge = start;
};
template<size_t start, bool is_behind, typename child>
struct process<start, lookahead<true, is_behind, child>> {
	using inner = typename process<0, child>::result;
	
	using result = re_flat<typename extract_captures<inner>::result, bridge<start, accept, lookahead<true, is_behind, inner>>>;
	static const size_t last_bridge = start;
};

}

template<size_t n_capture, typename T> struct re_flattener {
	using flat = flatten::process<0, T>;
	using flat_res = typename flat::result;
	flat_optimizer<n_capture, flat_res> compile() { return {}; }
};



template<size_t n_capture, typename Tre> class matcher;

//TODO: implement
//- bridge has no incoming backup, and only one incoming jump -> replace jump with bridge contents, delete bridge
//- bridge has no incoming backup, and no stones, only an accept -> replace jump with bridge contents, delete bridge
//- bridge has no stones, only a jump -> redirect incoming jumps and backups to the target, delete bridge
//- combine consecutive literals
//- lookahead contents should be optimized too
//test with RE_DEBUG_FLAT("a{5}") and RE_DEBUG_FLAT("a{0,5}"), should be 1 and 2 bridges respectively
template<size_t n_capture, typename T> struct flat_optimizer {
	matcher<n_capture, T> optimize() { return {}; }
};



//wipe the nuls early, to improve compile time by not passing large amounts of trash into deep template instantiations,
// and to improve error messages by trimming the fat and passing str as chars
template<size_t n, typename out, char... tail> struct mkstr_sub;
template<size_t n, char... out, char head, char... tail> struct mkstr_sub<n, parser::str<out...>, head, tail...> {
	using content = typename mkstr_sub<n-1, parser::str<out..., head>, tail...>::content;
};
template<char... out, char... tail>
struct mkstr_sub<0, parser::str<out...>, 0, tail...> {
	using content = parser::str<out..., ')'>;
};

//I could use static_assert(std::is_same<out_tree, str<>>), but that would remove what out_tree actually is from the errors.
template<typename chars, typename inner> struct assert_empty;
template<typename inner> struct assert_empty<parser::str<>, inner> {
	using content = inner;
};

template<typename chars> struct parse_outer;

template<size_t n_chars, char... rawchars>
struct mkstr {
	static_assert(n_chars < sizeof...(rawchars), "input too long, extend RE_UNPACK");
	// weirdo indirection, to delete rawchars from the compiler errors
	static parse_outer<typename mkstr_sub<n_chars, parser::str<'(','?',':'>, rawchars...>::content> chars() { return {}; }
};

template<char... chars> struct parse_outer<parser::str<chars...>> {
	using content = parser::parse_term<1, parser::str<chars...>>;
	
	static const size_t out_n_capture = content::out_n_capture;
	using out_tree = typename assert_empty<typename content::out_tail, typename content::out_tree>::content;
	re_flattener<out_n_capture, out_tree> parse() { return {}; };
};


struct pair {
	const char * start;
	const char * end;
#ifdef HAVE_ARLIB
	cstring str() const { return arrayview<char>(start, end-start); }
	operator cstring() const { return str(); }
#endif
};
template<size_t N> class match_t {
	template<size_t n_capture, typename Tre> friend class matcher;
	pair group[N];
	
public:
	template<size_t Ni> friend class match_t;
	match_t() { memset(group, 0, sizeof(group)); }
	template<size_t Ni> match_t(const match_t<Ni>& inner)
	{
		static_assert(Ni < N); // if equal, it goes to the implicitly defaulted copy ctor
		memcpy(group, inner.group, sizeof(inner.group));
		memset(group+Ni, 0, sizeof(pair)*(N-Ni));
	}
	
	static const size_t size = N;
	pair& operator[](size_t n) { return group[n]; }
	const pair& operator[](size_t n) const { return group[n]; }
	operator bool() const { return group[0].end; }
	bool operator!() const { return !(bool)*this; }
};

//I'd use a fold expression or something if I could, but c++ is weird
template<typename... ranges> struct match_range;
template<> struct match_range<> { static bool match(uint8_t ch) { return false; } };
template<size_t low, size_t high, typename... next>
struct match_range<range_t<low,high>, next...> {
	static bool match(uint8_t ch)
	{
		return (low <= ch && ch < high) || match_range<next...>::match(ch);
	}
};

template<size_t n_capture, typename Tcap, typename... Tbris_o>
class matcher<n_capture, re_flat<Tcap, Tbris_o...>> {
	template<size_t nci, typename Trei> friend class matcher;
	
	template<size_t n, typename Tbri, typename... Tbris> struct get_bridge {
		using result = typename get_bridge<n-1, Tbris...>::result;
	};
	template<typename Tbri, typename... Tbris> struct get_bridge<0, Tbri, Tbris...> {
		using result = Tbri;
	};
	
	template<typename... Ts> struct submatch;
	template<size_t n, typename Tfin, typename... Tstone>
	struct submatch<bridge<n, Tfin, Tstone...>> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			// extra pointless void because c++ rejects explicit specialization in non-namespace scope for some inexplicable reason
			return submatch<Tstone..., Tfin, void>::match(cap, start);
		}
	};
	
	template<uint8_t... chars, typename... Tstone>
	struct submatch<literal<chars...>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			uint8_t lit[] = { chars... };
			if ((size_t)(cap[0].end-start) < sizeof(lit)) return nullptr;
			for (size_t i=0;i<sizeof(lit);i++)
			{
				if ((uint8_t)start[i] != lit[i]) return nullptr;
			}
			return submatch<Tstone...>::match(cap, start+sizeof(lit));
		}
	};
	
	template<typename... ranges, typename... Tstone>
	struct submatch<char_class<ranges...>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			if (start == cap[0].end) return nullptr;
			uint8_t ch = (uint8_t)*start;
			if (!match_range<ranges...>::match(ch))
				return nullptr;
			return submatch<Tstone...>::match(cap, start+1);
		}
	};
	
	template<size_t n, bool is_start, typename... Tstone>
	struct submatch<capture_edge<n, is_start>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			if (is_start) cap[n].start = start;
			else cap[n].end = start;
			return submatch<Tstone...>::match(cap, start);
		}
	};
	
	template<size_t... ns, typename... Tstone>
	struct submatch<capture_delete<capture_set<ns...>>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			size_t nsu[] = { ns... };
			for (size_t n : nsu)
			{
				cap[n].start = nullptr;
				cap[n].end = nullptr;
			}
			return submatch<Tstone...>::match(cap, start);
		}
	};
	
	template<size_t n, typename... Tstone>
	struct submatch<backreference<n>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			const char * cend = cap[n].end;
			const char * cstart = cap[n].start;
			if (cap[0].end-start < cend-cstart)
				return nullptr;
			
			for (ptrdiff_t i=0;i<cend-cstart;i++)
			{
				if (start[i] != cstart[i]) return nullptr;
			}
			return submatch<Tstone...>::match(cap, start+(cend-cstart));
		}
	};
	
	template<bool is_end, typename... Tstone>
	struct submatch<anchor<is_end>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			if (!is_end && start != cap[0].start) return nullptr;
			if (is_end && start != cap[0].end) return nullptr;
			
			return submatch<Tstone...>::match(cap, start);
		}
	};
	
	template<bool expect_boundary, typename... Tstone>
	struct submatch<word_boundary<expect_boundary>, Tstone...> {
		static bool is_word_char(uint8_t ch)
		{
			return (('0' <= ch && ch <= '9') ||
			        ('A' <= ch && ch <= 'Z') ||
			        ('a' <= ch && ch <= 'z') ||
			        ch == '_');
		}
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			uint8_t ch1 = (start == cap[0].start ? '.' : start[-1]);
			uint8_t ch2 = (start == cap[0].end ? '.' : start[0]);
			bool is_boundary = (is_word_char(ch1) != is_word_char(ch2));
			if (is_boundary != expect_boundary) return nullptr;
			
			return submatch<Tstone...>::match(cap, start);
		}
	};
	
	template<bool should_match, typename Tre, typename... Tstone>
	struct submatch<lookahead<should_match, false, Tre>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			bool matches = matcher<n_capture, Tre>::match(cap, start);
			if (matches != should_match) return nullptr;
			return submatch<Tstone...>::match(cap, start);
		}
	};
	
	template<size_t n, typename... Tstone>
	struct submatch<backup<n>, Tstone...> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			match_t<n_capture> prev_cap = cap;
			const char * ret = submatch<Tstone...>::match(cap, start);
			if (ret) return ret;
			
			cap = prev_cap;
			return submatch<typename get_bridge<n, Tbris_o...>::result>::match(cap, start);
		}
	};
	
	template<typename T>
	struct submatch<accept, T> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			return start;
		}
	};
	
	template<size_t target>
	struct submatch<jump<target>, void> {
		static const char * match(match_t<n_capture>& cap, const char * start)
		{
			return submatch<typename get_bridge<target, Tbris_o...>::result>::match(cap, start);
		}
	};
	
	static const char * match(match_t<n_capture>& cap, const char * start)
	{
		return submatch<typename get_bridge<0, Tbris_o...>::result>::match(cap, start);
	}
	
public:
	//This is the base matcher function, which everything else is built on top of.
	//If the regex doesn't match, out's contents are unspecified; it's not even guaranteed to contain valid substrings.
	static bool match(match_t<n_capture>& out, const char * start, const char * at, const char * end)
	{
		out[0].start = start;
		out[0].end = end;
		out[0].end = match(out, at);
		out[0].start = at;
		return out;
	}
	
	// TODO: create fullmatch, if possible without shenanigans (whether to allow match end != string end is nontrivial)
	
	static match_t<n_capture> match(const char * start, const char * at, const char * end)
	{
		match_t<n_capture> cap;
		if (match(cap, start, at, end)) return cap;
		else return {};
	}
	
	static match_t<n_capture> match(const char * start, const char * end)
	{
		return match(start, start, end);
	}
#ifdef HAVE_ARLIB
	// must not take a temporary, that's a use-after-free
	static match_t<n_capture> match(cstring&& str) = delete;
	static match_t<n_capture> match(const cstring& str)
	{
		return match((char*)str.bytes().ptr(), (char*)str.bytes().ptr()+str.length());
	}
#endif
	static match_t<n_capture> match(const char * str)
	{
		return match(str, str+strlen(str));
	}
	
	static match_t<n_capture> search(const char * start, const char * end)
	{
		match_t<n_capture> cap;
		const char * iter = start;
		while (iter < end)
		{
			if (match(cap, start, iter, end))
				return cap;
			iter++;
		}
		return {};
	}
#ifdef HAVE_ARLIB
	// must not take a temporary, that's a use-after-free
	static match_t<n_capture> search(cstring&& str) = delete;
	static match_t<n_capture> search(const cstring& str)
	{
		return search((char*)str.bytes().ptr(), (char*)str.bytes().ptr()+str.length());
	}
#endif
	static match_t<n_capture> search(const char * str)
	{
		return search(str, str+strlen(str));
	}
	
#ifdef HAVE_ARLIB
	//Replacement should contain \\1 to refer to capture groups, not $1.
	//Only groups 0-9 are supported.
	static string replace(cstring str, const char * replacement)
	{
		const char * start = (char*)str.bytes().ptr();
		const char * end = (char*)str.bytes().ptr()+str.length();
		match_t<n_capture> cap;
		
		string out;
		
		const char * iter = start;
		while (iter < end)
		{
			if (match(cap, start, iter, end))
			{
				for (size_t i=0;replacement[i];i++)
				{
					if (replacement[i] == '\\')
					{
						int n = replacement[i+1]-'0';
						if (n < 0 || n > 9) abort();
						
						out += cap[n].str();
						i++;
					}
					else
						out += replacement[i];
				}
				iter = cap[0].end;
			}
			else
			{
				out += *iter;
				iter++;
			} 
		}
		return out;
	}
#endif
};

#ifdef __OPTIMIZE__
}
#endif
}

#define RE_UNPACK1(str,n) \
	,(n < sizeof(str) ? str[n] : 0) 
#define RE_UNPACK8(str,n) \
	RE_UNPACK1(str,(n)*8+0) RE_UNPACK1(str,(n)*8+1) RE_UNPACK1(str,(n)*8+2) RE_UNPACK1(str,(n)*8+3) \
	RE_UNPACK1(str,(n)*8+4) RE_UNPACK1(str,(n)*8+5) RE_UNPACK1(str,(n)*8+6) RE_UNPACK1(str,(n)*8+7)
#define RE_UNPACK64(str,n) \
	RE_UNPACK8(str,(n)*8+0) RE_UNPACK8(str,(n)*8+1) RE_UNPACK8(str,(n)*8+2) RE_UNPACK8(str,(n)*8+3) \
	RE_UNPACK8(str,(n)*8+4) RE_UNPACK8(str,(n)*8+5) RE_UNPACK8(str,(n)*8+6) RE_UNPACK8(str,(n)*8+7)
#define RE_UNPACK(str) \
	sizeof(str)-1 \
	RE_UNPACK64(str,0) RE_UNPACK64(str,1) RE_UNPACK64(str,2) RE_UNPACK64(str,3) \
	RE_UNPACK64(str,4) RE_UNPACK64(str,5) RE_UNPACK64(str,6) RE_UNPACK64(str,7)

#define REGEX(str) (regex::mkstr<RE_UNPACK(str)>::chars().parse().compile().optimize())
// These four emit a compiler error containing the given regex at various stages of parsing.
// Useful for debugging this module, less useful for anything else.
#define REGEX_DEBUG_STR(str) (regex::mkstr<RE_UNPACK(str)>::chars().fail())
#define REGEX_DEBUG_TREE(str) (regex::mkstr<RE_UNPACK(str)>::chars().parse().fail())
#define REGEX_DEBUG_FLATIN(str) (regex::mkstr<RE_UNPACK(str)>::chars().parse().compile().fail())
#define REGEX_DEBUG_FLAT(str) (regex::mkstr<RE_UNPACK(str)>::chars().parse().compile().optimize().fail())
