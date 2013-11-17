#include "shared.h"

#include <chrono>
#include <ctime>
#if defined(WINDOWS)
#include <stdlib.h>
#include <time.h>
#endif

static std::chrono::system_clock::time_point CalculateEpoch(void)
{
	std::tm Calendar{};
	Calendar.tm_year = 100;

	auto OriginalTZ = getenv("TZ");
#if defined(WINDOWS)
	_putenv("TZ=UTC");
	_tzset();
#else
	setenv("TZ", "UTC", 1);
	tzset();
#endif
	auto Out = std::chrono::system_clock::from_time_t(std::mktime(&Calendar));
#if defined(WINDOWS)
	if (OriginalTZ) _putenv((std::string("TZ=") + OriginalTZ).c_str());
	else _putenv("TZ=");
	_tzset();
#else
	if (OriginalTZ) setenv("TZ", OriginalTZ, 1);
	else unsetenv("TZ");
	tzset();
#endif

	return Out;
}
static auto const Epoch = CalculateEpoch();

uint64_t GetNow(void) { return (std::chrono::system_clock::now() - Epoch) / std::chrono::milliseconds(1); }

CallTransferType::~CallTransferType(void) {}

void CallTransferType::operator ()(std::function<void(void)> const &Call) { Transfer(Call); }
