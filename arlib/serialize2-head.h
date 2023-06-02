#define SER_ENTER(s) for (bool _continue=(s)._begin();_continue;_continue=(s)._continue())
#define SER_ENTER_ARRAY(s) \
	for (bool _continue=(s)._begin_array(); _continue; (_continue && ((s)._cancel_array(), _continue=false))) \
		for (; _continue; _continue=(s)._continue_array())
#define SER_NAME(s, name) for (bool _step=(s)._name(name);_step;_step=(!(s).serializing && (s)._name(name)))
#define SER_IF(s, cond) if (!(s).serializing || cond)
