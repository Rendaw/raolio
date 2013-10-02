#include "core.h"

std::mutex Mutex;
std::condition_variable Sleep(Mutex);

int main(int argc, char **argv)
{
	std::signal(SIGINT, [](int) { Sleep.notify_all(); });

	std::string Host{"0.0.0.0"};
	uint16_t Port{20578};
	if (argc >= 2) Host = argv[1];
	if (argc >= 3) String(argv[2]) >> Port;
	std::cout << "Starting server @ " << Host << ":" << Port << std::endl;
	Core Core(true, Host, Port);

	Sleep.take();

	return 0;
}
