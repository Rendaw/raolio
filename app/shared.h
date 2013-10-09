#ifndef shared_h
#define shared_h

#include <cstdint>
#include <functional>

uint64_t GetNow(void);

// For making a call occur from a different thread; generally queued and idly executed
struct CallTransferType
{
	virtual ~CallTransferType(void);
	virtual void Transfer(std::function<void(void)> const &Call) = 0;
	void operator ()(std::function<void(void)> const &Call);
};

#endif

