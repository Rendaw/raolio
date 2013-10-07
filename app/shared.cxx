#include "shared.h"

uint64_t GetNow(void) { return std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1); }

CallTransferType::~CallTransferType(void) {}

void CallTransferType::operator ()(std::function<void(void)> const &Call) { Transfer(Call); }

std::string FormatHash(HashType const &Hash)
{
	std::stringstream Display;
	Display << std::hex << std::setw(2);
	for (auto Byte : Hash) Display << static_cast<unsigned int>(Byte);
	return Display.str();
}
