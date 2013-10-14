#include <csignal>

static struct IgnoreSIGPIPEStatic
{
	IgnoreSIGPIPEStatic(void) { std::signal(SIGPIPE, SIG_IGN); }
} IgnoreSIGPIPE;
