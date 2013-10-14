#ifndef type_h
#define type_h

#include "extrastandard.h"
#include <string>
#include <sstream>

struct ConstructionError
{
	ConstructionError(void) {}
	ConstructionError(ConstructionError const &Other) : Buffer(Other.Buffer.str()) {}
	template <typename Whatever> ConstructionError &operator <<(Whatever const &Input) { Buffer << Input; return *this; }
	operator std::string(void) const { return Buffer.str(); }

	private:
		std::stringstream Buffer;
};

inline std::ostream& operator <<(std::ostream &Out, ConstructionError const &Error)
	{ Out << static_cast<std::string>(Error); return Out; }

template <typename DataType> struct Optional
{
	Optional(void) : Valid(false) {}
	Optional(DataType const &Data) : Valid(true), Data(Data) {}
	Optional &operator =(Optional<DataType> const &Other) { Valid = Other.Valid; if (Valid) Data = Other.Data; return *this; }
	operator bool(void) const { return Valid; }
	bool operator !(void) const { return !Valid; }
	DataType &operator *(void) { Assert(Valid); return Data; }
	DataType const &operator *(void) const { Assert(Valid); return Data; }
	DataType *operator ->(void) { Assert(Valid); return &Data; }
	DataType const *operator ->(void) const { Assert(Valid); return &Data; }
	bool operator ==(Optional<DataType> const &Other) const
		{ return (!Valid && !Other.Valid) || (Valid && Other.Valid && (Data == Other.Data)); }
	bool operator <(Optional<DataType> const &Other) const
	{
		if (!Valid && !Other.Valid) return false;
		if (Valid && Other.Valid) return Data < Other.Data;
		if (!Other.Valid) return true;
		return false;
	}
	bool Valid;
	DataType Data;
};

struct Pass {};
struct Fail {};

template <typename DataType> struct TextErrorOr
{
	TextErrorOr(Fail, std::string &Message) : Error(Message) {}
	TextErrorOr(Pass, DataType const &Data) : Data(Data) {}
	operator bool(void) const { return Error.empty(); }
	bool operator !(void) const { return !Error.empty(); }
	DataType &operator *(void) { Assert(Error.empty()); return Data; }
	DataType const &operator *(void) const { Assert(Error.empty()); return Data; }
	DataType *operator ->(void) { Assert(Error.empty()); return &Data; }
	DataType const *operator ->(void) const { Assert(Error.empty()); return &Data; }
	std::string const Error;
	DataType Data;
};

#endif
