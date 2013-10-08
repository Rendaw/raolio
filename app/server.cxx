#include "core.h"

#include <condition_variable>
#include <csignal>

bool Dead = false;
std::mutex Mutex;
std::condition_variable Sleep;

int main(int argc, char **argv)
{
	std::unique_lock<std::mutex> SleepLock(Mutex);
	std::signal(SIGINT, [](int) { std::lock_guard<std::mutex> Lock(Mutex); Dead = true; Sleep.notify_all(); });

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2) Host = argv[1];
	if (argc >= 3) String(argv[2]) >> Port;
	Core Core;
	Core.Open(true, Host, Port);
	std::cout << "Starting server @ " << Host << ":" << Port << std::endl;

	while (!Dead)
		Sleep.wait(SleepLock);

	return 0;
}
