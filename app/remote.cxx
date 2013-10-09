#include "core.h"

#include <condition_variable>
#include <csignal>

bool Die = false;
std::mutex Mutex;
std::condition_variable Sleep;

int main(int argc, char **argv)
{
	std::unique_lock<std::mutex> SleepLock(Mutex);
	std::signal(SIGINT, [](int) { std::lock_guard<std::mutex> Lock(Mutex); Die = true; Sleep.notify_all(); });

	std::string Command;
	int CommandIndex;
	for (int Index = 1; Index < argc; ++Index)
	{
		if (argv[Index][0] == '-')
		{
			CommandIndex = Index;
			Command = argv[Index];
			break;
		}
	}

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if ((argc >= 2) && (argv[1][0] != '-'))
	{
		Host = argv[1];
		if ((argc >= 3) && (argv[2][0] != '-'))
			String(argv[2]) >> Port;
	}

	Core Core;
	Core.LogCallback = [](Core::LogPriority Priority, std::string const &Message)
	{
#ifdef NDEBUG
		if (Priority >= Core::Debug) return;
#endif
		std::cout << Priority << ": " << Message << std::endl;
	};
	Core.Open(false, Host, Port);
	std::cout << "Connecting to " << Host << ":" << Port << std::endl;

	if (Command.empty()) // Go with the flow
		{}
	else if (Command == "--add")
	{
		if (CommandIndex + 1 >= argc)
		{
			std::cerr << "--add requires a filename." << std::endl;
			goto FullBreak;
		}
		auto Hash = HashFile(argv[CommandIndex + 1]);
		if (!Hash)
		{
			std::cerr << "Invalid file to --add '" << argv[CommandIndex + 1] << "'" << std::endl;
			goto FullBreak;
		}

		Core.Transfer([&, Hash](void) { Core.Add(Hash->first, argv[CommandIndex + 1]); });
	}
	else if (Command == "--play")
	{
		if (CommandIndex + 1 >= argc)
		{
			std::cerr << "--play requires a hash, in hex, lowercase, no spaces or other delimiters." << std::endl;
			goto FullBreak;
		}
		auto Hash = UnformatHash(argv[CommandIndex + 1]);
		if (!Hash)
		{
			std::cerr << "Invalid hash provided for --add." << std::endl;
			goto FullBreak;
		}

		Core.Transfer([&, Hash](void) { Core.Play(*Hash, 0u, GetNow()); });
	}
	else if (Command == "--stop")
	{
		Core.Transfer([&](void) { Core.Stop(); });
	}
	else if (Command == "--message")
	{
		std::stringstream Message;
		for (int Index = CommandIndex + 1; Index < argc; ++Index)
			Message << argv[Index] << " ";
		Core.Transfer([&](void) { Core.Chat(Message.str()); });
	}
	else std::cerr << "Unknown command '" << Command << "'" << std::endl;
	FullBreak:

	std::cout << "Command issued.  Please wait until you are reasonably certain it has been sent and all subsequent actions have been completed to your satisfaction, then press ctrl+c to quit." << std::endl;

	while (!Die)
		Sleep.wait(SleepLock);

	return 0;
}
