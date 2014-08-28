#ifndef protocoloperations_h
#define protocoloperations_h

template <typename IntT> struct ProtocolOperations<IntT, typename std::enable_if<std::is_integral<IntT>::value>::type>
{
	constexpr static size_t GetSize(IntT const &Argument) { return sizeof(IntT); }

	inline static void Write(uint8_t *&Out, IntT const &Argument)
		{ *reinterpret_cast<IntT *>(Out) = Argument; Out += sizeof(Argument); }

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, IntT &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + sizeof(IntT))
		{
			assert(false);
			return false;
		}

		Data = *reinterpret_cast<IntT const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeT::Type>(sizeof(IntT));
		return true;
	}
};

template <size_t Uniqueness, typename Type> struct ProtocolOperations<ExplicitCastableT<Uniqueness, Type>, void>
{
	typedef ExplicitCastableT<Uniqueness, Type> Explicit;
	constexpr static size_t GetSize(Explicit const &Argument)
		{ return ProtocolOperations<Type>::GetSize(*Argument); }

	inline static void Write(uint8_t *&Out, Explicit const &Argument)
		{ ProtocolOperations<Type>::Write(Out, *Argument); }

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, Explicit &Data)
		{ return ProtocolOperations<Type>::Read(VersionID, MessageID, Buffer, Offset, *Data); }
};

template <> struct ProtocolOperations<std::string, void>
{
	static size_t GetSize(std::string const &Argument)
	{
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeT::Type>::max());
		return {Protocol::SizeT::Type(Protocol::ArraySizeT::Size + Argument.size())};
	}

	inline static void Write(uint8_t *&Out, std::string const &Argument)
	{
		Protocol::ArraySizeT const StringSize = Protocol::ArraySizeT(Argument.size());
		ProtocolWrite(Out, *StringSize);
		memcpy(Out, Argument.c_str(), Argument.size());
		Out += Argument.size();
	}

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, std::string &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeT::Size)
		{
			assert(false);
			return false;
		}
		Protocol::ArraySizeT::Type const &Size = *reinterpret_cast<Protocol::ArraySizeT::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeT::Type>(sizeof(Size));
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
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeT::Type>::max());
		return Protocol::ArraySizeT::Size + (Protocol::ArraySizeT::Type)Argument.size() * sizeof(ElementType);
	}

	inline static void Write(uint8_t *&Out, std::vector<ElementType> const &Argument)
	{
		Protocol::ArraySizeT const ArraySize = Protocol::ArraySizeT(Argument.size());
		ProtocolWrite(Out, *ArraySize);
		memcpy(Out, &Argument[0], Argument.size() * sizeof(ElementType));
		Out += Argument.size() * sizeof(ElementType);
	}

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, std::vector<ElementType> &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeT::Size)
		{
			assert(false);
			return false;
		}
		Protocol::ArraySizeT::Type const &Size = *reinterpret_cast<Protocol::ArraySizeT::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeT::Type>(sizeof(Size));
		if (Buffer.Length < StrictCast(Offset, size_t) + Size * sizeof(ElementType))
		{
			assert(false);
			return false;
		}
		Data.resize(Size);
		memcpy(&Data[0], &Buffer[*Offset], Size * sizeof(ElementType));
		Offset += static_cast<Protocol::SizeT::Type>(Size * sizeof(ElementType));
		return true;
	}
};

template <typename ElementType> struct ProtocolOperations<std::vector<ElementType>, typename std::enable_if<std::is_class<ElementType>::value>::type>
{
	static size_t GetSize(std::vector<ElementType> const &Argument)
	{
		assert(Argument.size() <= std::numeric_limits<Protocol::ArraySizeT::Type>::max());
		Protocol::ArraySizeT const ArraySize = Protocol::ArraySizeT(Argument.size());
		size_t Out = Protocol::ArraySizeT::Size;
		for (Protocol::ArraySizeT ElementIndex = Protocol::ArraySizeT(0); ElementIndex < ArraySize; ++ElementIndex)
			Out += ProtocolGetSize(Argument[*ElementIndex]);
		return Out;
	}

	inline static void Write(uint8_t *&Out, std::vector<ElementType> const &Argument)
	{
		Protocol::ArraySizeT const ArraySize = Protocol::ArraySizeT(Argument.size());
		ProtocolWrite(Out, *ArraySize);
		for (Protocol::ArraySizeT ElementIndex = Protocol::ArraySizeT(0); ElementIndex < ArraySize; ++ElementIndex)
			ProtocolWrite(Out, Argument[*ElementIndex]);
	}

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, std::vector<ElementType> &Data)
	{
		if (Buffer.Length < StrictCast(Offset, size_t) + Protocol::ArraySizeT::Size) { assert(false); return false; }
		Protocol::ArraySizeT::Type const &Size = *reinterpret_cast<Protocol::ArraySizeT::Type const *>(&Buffer[*Offset]);
		Offset += static_cast<Protocol::SizeT::Type>(sizeof(Size));
		if (Buffer.Length < StrictCast(Offset, size_t) + (size_t)Size) { assert(false); return false; }
		Data.resize(Size);
		for (Protocol::ArraySizeT ElementIndex = Protocol::ArraySizeT(0); ElementIndex < Size; ++ElementIndex)
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

	static bool Read(Protocol::VersionIDT const &VersionID, Protocol::MessageIDT const &MessageID, Protocol::BufferT const &Buffer, Protocol::SizeT &Offset, std::array<ElementType, Count> &Data)
	{
		if (Buffer.Length < Count * sizeof(ElementType))
		{
			assert(false);
			return false;
		}
		memcpy(&Data[0], &Buffer[*Offset], Count * sizeof(ElementType));
		Offset += static_cast<Protocol::SizeT::Type>(Count * sizeof(ElementType));
		return true;
	}
};

#endif
