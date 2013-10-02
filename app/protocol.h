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

typedef std::vector<uint8_t> BufferType;
}

template <typename IntType, typename std::enable_if<std::is_integral<IntType>::value>::type * = nullptr>
	constexpr size_t ProtocolGetSize(IntType const &Argument)
	{ return sizeof(IntType); }

template <typename IntType, typename std::enable_if<std::is_integral<IntType>::value>::type * = nullptr>
	inline void ProtocolWrite(uint8_t *&Out, IntType const &Argument)
	{ *reinterpret_cast<IntType *>(Out) = Argument; Out += sizeof(Argument); }

template <size_t ExplicitCastableUniqueness, typename ExplicitCastableType> size_t ProtocolGetSize(ExplicitCastable<ExplicitCastableUniqueness, ExplicitCastableType> const &Argument)
	{ return ProtocolGetSize(*Argument); }

template <size_t ExplicitCastableUniqueness, typename ExplicitCastableType> void ProtocolWrite(uint8_t *&Out, ExplicitCastable<ExplicitCastableUniqueness, ExplicitCastableType> const &Argument)
	{ ProtocolWrite(Out, *Argument); }

inline size_t ProtocolGetSize(std::string const &Argument)
{
	assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
	return {Protocol::SizeType::Type(Protocol::ArraySizeType::Size + Argument.size())};
}
inline void ProtocolWrite(uint8_t *&Out, std::string const &Argument)
{
	Protocol::ArraySizeType const StringSize = (Protocol::ArraySizeType::Type)Argument.size();
	ProtocolWrite(Out, *StringSize);
	memcpy(Out, Argument.c_str(), Argument.size());
	Out += Argument.size();
}

template <typename ElementType, typename std::enable_if<!std::is_class<ElementType>::value>::type* = nullptr>
	inline size_t ProtocolGetSize(std::vector<ElementType> const &Argument)
{
	assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
	return Protocol::ArraySizeType::Size + (Protocol::ArraySizeType::Type)Argument.size() * sizeof(ElementType);
}

template <typename ElementType, typename std::enable_if<!std::is_class<ElementType>::value>::type* = nullptr>
	inline void ProtocolWrite(uint8_t *&Out, std::vector<ElementType> const &Argument)
{
	Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
	ProtocolWrite(Out, *ArraySize);
	memcpy(Out, &Argument[0], Argument.size() * sizeof(ElementType));
	Out += Argument.size() * sizeof(ElementType);
}

template <typename ElementType, typename std::enable_if<std::is_class<ElementType>::value>::type* = nullptr>
	static inline size_t ProtocolGetSize(std::vector<ElementType> const &Argument)
{
	assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
	Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
	size_t Out = Protocol::ArraySizeType::Size;
	for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < ArraySize; ++ElementIndex)
		Out += ProtocolGetSize(Argument[*ElementIndex]);
	return Out;
}
template <typename ElementType, typename std::enable_if<std::is_class<ElementType>::value>::type* = nullptr>
	static inline void ProtocolWrite(uint8_t *&Out, std::vector<ElementType> const &Argument)
{
	Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
	ProtocolWrite(Out, *ArraySize);
	for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < ArraySize; ++ElementIndex)
		ProtocolWrite(Out, Argument[*ElementIndex]);
}

template <typename LogType, typename IntType> bool ProtocolRead(LogType &Log, Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, IntType &Data)
{
	if (Buffer.size() < StrictCast(Offset, size_t) + sizeof(IntType))
	{
		Log.Debug() << "End of file reached prematurely reading message body integer size " << sizeof(IntType) << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}

	Data = *reinterpret_cast<IntType const *>(&Buffer[*Offset]);
	Offset += static_cast<Protocol::SizeType::Type>(sizeof(IntType));
	return true;
}

template <typename LogType> bool ProtocolRead(LogType &Log, Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::string &Data)
{
	if (Buffer.size() < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size)
	{
		Log.Debug() << "End of file reached prematurely reading message body string header size " << Protocol::SizeType::Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
	Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
	if (Buffer.size() < StrictCast(Offset, size_t) + (size_t)Size)
	{
		Log.Debug() << "End of file reached prematurely reading message body string body size " << Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Data = std::string(reinterpret_cast<char const *>(&Buffer[*Offset]), Size);
	Offset += Size;
	return true;
}

template <typename LogType, typename ElementType, typename std::enable_if<!std::is_class<ElementType>::value>::type* = nullptr>
	bool ProtocolRead(LogType &Log, Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::vector<ElementType> &Data)
{
	if (Buffer.size() < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size)
	{
		Log.Debug() << "End of file reached prematurely reading message body array header size " << Protocol::ArraySizeType::Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
	Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
	if (Buffer.size() < StrictCast(Offset, size_t) + Size * sizeof(ElementType))
	{
		Log.Debug() << "End of file reached prematurely reading message body array body size " << Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Data.resize(Size);
	memcpy(&Data[0], &Buffer[*Offset], Size * sizeof(ElementType));
	Offset += static_cast<Protocol::SizeType::Type>(Size * sizeof(ElementType));
	return true;
}

template <typename LogType, typename ElementType, typename std::enable_if<std::is_class<ElementType>::value>::type* = nullptr>
	bool ProtocolRead(LogType &Log, Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::vector<ElementType> &Data)
{
	if (Buffer.size() < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size)
	{
		Log.Debug() << "End of file reached prematurely reading message body array header size " << Protocol::ArraySizeType::Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
	Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
	if (Buffer.size() < StrictCast(Offset, size_t) + (size_t)Size)
	{
		Log.Debug() << "End of file reached prematurely reading message body array body size " << Size << ", message length doesn't match contents (version " << *VersionID << ", type " << *MessageID << ")";
		assert(false);
		return false;
	}
	Data.resize(Size);
	for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < Size; ++ElementIndex)
		ProtocolRead(Log, VersionID, MessageID, Buffer, Offset, Data[*ElementIndex]);
	return true;
}

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
		template <typename LogType, typename HandlerType, typename... ExtraTypes>
			bool Read(LogType &Log, HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, ExtraTypes const &... ExtraTypes)
		{
			if ((VersionID == MessageType::Version::ID) && (MessageID == MessageType::ID))
			{
				SizeType Offset{(SizeType::Type)0};
				return ReadImplementation<LogType, typename MessageDerivedTypes<>::Tuple, std::tuple<ExtraTypes...>>::Read(Log, Handler, VersionID, MessageID, Buffer, Offset, std::forward<ExtraTypes const &>(ExtraTypes)...);
			}
			return NextElement::Read(Log, Handler, VersionID, MessageID, Buffer);
		}

	public:
		template <
			typename CallMessageType,
			typename ...ArgumentTypes,
			typename std::enable_if<std::is_same<CallMessageType, MessageType>::value>::type* = nullptr>
			void Call(ArgumentTypes const & ...Arguments)
			{ Handler.Handle(CallMessageType{}, std::forward<ArgumentTypes const &>(Arguments)...); }

		template <
			typename CallMessageType,
			typename ...ArgumentTypes,
			typename std::enable_if<!std::is_same<CallMessageType, MessageType>::value>::type* = nullptr>
			void Call(ArgumentTypes const & ...Arguments)
			{ NextElement::template Call<CallMessageType, ArgumentTypes...>(std::forward<ArgumentTypes const &>(Arguments)...); }

	private:
		template <typename LogType, typename HandlerType, typename UnreadTypes, typename ReadTypes> struct ReadImplementation {};
		template <typename LogType, typename HandlerType, typename NextType, typename... RemainingTypes, typename... ReadTypes>
			struct ReadImplementation<LogType, HandlerType, std::tuple<NextType, RemainingTypes...>, std::tuple<ReadTypes...>>
		{
			static bool Read(LogType &Log, HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, SizeType &Offset, ReadTypes const &...ReadData)
			{
				NextType Data;
				if (!ProtocolRead(Log, VersionID, MessageID, Buffer, Offset, Data)) return false;
				return ReadImplementation<LogType, std::tuple<RemainingTypes...>, std::tuple<ReadTypes..., NextType>>::Read(Log, Handler, VersionID, MessageID, Buffer, Offset, std::forward<ReadTypes const &>(ReadData)..., std::move(Data));
			}
		};

		template <typename LogType, typename HandlerType, typename... ReadTypes>
			struct ReadImplementation<LogType, std::tuple<>, std::tuple<ReadTypes...>>
		{
			static bool Read(LogType &Log, HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer, SizeType &Offset, ReadTypes const &...ReadData)
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
	typedef std::vector<uint8_t> BufferType;
	
	template <typename LogType, typename HandlerType> bool Read(LogType &Log, HandlerType &Handler, VersionIDType const &VersionID, MessageIDType const &MessageID, BufferType const &Buffer)
	{
		Log.Warn() << "Read message with invalid version or message type (version " << *VersionID << ") with invalid message type: " << *MessageID;
		assert(false);
		return false;
	}
};

template <typename ElementType> struct SubVector
{
      SubVector(void) : Data{nullptr}, Length{0} {}
      SubVector(std::vector<ElementType> const &Base, size_t Start, size_t Length) : Data{&Base[Start]}, Length{Length} {}
      ElementType *Data;
      size_t Length;
      operator bool(void) { return Size > 0; }
      ElementType &operator[](size_t Index) { assert(*this); assert(Index < Legnth); return Data[Index]; }
};

template <typename LogType, typename ...MessageTypes> struct Reader : ReaderTupleElement<0, 0, void, MessageTypes...>
{
	Reader(LogType &Log) : Log(Log) {}

	// StreamType must have SubVector<uint8_t> const &Read(size_t Length, size_t Offset = 0) and void Consume(size_t) methods.
	template <typename StreamType, typename HandlerType, typename... ExtraTypes> bool Read(StreamType &&Stream, HandlerType &Handler, ExtraTypes const ...ExtraArguments)
	{
		auto Header = Buffer.Read(StrictCast(HeaderSize, size_t));
		if (!Header) return true;
		VersionIDType const VersionID = *reinterpret_cast<VersionIDType *>(&Header[0]);
		MessageIDType const MessageID = *reinterpret_cast<MessageIDType *>(&Header[VersionIDType::Size]);
		SizeType const DataSize = *reinterpret_cast<SizeType *>(&Header[VersionIDType::Size + MessageIDType::Size]);

		auto Body = Buffer.Read(StrictCast(DataSize, size_t), StrictCast(HeaderSize, size_t));
		if (!Body) return true;

		bool Out = HeadElement::Read(Log, Handler, VersionID, MessageID, Body, ExtraArguments...);

		Stream.Consume(StrictCast(HeaderSize + DataSize, size_t));

		return Out;
	}

	private:
		typedef ReaderTupleElement<0, 0, void, MessageTypes...> HeadElement;

		LogType &Log;
};

}

#endif
