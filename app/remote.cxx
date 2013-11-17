#include "core.h"

#include "translation/translation.h"

#include <condition_variable>
#include <csignal>

bool Die = false;
std::mutex Mutex;
std::condition_variable SleepSignal;

int main(int argc, char **argv)
{
	InitializeTranslation("raolioremote");
	if (argc >= 2)
	{
		std::string Test = argv[1];
		if ((Test == "--help") || (Test == "-h"))
		{
			std::cout << Local(
				"raolioremote [HOST] [PORT] [OPTIONS]\n"
				"\tOPTION can be --add FILE, --play FILE, --stop, --message MESSAGE\n")
				<< std::flush;
			return 0;
		}
	}
	std::unique_lock<std::mutex> SleepSignalLock(Mutex);
	std::signal(SIGINT, [](int) { std::lock_guard<std::mutex> Lock(Mutex); Die = true; SleepSignal.notify_all(); });

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

	Core Core{true};
	Core.LogCallback = [](Core::LogPriority Priority, std::string const &Message)
	{
#ifdef NDEBUG
		if (Priority >= Core::Debug) return;
#endif
		std::cout << Priority << ": " << Message << std::endl;
	};
	Core.Open(false, Host, Port);
	std::cout << Local("Connecting to ^0:^1", Host, Port) << std::endl;

	if (Command.empty()) // Go with the flow
		{}
	else if (Command == "--add")
	{
		if (CommandIndex + 1 >= argc)
		{
			std::cerr << Local("--add requires a filename.") << std::endl;
			goto FullBreak;
		}
		auto Hash = HashFile(argv[CommandIndex + 1]);
		if (!Hash)
		{
			std::cerr << Local("Invalid file to --add '^0'", argv[CommandIndex + 1]) << std::endl;
			goto FullBreak;
		}

		Core.Transfer([&, Hash](void) { Core.Add(Hash->first, Hash->second, argv[CommandIndex + 1]); });
	}
	else if (Command == "--play")
	{
		if (CommandIndex + 1 >= argc)
		{
			std::cerr << Local("--play requires a filename.") << std::endl;
			goto FullBreak;
		}
		auto Hash = HashFile(argv[CommandIndex + 1]);
		if (!Hash)
		{
			std::cerr << Local("Invalid file to --play '^0'", argv[CommandIndex + 1]) << std::endl;
			goto FullBreak;
		}

		Core.Transfer([&, Hash](void)
		{
			Core.Add(Hash->first, Hash->second, argv[CommandIndex + 1]);
			Core.Play(Hash->first, MediaTimeT(0), GetNow());
		});
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
	else std::cerr << Local("Unknown command '^0'", Command) << std::endl;
	FullBreak:

	std::cout << Local("Command issued.  Please wait until you are reasonably certain it has been sent and all subsequent actions have been completed to your satisfaction, then press ctrl+c to quit.") << std::endl;

	while (!Die)
		SleepSignal.wait(SleepSignalLock);

	return 0;
}
