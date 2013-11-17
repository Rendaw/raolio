#include <csignal>

#if !defined(WINDOWS)
static struct IgnoreSIGPIPEStatic
{
	IgnoreSIGPIPEStatic(void) { std::signal(SIGPIPE, SIG_IGN); }
} IgnoreSIGPIPE;
#endif
