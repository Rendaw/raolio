#include "shared.h"

uint64_t GetNow(void) { return std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1); }

CallTransferType::~CallTransferType(void) {}
