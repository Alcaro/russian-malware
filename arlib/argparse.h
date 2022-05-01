#pragma once
#include "string.h"
#include "stringconv.h"
#include "file.h"
#include <type_traits>

class argparse {
	class arg_base {
		friend class argparse;
		
	protected:
		arg_base(bool accept_no_value, bool accept_value) : accept_no_value(accept_no_value), accept_value(accept_value) {}
		
		string name;
		char sname;
		
		bool accept_no_value;
		bool accept_value;
		
		bool must_use = false;
		bool can_use = true;
		bool can_use_multi = false;
		
		virtual bool parse(bool has_value, cstring arg) = 0;
	public:
		virtual ~arg_base() {}
		
		friend void arlib_init(argparse& args, char** argv);
	};
	
	template<typename T>
	class arg_t : public arg_base {
	protected:
		arg_t(bool accept_no_value, bool accept_value) : arg_base(accept_no_value, accept_value) {}
		
	public:
		T& required()
		{
			this->must_use = true;
			return *(T*)this;
		}
	};
	
	string m_appname;
	refarray<arg_base> m_args;
	function<void(cstrnul error)> m_onerror;
	bool m_early_stop = false;
#ifdef ARLIB_GUI
	bool m_accept_cli = false;
	bool m_has_gui = false;
#endif
	
	//sname can be '\0' or absent if you only want long names
	//if the long name is empty, all non-option arguments are sent here; if nothing has a long name, passing non-options is an error
	
private:
	class arg_str : public arg_t<arg_str> {
		friend class argparse;
		
		arg_str(bool* has_target, string* target) : arg_t(has_target, true), has_target(has_target), target(target)
		{
			if (has_target) *has_target = false;
		}
		~arg_str() {}
		bool* has_target;
		string* target;
		bool parse(bool has_value, cstring arg)
		{
			if (has_target) *has_target = true;
			if (has_value) *target = arg;
			return true;
		}
	public:
		//none
	};
public:
	arg_str& add(char sname, cstring name, bool* has_target, string* target)
	{
		arg_str* arg = new arg_str(has_target, target);
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	arg_str& add(            cstring name, bool* has_target, string* target) { return add('\0',  name, has_target, target); }
	arg_str& add(char sname, cstring name,                   string* target) { return add(sname, name, NULL,       target); }
	arg_str& add(            cstring name,                   string* target) { return add('\0',  name, NULL,       target); }
	
private:
	class arg_file : public arg_t<arg_file> {
		friend class argparse;
		
		arg_file(bool* has_target, string* target) : arg_t(has_target, true), has_target(has_target), target(target) {}
		~arg_file() {}
		bool* has_target;
		string* target;
		bool parse(bool has_value, cstring arg)
		{
			if (has_target) *has_target = true;
			if (has_value) *target = file::sanitize_trusted_path(arg);
			return true;
		}
	public:
		//none
	};
public:
	arg_file& add_file(char sname, cstring name, bool* has_target, string* target)
	{
		arg_file* arg = new arg_file(has_target, target);
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	arg_file& add_file(            cstring name, bool* has_target, string* target) { return add_file('\0',  name, has_target, target); }
	arg_file& add_file(char sname, cstring name,                   string* target) { return add_file(sname, name, NULL,       target); }
	arg_file& add_file(            cstring name,                   string* target) { return add_file('\0',  name, NULL,       target); }
	
private:
	class arg_strmany : public arg_t<arg_strmany> {
		friend class argparse;
		
		arg_strmany(array<string>* target) : arg_t(false, true), target(target) { this->can_use_multi = true; }
		array<string>* target;
		bool parse(bool has_value, cstring arg) { target->append(arg); return true; }
	public:
		//none
	};
public:
	arg_strmany& add(char sname, cstring name, array<string>* target)
	{
		arg_strmany* arg = new arg_strmany(target);
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	arg_strmany& add(cstring name, array<string>* target) { return add('\0', name, target); }
	
	arg_strmany& add_early_stop(array<string>* target) { m_early_stop = true; return add('\0', "", target); }
	
private:
	template<typename T>
	class arg_int : public arg_t<arg_int<T>> {
		friend class argparse;
		
		arg_int(bool* has_value, T* target) : arg_t<arg_int<T>>(false, true), m_has_value(has_value), m_target(target) {}
		bool* m_has_value;
		T* m_target;
		bool parse(bool has_value, cstring arg) { if (m_has_value) *m_has_value = true; return fromstring(arg, *m_target); }
	public:
		//none
	};
public:
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, arg_int<T>&>
	add(char sname, cstring name, bool* has_value, T* target)
	{
		arg_int<T>* arg = new arg_int<T>(has_value, target);
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, arg_int<T>&>
	add(            cstring name, bool* has_value, T* target) { return add('\0',  name, has_value, target); }
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, arg_int<T>&>
	add(char sname, cstring name,                   T* target) { return add(sname, name, NULL,       target); }
	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, arg_int<T>&>
	add(            cstring name,                   T* target) { return add('\0',  name, NULL,       target); }
	
private:
	class arg_bool : public arg_t<arg_bool> {
		friend class argparse;
		
		arg_bool(bool* target) : arg_t(true, false), target(target) {}
		bool* target;
		bool parse(bool has_value, cstring arg) { *target = true; return true; }
	public:
		//none
	};
public:
	arg_bool& add(char sname, cstring name, bool* target)
	{
		arg_bool* arg = new arg_bool(target);
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	arg_bool& add(cstring name, bool* target) { return add('\0', name, target); }
	
private:
	template<typename Tl>
	class arg_cb : public arg_t<arg_cb<Tl>> {
		friend class argparse;
		
		arg_cb(bool accept_no_value, bool accept_value, Tl&& cb) : arg_t<arg_cb<Tl>>(accept_no_value, accept_value), cb(std::move(cb)) {}
		Tl cb;
		bool parse(bool has_value, cstring arg) { return cb(has_value, arg); }
	public:
		//none
	};
public:
	//takes function<bool(bool has_value, cstring arg)>
	template<typename Tl>
	arg_cb<Tl>& add(char sname, cstring name, bool accept_no_value, bool accept_value, Tl&& cb)
	{
		arg_cb<Tl>* arg = new arg_cb<Tl>(accept_no_value, accept_value, std::move(cb));
		arg->name = name;
		arg->sname = sname;
		m_args.append_take(*arg);
		return *arg;
	}
	template<typename Tl>
	arg_cb<Tl>& add(cstring name, bool accept_no_value, bool accept_value, Tl&& cb)
	{
		return add('\0', accept_no_value, accept_value, std::move(cb));
	}
	
#ifdef ARLIB_GUI
	void add_cli() { m_accept_cli = true; }
	bool has_gui() { return m_has_gui; }
#else
	void add_cli() {}
	bool has_gui() { return false; }
#endif
	
private:
	enum arglevel_t {
		al_none, // no argument available
		al_loose, // -f bar, --foo bar; if the option requires an argument, use it; if not, that's the next word
		al_tight, // -fbar;             if the option can take an argument, use it; if not, that's the next option
		al_mandatory // --foo=bar;      argument must be used, otherwise error
	};
	
	string get_usage();
	void usage();
	void error(cstrnul why);
	void single_arg(arg_base& arg, const char * value, arglevel_t arglevel, bool* used_value);
	void single_arg(cstring name, const char * value, arglevel_t arglevel, bool* used_value);
	void single_arg(char sname, const char * value, arglevel_t arglevel, bool* used_value);
	
public:
	//The handler should not return; if it does, the default handler (print error to stderr and terminate) is called.
	//If you want to do something else, throw an exception.
	void onerror(function<void(cstrnul error)> handler)
	{
		m_onerror = handler;
	}
	
private:
	void parse_pre(const char * const * argv);
	void parse_post(); // Remember to set m_has_gui.
	
public:
	void parse(const char * const * argv);
	
	friend void arlib_init(argparse& args, char** argv);
};
