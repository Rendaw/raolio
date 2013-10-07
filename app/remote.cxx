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

	std::string Command;
	for (int Index = 1; Index < argc; ++Index)
	{
		if (argv[Index][0] == '-')
		{
			Command = argv[Index];
			break;
		}
	}

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (CommandIndex
	if ((argc >= 2) && (argv[1][0] != '-'))
	{
		Host = argv[1];
		if ((argc >= 3) && (argv[2][0] != '-'))
			String(argv[2]) >> Port;
	}

	Core Core;
	Core.Open(true, Host, Port);
	std::cout << "Starting server @ " << Host << ":" << Port << std::endl;

	if (Command.empty()) // Go with the flow
		{}
	else if (Command == "--add")
	{
	}
	else if (Command == "--play")
	{
	}
	else if (Command == "--stop")
	{
		Core.Stop();
	}
	else if (Command == "--message")
	{
	}

	std::cout << "Command issued.  Please wait until you are reasonably certain it has been sent and all subsequent actions have been completed to your satisfaction, then press ctrl+c to quit." << std::endl;

	while (!Dead)
		Sleep.wait(SleepLock);

	return 0;
}
