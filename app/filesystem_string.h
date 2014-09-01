#ifndef string_h
#define string_h

#include "extrastandard.h"

#include <string>
#include <map>
#include <sstream>
#include <vector>
#include <list>
#include <fstream>
#ifdef WINDOWS
#include <windows.h>
#endif

#ifdef WINDOWS

inline std::u16string ToNativeString(std::string const &Input)
{
	static_assert(sizeof(wchar_t) == sizeof(char16_t), "Assumption that all Windows systems use 16-bit wide characters failed!");
	if (Input.empty()) return {};
	const int Length = MultiByteToWideChar(CP_UTF8, 0, Input.c_str(), Input.length(), nullptr, 0);
	AssertGT(Length, 0);
	std::vector<char16_t> ConversionBuffer;
	ConversionBuffer.resize(Length);
	MultiByteToWideChar(CP_UTF8, 0, Input.c_str(), Input.length(), (LPWSTR)&ConversionBuffer[0], Length);
	return std::u16string(&ConversionBuffer[0], Length);
}

inline std::string FromNativeString(std::u16string const &Input)
{
	const int Length = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t const *>(Input.c_str()), Input.length(), nullptr, 0, nullptr, nullptr);
	AssertGT(Length, 0);
	std::vector<char> ConversionBuffer;
	ConversionBuffer.resize(Length);
	WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t const *>(Input.c_str()), Input.length(), &ConversionBuffer[0], Length, nullptr, nullptr);
	return std::string(&ConversionBuffer[0], Length);
}

#else

inline std::string ToNativeString(std::string const &Input)
	{ return Input; }

inline std::string FromNativeString(std::string const &Input)
	{ return Input; }

#endif

#endif
