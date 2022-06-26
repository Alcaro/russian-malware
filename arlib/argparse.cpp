#include "argparse.h"
#include "stringconv.h"
#include <time.h>

string argparse::get_usage()
{
#ifdef STDOUT_DELETE
	return "";
#endif
	return "TODO";
}
void argparse::usage()
{
	exit(0);
}
void argparse::error(cstrnul why)
{
	m_onerror(why);
	
	fprintf(stderr, "%s: %s\n", (const char*)m_appname, (const char*)why);
	exit(1);
}
void argparse::single_arg(arg_base& arg, const char * value, arglevel_t arglevel, bool* used_value)
{
	if (!arg.can_use)
	{
		if (arg.name) error("can't use --"+arg.name+" more than once");
		else error("can't use more than one non-argument");
	}
	if (!arg.can_use_multi) arg.can_use = false;
	arg.must_use = false;
	
	if (used_value) *used_value = false;
	
	if (!value && !arg.accept_no_value) error("missing argument for --"+arg.name);
	if (arglevel == al_mandatory && !arg.accept_value) error("--"+arg.name+" doesn't allow an argument");
	
	if (value && arg.accept_value && (!arg.accept_no_value || arglevel > al_loose))
	{
		if (!arg.parse(true, value)) error("invalid value for --"+arg.name);
		if (used_value) *used_value = true;
	}
	else
	{
		arg.parse(false, "");
	}
}
void argparse::single_arg(cstring name, const char * value, arglevel_t arglevel, bool* used_value)
{
	for (arg_base& arg : m_args)
	{
		if (arg.name == name)
		{
			single_arg(arg, value, arglevel, used_value);
			return;
		}
	}
	if (name != "") error("unknown argument: --"+name);
	else error("positional arguments not supported");
}
void argparse::single_arg(char sname, const char * value, arglevel_t arglevel, bool* used_value)
{
	for (arg_base& arg : m_args)
	{
		if (arg.sname == sname)
		{
			single_arg(arg, value, arglevel, used_value);
			return;
		}
	}
	string e = "unknown argument: -";
	e += sname; // no operator+(string, char), too high risk it's hit by an accidental operator+(string, int)
	error(e);
}

void argparse::parse_pre(const char * const * argv)
{
#ifndef ARLIB_OPT
	if (m_appname)
	{
		error("internal error: argparse::parse called twice");
	}
#endif
	m_appname = argv[0];
}
void argparse::parse(const char * const * argv)
{
	parse_pre(argv);
	
	argv++;
	while (*argv)
	{
		const char * arg = *argv;
		argv++;
		if (arg[0] == '-')
		{
			if (arg[1] == '-')
			{
				if (arg[2] == '\0')
				{
					// just --
					while (*argv)
					{
						single_arg("", *argv, al_mandatory, NULL);
						argv++;
					}
				}
				else
				{
					// -- followed by something
					
					// +3 to skip the --, and the first actual character, otherwise '--=derp' would derp out
					const char * eq = strchr(arg+3, '=');
					if (eq)
					{
						single_arg(arrayview<char>(arg+2, eq-(arg+2)), eq+1, al_mandatory, NULL);
					}
					else
					{
						bool used;
						single_arg(arg+2, *argv, al_loose, &used);
						if (used) argv++;
					}
				}
			}
			else if (arg[1] == '\0')
			{
				// a lone -
				single_arg("", "-", al_mandatory, NULL);
			}
			else
			{
				// - followed by something
				arg++;
				while (*arg)
				{
					if (arg[1])
					{
						// -ab (*arg is 'a', argument is "b")
						bool used;
						single_arg(*arg, arg+1, al_tight, &used);
						if (used) break;
					}
					else
					{
						// -a b (*arg is 'a', next is "b")
						bool used;
						single_arg(*arg, *argv, al_loose, &used);
						if (used) argv++;
					}
					arg++;
				}
			}
		}
		else
		{
			// no leading -
			single_arg("", arg, al_mandatory, NULL);
			if (m_early_stop)
			{
				while (*argv)
				{
					single_arg("", *argv, al_mandatory, NULL);
					argv++;
				}
			}
		}
	}
	
	parse_post();
}

void argparse::parse_post()
{
	for (arg_base& arg : m_args)
	{
		if (arg.must_use)
		{
			if (arg.name) error("missing required argument --"+arg.name);
			else error("missing required positional argument");
		}
	}
}


#include "test.h"

#ifdef ARLIB_TEST
static void test_one_pack(const char * opts, cstring extras, bool error, const char * const argvp[])
{
	bool b[3] = {};
	string s[3];
	int i = 0;
	string os;
	bool hos = false;
	string ns;
	bool hns = false;
	array<string> extra;
	
	argparse args;
	args.add('a', "bool",  &b[0]);
	args.add('b', "bool2", &b[1]);
	args.add('c', "bool3", &b[2]);
	args.add('i', "int",  &i);
	args.add('A', "str",  &s[0]);
	args.add('B', "str2", &s[1]);
	args.add('C', "str3", &s[2]);
	args.add('N', "nullstr", &hns, &ns);
	args.add('O', "optstr", &hos, &os);
	if (extras) args.add("", &extra);
	
	if (error) args.onerror([](cstrnul){ throw 42; });
	else args.onerror([](cstrnul error){ puts(error); assert_unreachable(); });
	
	try {
		args.parse(argvp);
	}
	catch(int) {
		if (error) return;
		else throw;
	}
	if (error) assert_unreachable();
	
#define opt(ch, val, yes, no) if (strchr(opts, ch)) assert_eq(val, yes); else assert_eq(val, no);
	opt('a', b[0], true, false);
	opt('b', b[1], true, false);
	opt('c', b[2], true, false);
	opt('i', i, 42, 0);
	opt('A', s[0], "value", "");
	opt('B', s[1], "value", "");
	opt('C', s[2], "-value", "");
	opt('N', hns, true, false);
	assert_eq(ns, "");
#undef opt
	
	if (strchr(opts, 'o'))
	{
		assert(hos);
		assert_eq(os, "");
	}
	else if (strchr(opts, 'O'))
	{
		assert(hos);
		assert_eq(os, "value");
	}
	else assert(!hos);
	
	assert_eq(extra.join("/"), extras);
}

template<typename... Args>
static void test_one(const char * opts, cstring extras, Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	test_one_pack(opts, extras, false, argvp);
}

template<typename... Args>
static void test_error(Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	test_one_pack(nullptr, "", true, argvp);
}

template<typename... Args>
static void test_getopt(bool a, bool b, const char * c, const char * nonopts, Args... argv)
{
	const char * const argvp[] = { "x", argv..., NULL };
	
	bool ra = false;
	bool rb = false;
	string rc;
	array<string> realnonopts;
	
	argparse args;
	args.add('a', "a", &ra);
	args.add('b', "b", &rb);
	args.add('c', "c", &rc);
	args.add("", &realnonopts);
	
	args.parse(argvp);
	
	if (!c) c = "";
	if (!nonopts) nonopts = "";
	
	assert_eq(ra, a);
	assert_eq(rb, b);
	assert_eq(rc, c);
	assert_eq(realnonopts.join("/"), nonopts);
}

test("argument parser", "string,array", "argparse")
{
	// If Arlib in any way disagrees with GNU getopt, Arlib is wrong.
	// Exception: getopt allows abbreviating long option names if no other option starts with these characters.
	// This just feels silly; too high risk previously-working shell scripts stop working if a new option is added so --co is now ambiguous.
	// Another difference is that this one can't differentiate blank arguments from missing.
	// This is a bug and will be fixed as soon as I figure out how to represent that.
	
	testcall(test_one("a",   "",        "--bool"));
	testcall(test_one("A",   "",        "--str=value"));
	testcall(test_one("i",   "",        "--int=42"));
	testcall(test_one("a", "arg/--arg", "--bool", "arg", "--", "--arg"));
	testcall(test_one("A",   "",        "--str", "value"));
	testcall(test_one("ab",  "",        "--bool", "--bool2"));
	testcall(test_one("ab",  "-",       "--bool", "-", "--bool2"));
	testcall(test_one("a",   "--arg",   "--bool", "--", "--arg"));
	testcall(test_one("",    "",        "--"));
	testcall(test_one("",    "--",      "--", "--"));
	testcall(test_one("abA", "",        "-abAvalue"));
	testcall(test_one("abA", "",        "-abA", "value"));
	testcall(test_one("ab",  "arg",     "-ab", "arg"));
	testcall(test_one("A",   "",        "-A", "value"));
	testcall(test_one("C",   "",        "-C", "-value"));
	testcall(test_one("C",   "",        "-C-value"));
	testcall(test_one("o",   "",        "-O"));
	testcall(test_one("O",   "",        "-Ovalue"));
	testcall(test_one("o",   "value",   "-O", "value"));
	testcall(test_one("o",   "",        "--optstr"));
	testcall(test_one("O",   "",        "--optstr=value"));
	testcall(test_one("o",   "value",   "--optstr", "value"));
	testcall(test_one("a",   "foo",     "foo", "--bool"));
	testcall(test_error("--nonexistent"));
	testcall(test_error("--=error"));
	testcall(test_error("-x")); // nonexistent
	testcall(test_error("--bool", "--bool")); // more than once
	testcall(test_error("--str")); // missing argument
	testcall(test_error("--bool=error")); // argument not allowed
	testcall(test_error("--int=error")); // can't parse
	testcall(test_error("eee")); // extras not allowed here
	
	//missing-required, not tested by the above
	try
	{
		argparse args;
		args.onerror([](cstring){ throw 42; });
		string foo;
		args.add("foo", &foo).required();
		const char * const argvp[] = { "x", NULL };
		args.parse(argvp);
		assert_unreachable();
	}
	catch(int) {}
	
	//examples extracted from https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
	//0/1 as bool because the example does so
	//redundant to the above, but why not
	
	test_getopt(0, 0, NULL,  NULL);
	test_getopt(1, 1, NULL,  NULL,     "-a", "-b");
	test_getopt(1, 1, NULL,  NULL,     "-ab");
	test_getopt(0, 0, "foo", NULL,     "-c", "foo");
	test_getopt(0, 0, "foo", NULL,     "-cfoo");
	test_getopt(0, 0, NULL,  "arg1",   "arg1");
	test_getopt(1, 0, NULL,  "arg1",   "-a", "arg1");
	test_getopt(0, 0, "foo", "arg1",   "-c", "foo", "arg1");
	test_getopt(1, 0, NULL,  "-b",     "-a", "--", "-b");
	test_getopt(1, 0, NULL,  "-",      "-a", "-");
}
#endif
