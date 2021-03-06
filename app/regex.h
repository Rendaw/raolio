#ifndef regex_h
#define regex_h

//#include "shared.h"
#include "../ren-cxx-basics/extrastandard.h"

#include <regex>
#define hack_regex_match std::regex_match
#define hack_regex std::regex
#define hack_smatch std::smatch
#define hack_mark_count(regex) regex.mark_count()
/*#include <boost/regex.hpp>
#define hack_regex_match boost::regex_match
#define hack_regex boost::regex
#define hack_smatch boost::smatch
#define hack_mark_count(regex) (regex.mark_count() - 1)
*/
#include <cassert>
#include <map>
#include <string>

#include <iostream>

namespace Regex
{

struct Ignore {};

template <typename EnumerationType> struct EnumerationT : std::map<std::string, EnumerationType>
{
	typedef EnumerationType Type;
	using std::map<std::string, EnumerationType>::map;
	EnumerationT(void) = delete;
};

template <typename ...CaptureTypes> struct ParserT
{
	ParserT(char const *Pattern) : Expression{Pattern} { AssertE(hack_mark_count(Expression), sizeof...(CaptureTypes)); }

	template <typename... OutputTypes>
	bool operator()(std::string Input, OutputTypes &...Outputs)
	{
		hack_smatch Captures;
		if (!hack_regex_match(Input, Captures, Expression)) return false;
		Extract<void, std::tuple<CaptureTypes...>, std::tuple<OutputTypes...>>{Captures, Outputs...};
		return true;
	}

	private:
		hack_regex Expression;

		template <typename Enabled, typename ExtractTypes, typename OutputTypes> struct Extract;

		template <typename Dummy> struct Extract<Dummy, std::tuple<>, std::tuple<>>
			{ Extract(hack_smatch const &Captures) {} };

		template <typename NextType, typename ...RemainingTypes, typename ...OutputTypes>
			struct Extract
			<
				typename std::enable_if<std::is_same<NextType, std::string>::value>::type,
				std::tuple<NextType, RemainingTypes...>,
				std::tuple<NextType, OutputTypes...>
			>
		{
			Extract(hack_smatch const &Captures, NextType &Output, OutputTypes &...OtherOutputs)
			{
				Output = Captures[sizeof...(CaptureTypes) - sizeof...(RemainingTypes)];
				Extract<void, std::tuple<RemainingTypes...>, std::tuple<OutputTypes...>>{Captures, OtherOutputs...};
			}
		};

		template <typename NextType, typename ...RemainingTypes, typename ...OutputTypes>
			struct Extract
			<
				typename std::enable_if<std::is_arithmetic<NextType>::value>::type,
				std::tuple<NextType, RemainingTypes...>,
				std::tuple<NextType, OutputTypes...>
			>
		{
			Extract(hack_smatch const &Captures, NextType &Output, OutputTypes &...OtherOutputs)
			{
				StringT(Captures[sizeof...(CaptureTypes) - sizeof...(RemainingTypes)]) >> Output;
				Extract<void, std::tuple<RemainingTypes...>, std::tuple<OutputTypes...>>(Captures, OtherOutputs...);
			}
		};

		template <typename EnumerationType, typename ...RemainingTypes, typename ...OutputTypes>
			struct Extract
			<
				//typename std::enable_if<std::is_same<EnumerationType, EnumerationT<typename EnumerationType::Type>>::value>::type,
				void,
				std::tuple<EnumerationType, RemainingTypes...>,
				std::tuple<typename EnumerationType::Type, OutputTypes...>
			>
		{
			Extract(hack_smatch const &Captures, typename EnumerationType::Type &Output, OutputTypes &...OtherOutputs)
			{
				EnumerationType Enumeration;
				typename EnumerationType::iterator Found =
					Enumeration.find(Captures[sizeof...(CaptureTypes) - sizeof...(RemainingTypes)]);
				Assert(Found != Enumeration.end());
				Output = Found->second;
				Extract<void, std::tuple<RemainingTypes...>, std::tuple<OutputTypes...>>{Captures, OtherOutputs...};
			}
		};

		template <typename NextType, typename ...RemainingTypes, typename ...OutputTypes>
			struct Extract
			<
				typename std::enable_if<std::is_same<NextType, Ignore>::value>::type,
				std::tuple<NextType, RemainingTypes...>,
				std::tuple<OutputTypes...>
			>
		{
			Extract(hack_smatch const &Captures, OutputTypes &...OtherOutputs)
				{ Extract<void, std::tuple<RemainingTypes...>, std::tuple<OutputTypes...>>{Captures, OtherOutputs...}; }
		};
};

}

#endif
