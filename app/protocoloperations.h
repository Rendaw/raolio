#ifndef protocoloperations_h
#define protocoloperations_h

template <typename IntType> struct ProtocolOperations<IntType, typename std::enable_if<std::is_integral<IntType>::value>::type>
{
	constexpr static size_t GetSize(IntType const &Argument) { return sizeof(IntType); }

	inline static void Write(uint8_t *&Out, IntType const &Argument)
		{ *reinterpret_cast<IntType *>(Out) = Argument; Out += sizeof(Argument); }

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, IntType &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + sizeof(IntType))
		{
			assert(false);
			return false;
		}

		Data = *reinterpret_cast<IntType const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeType::Type>(sizeof(IntType));
		return true;
	}
};

template <size_t Uniqueness, typename Type> struct ProtocolOperations<ExplicitCastable<Uniqueness, Type>, void>
{
	typedef ExplicitCastable<Uniqueness, Type> Explicit;
	constexpr static size_t GetSize(Explicit const &Argument)
		{ return ProtocolOperations<Type>::GetSize(*Argument); }

	inline static void Write(uint8_t *&Out, Explicit const &Argument)
		{ ProtocolOperations<Type>::Write(Out, *Argument); }

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, Explicit &Data)
		{ return ProtocolOperations<Type>::Read(VersionID, MessageID, Buffer, Offset, Data.Data); }
};

template <> struct ProtocolOperations<std::string, void>
{
	static size_t GetSize(std::string const &Argument)
	{
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
		return {Protocol::SizeType::Type(Protocol::ArraySizeType::Size + Argument.size())};
	}

	inline static void Write(uint8_t *&Out, std::string const &Argument)
	{
		Protocol::ArraySizeType const StringSize = (Protocol::ArraySizeType::Type)Argument.size();
		ProtocolWrite(Out, *StringSize);
		memcpy(Out, Argument.c_str(), Argument.size());
		Out += Argument.size();
	}

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::string &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size)
		{
			assert(false);
			return false;
		}
		Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
		if (Buffer.Length < StrictCast(Offset, size_t) + (size_t)Size)
		{
			assert(false);
			return false;
		}
		Data = std::string(reinterpret_cast<char const *>(&Buffer[*Offset]), Size);
		Offset += Size;
		return true;
	}
};

template <typename ElementType> struct ProtocolOperations<std::vector<ElementType>, typename std::enable_if<!std::is_class<ElementType>::value>::type>
{
	static size_t GetSize(std::vector<ElementType> const &Argument)
	{
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
		return Protocol::ArraySizeType::Size + (Protocol::ArraySizeType::Type)Argument.size() * sizeof(ElementType);
	}

	inline static void Write(uint8_t *&Out, std::vector<ElementType> const &Argument)
	{
		Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
		ProtocolWrite(Out, *ArraySize);
		memcpy(Out, &Argument[0], Argument.size() * sizeof(ElementType));
		Out += Argument.size() * sizeof(ElementType);
	}

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::vector<ElementType> &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size)
		{
			assert(false);
			return false;
		}
		Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
		if (Buffer.Length < StrictCast(Offset, size_t) + Size * sizeof(ElementType))
		{
			assert(false);
			return false;
		}
		Data.resize(Size);
		memcpy(&Data[0], &Buffer[*Offset], Size * sizeof(ElementType));
		Offset += static_cast<Protocol::SizeType::Type>(Size * sizeof(ElementType));
		return true;
	}
};

template <typename ElementType> struct ProtocolOperations<std::vector<ElementType>, typename std::enable_if<std::is_class<ElementType>::value>::type>
{
	static size_t GetSize(std::vector<ElementType> const &Argument)
	{
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeType::Type>::max());
		Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
		size_t Out = Protocol::ArraySizeType::Size;
		for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < ArraySize; ++ElementIndex)
			Out += ProtocolGetSize(Argument[*ElementIndex]);
		return Out;
	}

	inline static void Write(uint8_t *&Out, std::vector<ElementType> const &Argument)
	{
		Protocol::ArraySizeType const ArraySize = (Protocol::ArraySizeType::Type)Argument.size();
		ProtocolWrite(Out, *ArraySize);
		for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < ArraySize; ++ElementIndex)
			ProtocolWrite(Out, Argument[*ElementIndex]);
	}

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::vector<ElementType> &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeType::Size) { assert(false); return false; }
		Protocol::ArraySizeType::Type const &Size = *reinterpret_cast<Protocol::ArraySizeType::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeType::Type>(sizeof(Size));
		if (Buffer.Length < StrictCast(Offset, size_t) + (size_t)Size) { assert(false); return false; }
		Data.resize(Size);
		for (Protocol::ArraySizeType ElementIndex = (Protocol::ArraySizeType::Type)0; ElementIndex < Size; ++ElementIndex)
			ProtocolRead(VersionID, MessageID, Buffer, Offset, Data[*ElementIndex]);
		return true;
	}
};

template <typename ElementType, size_t Count> struct ProtocolOperations<std::array<ElementType, Count>, typename std::enable_if<!std::is_class<ElementType>::value>::type>
{
	constexpr static size_t GetSize(std::array<ElementType, Count> const &Argument)
		{ return Count * sizeof(ElementType); }

	inline static void Write(uint8_t *&Out, std::array<ElementType, Count> const &Argument)
	{
		memcpy(Out, &Argument[0], Count * sizeof(ElementType));
		Out += Count * sizeof(ElementType);
	}

	static bool Read(Protocol::VersionIDType const &VersionID, Protocol::MessageIDType const &MessageID, Protocol::BufferType const &Buffer, Protocol::SizeType &Offset, std::array<ElementType, Count> &Data)
	{
		if (Buffer.Length < Count * sizeof(ElementType))
		{
			assert(false);
			return false;
		}
		memcpy(&Data[0], &Buffer[*Offset], Count * sizeof(ElementType));
		Offset += static_cast<Protocol::SizeType::Type>(Count * sizeof(ElementType));
		return true;
	}
};

#endif
