#ifndef error_h
#define error_h

#include <string>
#include <sstream>
#include <functional>
#include <cassert>
#include <iostream>

template <int InternalID> struct ErrorBase
{
	ErrorBase(void) {}
	ErrorBase(ErrorBase<InternalID> const &Other) : Buffer(Other.Buffer.str()) {}
	template <int OtherInternalID> ErrorBase(ErrorBase<OtherInternalID> const &Other) : Buffer(Other.Buffer.str()) {}
	template <typename Whatever> ErrorBase<InternalID> &operator <<(Whatever const &Input) { Buffer << Input; return *this; }
	operator std::string(void) const { return Buffer.str(); }

	private:
		std::stringstream Buffer;
};

template <int InternalID> inline std::ostream& operator <<(std::ostream &Out, ErrorBase<InternalID> const &Error)
	{ Out << static_cast<std::string>(Error); return Out; }

#define ErrorBase ErrorBase<__COUNTER__>

typedef ErrorBase UserError;
typedef ErrorBase SystemError;

inline void AssertStamp(char const *File, char const *Function, int Line)
	{ std::cerr << File << "/" << Function << ":" << Line << " Assertion failed" << std::endl; }

template <typename Type> inline void AssertImplementation(char const *File, char const *Function, int Line, Type const &Value)
{
#ifndef NDEBUG
	if (!Value)
	{
		AssertStamp(File, Function, Line);
		throw false;
	}
#endif
}

template <typename GotType, typename ExpectedType> inline void AssertImplementation(char const *File, char const *Function, int Line, GotType const &Got, ExpectedType const &Expected)
{
#ifndef NDEBUG
	if (Got != Expected)
	{
		AssertStamp(File, Function, Line);
		throw false;
	}
#endif
}

#define Assert(...) AssertImplementation(__FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

struct Cleanup
{
	Cleanup(std::function<void(void)> &&Procedure) : Procedure(std::move(Procedure)) {}
	~Cleanup(void) { Procedure(); }
	private:
		std::function<void(void)> Procedure;
};

#endif
