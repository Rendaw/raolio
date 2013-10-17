#include "shared.h"

#include <chrono>
#include <ctime>

static std::chrono::system_clock::time_point CalculateEpoch(void)
{
	std::tm Calendar{};
	Calendar.tm_year = 100;

	auto OriginalTZ = getenv("TZ");
	setenv("TZ", "UTC", 1);
	tzset();
	auto Out = std::chrono::system_clock::from_time_t(std::mktime(&Calendar));
	if (OriginalTZ) setenv("TZ", OriginalTZ, 1);
	else unsetenv("TZ");
	tzset();

	return Out;
}
static auto const Epoch = CalculateEpoch();

uint64_t GetNow(void) { return (std::chrono::system_clock::now() - Epoch) / std::chrono::milliseconds(1); }

CallTransferType::~CallTransferType(void) {}

void CallTransferType::operator ()(std::function<void(void)> const &Call) { Transfer(Call); }
