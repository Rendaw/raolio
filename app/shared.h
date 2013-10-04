#ifndef shared_h
#define shared_h

#include "error.h"

#include <string>
#include <sstream>
#include <array>
#include <iomanip>
#include <chrono>

typedef std::array<uint8_t, 16> HashType;

struct String
{
	String(void) {}
	String(std::string const &Initial) : Buffer(Initial) {}
	template <typename Whatever> String &operator <<(Whatever const &Input) { Buffer << Input; return *this; }
	template <typename Whatever> String &operator >>(Whatever &Output) { Buffer >> Output; return *this; }
	operator std::string(void) const { return Buffer.str(); }

	private:
		std::stringstream Buffer;
};

template <typename DataType> struct Optional
{
	Optional(void) : Valid(false) {}
	Optional(DataType const &Data) : Valid(true), Data(Data) {}
	operator bool(void) const { return Valid; }
	bool operator !(void) const { return !Valid; }
	DataType &operator *(void) { Assert(Valid); return Data; }
	DataType *operator ->(void) { Assert(Valid); return &Data; }
	bool Valid;
	DataType Data;
};

uint64_t GetNow(void);

#include <memory>

// Will be included in C++14 lolololol
template<typename T, typename... Args> std::unique_ptr<T> make_unique(Args&&... args)
	{ return std::unique_ptr<T>(new T(std::forward<Args>(args)...)); }

// For making a call occur from a different thread; generally queued and idly executed
struct CallTransferType
{
	virtual ~CallTransferType(void);
	virtual void operator()(std::function<void(void)> const &Call) = 0;
};

std::string FormatHash(HashType const &Hash)
{
	std::stringstream Display;
	Display << std::hex << std::setw(2);
	for (auto Byte : Hash) Display << static_cast<unsigned int>(Byte);
	return Display.str();
}

#endif

