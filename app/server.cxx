#include "core.h"

#include "translation/translation.h"

#include <condition_variable>
#include <csignal>

bool Die = false;
std::mutex Mutex;
std::condition_variable SleepSignal;

int main(int argc, char **argv)
{
	std::unique_lock<std::mutex> SleepSignalLock(Mutex);
	std::signal(SIGINT, [](int) { std::lock_guard<std::mutex> Lock(Mutex); Die = true; SleepSignal.notify_all(); });

	InitializeTranslation("raolioserver");

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2)
	{
		Host = argv[1];
		if ((Host == "--help") || (Host == "-h"))
		{
			std::cout << "raolioserver [HOST] [PORT]" << std::endl;
			return 0;
		}
	}
	if (argc >= 3) String(argv[2]) >> Port;
	Core Core{true};
	Core.LogCallback = [](Core::LogPriority Priority, std::string const &Message)
	{
#ifdef NDEBUG
		if (Priority >= Core::Debug) return;
#endif
		std::cout << Priority << ": " << Message << std::endl;
	};
	Core.Open(true, Host, Port);
	std::cout << Local("Starting server @ ^0:^1", Host, Port) << std::endl;

	while (!Die)
		SleepSignal.wait(SleepSignalLock);

	return 0;
}
