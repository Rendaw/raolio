#ifndef protocol_h
#define protocol_h

// TODO Add conditional big endian -> little endian conversions

/*
Major versions are incompatible, and could be essentially different protocols.
 versions are compatible.

You may add minor versions to a protocol at any time, but you must never remove them.
Messages are completely redefined for each new version.
*/

#include "constcount.h"
#include "error.h"
#include "cast.h"

#include <vector>
#include <functional>
#include <limits>
#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>
#include <typeinfo>

#define DefineProtocol(Name) typedef Protocol::Protocol<__COUNTER__> Name;
#define DefineProtocolVersion(Name, InProtocol) typedef Protocol::Version<static_cast<Protocol::VersionIDType::Type>(GetConstCount(InProtocol)), InProtocol> Name; IncrementConstCount(InProtocol)
#define DefineProtocolMessage(Name, InVersion, Signature) typedef Protocol::Message<static_cast<Protocol::MessageIDType::Type>(GetConstCount(InVersion)), InVersion, Signature> Name; IncrementConstCount(InVersion)

namespace Protocol
{
// Overloaded write and read methods
typedef StrictType(uint8_t) VersionIDType;
typedef StrictType(uint8_t) MessageIDType;
typedef StrictType(uint16_t) SizeType;
typedef StrictType(uint16_t) ArraySizeType;

template <typename ElementType> struct SubVector
{
      SubVector(void) : Data{nullptr}, Length{0} {}
      SubVector(std::vector<ElementType> &Base, size_t Start, size_t Length) : Data{&Base[Start]}, Length{Length} {}
      ElementType *const Data;
      size_t const Length;
      operator bool(void) const { return Length > 0; }
      ElementType &operator[](size_t Index) { assert(*this); assert(Index < Length); return Data[Index]; }
      ElementType const &operator[](size_t Index) const { assert(*this); assert(Index < Length); return Data[Index]; }
};
typedef SubVector<uint8_t> BufferType;

}

template <typename Type, typename Enable = void> struct ProtocolOperations;

template <typename Type> constexpr size_t ProtocolGetSize(Type const &Argument)
	{ return ProtocolOperations<Type>::GetSize(Argument); }
template <typename Type> inline void ProtocolWrite(uint8_t *&Out, Type const &Argument)
	{ return ProtocolOperations<Type>::Write(Out, Argument); }
template <typename Type> bool ProtocolRead(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, Type &Data)
	{ return ProtocolOperations<Type>::Read(VersionID, MessageID, Buffer, Offset, Data); }

namespace Protocol
{
// Infrastructure
constexpr SizeType HeaderSize{SizeType::Type(VersionIDType::Size + MessageIDType::Size + SizeType::Size)};

template <size_t Individuality> struct Protocol {};

template <VersionIDType::Type IDValue, typename InProtocol> struct Version
	{ static constexpr VersionIDType ID{IDValue}; };
template <VersionIDType::Type IDValue, typename InProtocol> constexpr VersionIDType Version<IDValue, InProtocol>::ID;

template <MessageIDType::Type, typename, typename> struct Message;
template <MessageIDType::Type IDValue, typename InVersion, typename ...Definition> struct Message<IDValue, InVersion, void(Definition...)>
{
	typedef InVersion Version;
	typedef void Signature(Definition...);
	typedef std::function<void(Definition const &...)> Function;
	static constexpr MessageIDType ID{IDValue};

	static std::vector<uint8_t> Write(Definition const &...Arguments)
	{
		std::vector<uint8_t> Out;
		auto RequiredSize = Size(Arguments...);
		if (RequiredSize > std::numeric_limits<SizeType::Type>::max())
			{ assert(false); return Out; }
		Out.resize(StrictCast(HeaderSize, size_t) + RequiredSize);
		uint8_t *WritePointer = &Out[0];
		ProtocolWrite(WritePointer, InVersion::ID);
		ProtocolWrite(WritePointer, ID);
		ProtocolWrite(WritePointer, (SizeType::Type)RequiredSize);
		Write(WritePointer, Arguments...);
		return Out;
	}

	private:
		template <typename NextType, typename... RemainingTypes>
			static inline size_t Size(NextType NextArgument, RemainingTypes... RemainingArguments)
			{ return ProtocolGetSize(NextArgument) + Size(RemainingArguments...); }

		static constexpr size_t Size(void) { return {0}; }

		template <typename NextType, typename... RemainingTypes>
			static inline void Write(uint8_t *&Out, NextType NextArgument, RemainingTypes... RemainingArguments)
			{
				ProtocolWrite(Out, NextArgument);
				Write(Out, RemainingArguments...);
			}

		static inline void Write(uint8_t *&) {}

};
template <MessageIDType::Type IDValue, typename InVersion, typename ...Definition> constexpr MessageIDType Message<IDValue, InVersion, void(Definition...)>::ID;

// Deserialization
template <VersionIDType::Type CurrentVersionID, MessageIDType::Type CurrentMessageID, typename Enabled, typename ...MessageTypes> struct ReaderTupleElement;
template
<
	VersionIDType::Type CurrentVersionID,
	MessageIDType::Type CurrentMessageID,
	typename MessageType,
	typename ...RemainingMessageTypes
>
struct ReaderTupleElement
<
	CurrentVersionID,
	CurrentMessageID,
	typename std::enable_if<(CurrentVersionID == *MessageType::Message::Version::ID) && (CurrentMessageID == *MessageType::Message::ID)>::type,
	MessageType, RemainingMessageTypes...
>
: ReaderTupleElement
<
	CurrentVersionID,
	CurrentMessageID + 1,
	void,
	RemainingMessageTypes...
>
{
	private:
		template <typename ParentType = MessageType> struct MessageDerivedTypes;
		template <MessageIDType::Type ID, typename InVersion, typename ...Definition>
			struct MessageDerivedTypes<Message<ID, InVersion, void(Definition...)>>
		{
			typedef std::tuple<Definition...> Tuple;
		};

		typedef ReaderTupleElement
		<
			CurrentVersionID,
			CurrentMessageID + 1,
			void,
			RemainingMessageTypes...
		> NextElement;

	protected:
		template <typename HandlerType, typename... ExtraTypes>
			bool Read(HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, ExtraTypes const &... ExtraArguments)
		{
			if ((VersionID == MessageType::Version::ID) && (MessageID == MessageType::ID))
			{
				SizeType Offset{(SizeType::Type)0};
				return ReadImplementation<HandlerType, typename MessageDerivedTypes<>::Tuple, std::tuple<ExtraTypes...>>::Read(Handler, VersionID, MessageID, Buffer, Offset, std::forward<ExtraTypes const &>(ExtraArguments)...);
			}
			return NextElement::Read(Handler, VersionID, MessageID, Buffer);
		}

	private:
		template <typename HandlerType, typename UnreadTypes, typename ReadTypes> struct ReadImplementation {};
		template <typename HandlerType, typename NextType, typename... RemainingTypes, typename... ReadTypes>
			struct ReadImplementation<HandlerType, std::tuple<NextType, RemainingTypes...>, std::tuple<ReadTypes...>>
		{
			static bool Read(HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, SizeType &Offset, ReadTypes const &...ReadData)
			{
				NextType Data;
				if (!ProtocolRead(VersionID, MessageID, Buffer, Offset, Data)) return false;
				return ReadImplementation<HandlerType, std::tuple<RemainingTypes...>, std::tuple<ReadTypes..., NextType>>::Read(Handler, VersionID, MessageID, Buffer, Offset, std::forward<ReadTypes const &>(ReadData)..., std::move(Data));
			}
		};

		template <typename HandlerType, typename... ReadTypes>
			struct ReadImplementation<HandlerType, std::tuple<>, std::tuple<ReadTypes...>>
		{
			static bool Read(HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, SizeType &Offset, ReadTypes const &...ReadData)
			{
				Handler.Handle(MessageType{}, std::forward<ReadTypes const &>(ReadData)...);
				return true;
			}
		};
};

template
<
	VersionIDType::Type CurrentVersionID,
	MessageIDType::Type CurrentMessageID,
	typename MessageType,
	typename ...RemainingMessageTypes
>
struct ReaderTupleElement
<
	CurrentVersionID,
	CurrentMessageID,
	typename std::enable_if<(CurrentVersionID + 1 == *MessageType::Message::Version::ID)>::type,
	MessageType, RemainingMessageTypes...
>
: ReaderTupleElement
<
	CurrentVersionID + 1,
	0,
	void,
	MessageType, RemainingMessageTypes...
>
{
	typedef ReaderTupleElement
	<
		CurrentVersionID + 1,
		0,
		void,
		MessageType, RemainingMessageTypes...
	> NextElement;
};

template
<
	VersionIDType::Type CurrentVersionID,
	MessageIDType::Type CurrentMessageID
>
struct ReaderTupleElement
<
	CurrentVersionID,
	CurrentMessageID,
	void
>
{
	template <typename HandlerType> bool Read(HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer)
	{
		assert(false);
		return false;
	}
};

template <typename ...MessageTypes> struct Reader : ReaderTupleElement<0, 0, void, MessageTypes...>
{
	// StreamType must have SubVector<uint8_t> const &Read(size_t Length, size_t Offset = 0) and void Consume(size_t) methods.
	template <typename StreamType, typename HandlerType, typename... ExtraTypes> bool Read(StreamType &&Stream, HandlerType &Handler, ExtraTypes const ...ExtraArguments)
	{
		auto Header = Stream.Read(StrictCast(HeaderSize, size_t));
		if (!Header) return true;
		VersionIDType const VersionID = *reinterpret_cast<VersionIDType *>(&Header[0]);
		MessageIDType const MessageID = *reinterpret_cast<MessageIDType *>(&Header[VersionIDType::Size]);
		SizeType const DataSize = *reinterpret_cast<SizeType *>(&Header[VersionIDType::Size + MessageIDType::Size]);

		auto Body = Stream.Read(StrictCast(DataSize, size_t), StrictCast(HeaderSize, size_t));
		if (!Body) return true;

		bool Out = HeadElement::Read(Handler, VersionID, MessageID, Body, ExtraArguments...);

		static_assert(sizeof(HeaderSize) == sizeof(DataSize), "U suk");
		static_assert(sizeof(typename decltype(HeaderSize)::Type) == sizeof(typename decltype(DataSize)::Type), "U suk");
		static_assert(std::is_same<typename decltype(HeaderSize)::Type, typename decltype(DataSize)::Type>::value, "U suk");
		Stream.Consume(StrictCast(HeaderSize + DataSize, size_t));

		return Out;
	}

	private:
		typedef ReaderTupleElement<0, 0, void, MessageTypes...> HeadElement;
};

}

#endif
