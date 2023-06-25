#ifndef __DEKU_INSPECT__
#define __DEKU_INSPECT__

#define STACKTRACE_MAX_SIZE 32

struct inspect_stacktrace {
	void *address[STACKTRACE_MAX_SIZE];
	unsigned long long unreliable;
	unsigned long long id;
};

#endif /* __DEKU_INSPECT__ */
