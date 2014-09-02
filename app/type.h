#ifndef type_h
#define type_h

// FIXME
// I think OptionalT doesn't work if inner type is const

#include <cassert>
#include "extrastandard.h"
#include <string>
#include <sstream>

template <typename TargetT> struct ExplicitT {};

//----------------------------------------------------------------------------------------------------------------
// Exception for failed construction
struct ConstructionErrorT
{
	ConstructionErrorT(void) {}
	ConstructionErrorT(ConstructionErrorT const &Other) : Buffer(Other.Buffer.str()) {}
	template <typename Whatever> ConstructionErrorT &operator <<(Whatever const &Input) { Buffer << Input; return *this; }
	operator std::string(void) const { return Buffer.str(); }

	private:
		std::stringstream Buffer;
};

inline std::ostream& operator <<(std::ostream &Out, ConstructionErrorT const &Error)
	{ Out << static_cast<std::string>(Error); return Out; }
	
//================================================================================================================
// Strict types (no implicit casting)
/* 
Usage:

typedef StrictType(int) MyInt;
MyInt x(4);

typedef StrictType(float) MyFloat;
MyFloat y(5.0f);
MyFloat z((float)5);

y = StrictCast(x, MyFloat);

*/

template <size_t Uniqueness, typename ValueT, typename Enabled = void> struct ExplicitCastableT;
template <size_t Uniqueness, typename ValueT> 
	struct ExplicitCastableT<Uniqueness, ValueT, typename std::enable_if<!std::is_integral<ValueT>::value>::type>
{
	typedef ExplicitCastableT<Uniqueness, ValueT> ThisT;
	
	ExplicitCastableT(void) {}
	ExplicitCastableT(ThisT const &That) : Value(*That) {}
	explicit ExplicitCastableT(ValueT const &That) : Value(That) {}
	template <typename ThatT> ExplicitCastableT(ThatT const &That) : Value(static_cast<ValueT>(That)) {}
	
	ValueT &operator *(void) { return Value; }
	ValueT const &operator *(void) const { return Value; }
	
	//template <typename ...ArgumentsT> constexpr decltype(reinterpret_cast<ValueT *>(nullptr)->operator ()) operator()(ArgumentsT... Arguments)
	//	{ return Value(Arguments...); }
	template <typename ...ArgumentsT> auto operator()(ArgumentsT... Arguments)
		-> typename std::result_of<ValueT(ArgumentsT...)>::type
		{ return Value(std::forward<ArgumentsT>(Arguments)...); }
	template <typename ...ArgumentsT> auto operator()(ArgumentsT... Arguments) const
		-> typename std::result_of<ValueT(ArgumentsT...)>::type
		{ return Value(std::forward<ArgumentsT>(Arguments)...); }

	constexpr static size_t Size = sizeof(ValueT);
	typedef ValueT Type;
	ValueT Value;
};
template <size_t Uniqueness, typename ValueT> constexpr size_t ExplicitCastableT<Uniqueness, ValueT, typename std::enable_if<!std::is_integral<ValueT>::value>::type>::Size;

template <size_t Uniqueness, typename ValueT> struct ExplicitCastableT<Uniqueness, ValueT, typename std::enable_if<std::is_integral<ValueT>::value>::type>
{
	typedef ExplicitCastableT<Uniqueness, ValueT> ThisType;

	constexpr ExplicitCastableT(void) {}
	constexpr ExplicitCastableT(ThisType const &That) : Value(*That) {}
	explicit constexpr ExplicitCastableT(ValueT const &That) : Value(That) {}
	template <typename ThatType> explicit constexpr ExplicitCastableT(ThatType const &That) : Value(static_cast<ValueT>(That)) {}

	ValueT &operator *(void) { return Value; }
	constexpr ValueT const &operator *(void) const { return Value; }
	constexpr static size_t Size = sizeof(ValueT);

	constexpr ThisType operator +(ThisType const &That) const { return ThisType(Value + *That); }
	constexpr ThisType operator +(ValueT const &That) const { return ThisType(Value + That); }
	template <typename ThatType> ThisType operator +(ThatType const &) const = delete;

	constexpr ThisType operator -(ThisType const &That) const { return ThisType(Value - *That); }
	constexpr ThisType operator -(ValueT const &That) const { return ThisType(Value - That); }
	template <typename ThatType> ThisType operator -(ThatType const &) const = delete;

	constexpr ThisType operator *(ThisType const &That) const { return ThisType(Value * *That); }
	constexpr ThisType operator *(ValueT const &That) const { return ThisType(Value * That); }
	template <typename ThatType> ThisType operator *(ThatType const &) const = delete;

	constexpr ThisType operator /(ThisType const &That) const { return ThisType(Value / *That); }
	constexpr ThisType operator /(ValueT const &That) const { return ThisType(Value / That); }
	template <typename ThatType> ThisType operator /(ThatType const &) const = delete;

	template <typename ThatType> ThisType operator +=(ThatType const &) = delete;
	ThisType operator +=(ThisType const &That) { return ThisType(Value += *That); }
	ThisType operator +=(ValueT const &That) { return ThisType(Value += That); }

	ThisType operator ++(void) { return ++**this; }

	ThisType operator -=(ThisType const &That) { return ThisType(Value -= *That); }
	ThisType operator -=(ValueT const &That) { return ThisType(Value -= That); }
	template <typename ThatType> ThisType operator -=(ThatType const &) = delete;

	ThisType operator --(void) { return --**this; }

	ThisType operator *=(ThisType const &That) { return ThisType(Value *= *That); }
	ThisType operator *=(ValueT const &That) { return ThisType(Value *= That); }
	template <typename ThatType> ThisType operator *=(ThatType const &) = delete;

	ThisType operator /=(ThisType const &That) { return ThisType(Value /= *That); }
	ThisType operator /=(ValueT const &That) { return ThisType(Value /= That); }
	template <typename ThatType> ThisType operator /=(ThatType const &) = delete;

	constexpr bool operator ==(ThisType const &That) const { return Value == *That; }
	constexpr bool operator ==(ValueT const &That) const { return Value == That; }
	template <typename ThatType> constexpr bool operator ==(ThatType const &) const = delete;

	constexpr bool operator !=(ThisType const &That) const { return Value != *That; }
	constexpr bool operator !=(ValueT const &That) const { return Value != That; }
	template <typename ThatType> constexpr bool operator !=(ThatType const &) const = delete;

	constexpr bool operator <(ThisType const &That) const { return Value < *That; }
	constexpr bool operator <(ValueT const &That) const { return Value < That; }
	template <typename ThatType> constexpr bool operator <(ThatType const &) const = delete;

	constexpr bool operator >(ThisType const &That) const { return Value > *That; }
	constexpr bool operator >(ValueT const &That) const { return Value > That; }
	template <typename ThatType> constexpr bool operator >(ThatType const &) const = delete;

	constexpr bool operator <=(ThisType const &That) const { return Value <= *That; }
	constexpr bool operator <=(ValueT const &That) const { return Value <= That; }
	template <typename ThatType> constexpr bool operator <=(ThatType const &) const = delete;

	constexpr bool operator >=(ThisType const &That) const { return Value >= *That; }
	constexpr bool operator >=(ValueT const &That) const { return Value >= That; }
	template <typename ThatType> constexpr bool operator >=(ThatType const &) const = delete;
	
	typedef ValueT Type;
	ValueT Value;
};
template <size_t Uniqueness, typename ValueT> constexpr size_t ExplicitCastableT<Uniqueness, ValueT, typename std::enable_if<std::is_integral<ValueT>::value>::type>::Size;

template <size_t Uniqueness, typename ExplicitT> std::ostream &operator <<(std::ostream &Stream, ExplicitCastableT<Uniqueness, ExplicitT> const &Value)
	{ Stream << *Value; return Stream; }

#define StrictType(Type) ::ExplicitCastableT<__COUNTER__, Type>
//#define StrictCast(Value, ToType) (Value)(ExplicitT<ToType>())
#define StrictCast(StrictValue, ToType) static_cast<ToType>((StrictValue).Value)

//================================================================================================================
// Variants (algebraic types)

#if !defined(NDEBUG) && (defined(_CPPRTTI) || defined(__GXX_RTTI))
#include <typeinfo>
typedef char const * VariantTagT;
#define VARIANTTAG typeid(CurrentT).name()
#else
typedef uintptr_t VariantTagT;
template <typename VariantT> struct VariantIDT { static char Location; };
template <typename VariantT> char VariantIDT<VariantT>::Location;
template <typename VariantT> struct VariantIDT<VariantT *> : VariantIDT<VariantT> 
	{ using VariantIDT<VariantT>::Location; };
template <typename VariantT> struct VariantIDT<VariantT const *> : VariantIDT<VariantT> 
	{ using VariantIDT<VariantT>::Location; };
template <typename VariantT> struct VariantIDT<VariantT &> : VariantIDT<VariantT> 
	{ using VariantIDT<VariantT>::Location; };
template <typename VariantT> struct VariantIDT<VariantT &&> : VariantIDT<VariantT> 
	{ using VariantIDT<VariantT>::Location; };
template <typename VariantT> struct VariantIDT<VariantT const &> : VariantIDT<VariantT> 
	{ using VariantIDT<VariantT>::Location; };
//#define VARIANTTAG reinterpret_cast<uintptr_t>(&UnionT::UnionT)
//#define VARIANTTAG reinterpret_cast<uintptr_t>((void(*)(UnionT &, UnionT const &, VariantInternalsT<CurrentT, RemainingT...> const &))&VariantInternalsT::VariantInternalsT)
static_assert(&VariantIDT<int>::Location == &VariantIDT<int &>::Location, "VariantIDT broken.");
static_assert(&VariantIDT<int>::Location == &VariantIDT<int *>::Location, "VariantIDT broken.");
static_assert(&VariantIDT<int>::Location == &VariantIDT<int const *>::Location, "VariantIDT broken.");
static_assert(&VariantIDT<int>::Location == &VariantIDT<int &&>::Location, "VariantIDT broken.");
#define VARIANTTAG reinterpret_cast<uintptr_t>(&VariantIDT<decltype(this)>::Location)
#endif
	
template <typename ...TypesT> struct VariantInternalsT;

template <typename CurrentT, typename ...RemainingT> struct VariantInternalsT<CurrentT, RemainingT...> : VariantInternalsT<RemainingT...>
{
	union UnionT
	{
		UnionT(void) {} 
		~UnionT(void) {} 
		CurrentT Value;
		typename VariantInternalsT<RemainingT...>::UnionT Next;
	};
	
	using VariantInternalsT<RemainingT...>::VariantInternalsT;
	
	template <typename TypeT, typename ValueT> VariantInternalsT(ExplicitT<TypeT> ValueTag, UnionT &Union, ValueT const &Value) : 
		VariantInternalsT<RemainingT...>(ValueTag, Union.Next, Value) {}
		
	template <typename TypeT, typename ValueT> VariantInternalsT(ExplicitT<TypeT> ValueTag, UnionT &Union, ValueT &&Value) : 
		VariantInternalsT<RemainingT...>(ValueTag, Union.Next, std::move(Value)) {}
		
	template <typename ValueT> VariantInternalsT(ExplicitT<CurrentT>, UnionT &Union, ValueT const &Value) : 
		VariantInternalsT<RemainingT...>(VARIANTTAG)
		{ new (&Union.Value) CurrentT(Value); }

	template <typename ValueT> VariantInternalsT(ExplicitT<CurrentT>, UnionT &Union, ValueT &&Value) : 
		VariantInternalsT<RemainingT...>(VARIANTTAG)
		{ new (&Union.Value) CurrentT(std::move(Value)); }
		
	VariantInternalsT(UnionT &Union, UnionT const &OtherUnion, VariantInternalsT<CurrentT, RemainingT...> const &Other) : 
		VariantInternalsT<RemainingT...>(Union.Next, OtherUnion.Next, Other)
		{ if (Other.Tag == VARIANTTAG) new (&Union.Value) CurrentT(OtherUnion.Value); }

	VariantInternalsT(UnionT &Union, UnionT &&OtherUnion, VariantInternalsT<CurrentT, RemainingT...> &Other) : 
		VariantInternalsT<RemainingT...>(Union.Next, OtherUnion.Next, std::move(Other))
		{ if (Other.Tag == VARIANTTAG) new (&Union.Value) CurrentT(std::move(OtherUnion.Value)); }

	using VariantInternalsT<RemainingT...>::Is;

	bool Is(ExplicitT<CurrentT>) const { return this->Tag == VARIANTTAG; }
	
	using VariantInternalsT<RemainingT...>::Set;
		
	template <typename TypeT, typename ValueT> TypeT &Set(ExplicitT<TypeT> ValueTag, UnionT &Union, ValueT const &Value)
	{
		if (this->Tag == VARIANTTAG) DestroyImmediately(Union);
		return Set(ValueTag, Union.Next, Value);
	}

	template <typename TypeT, typename ValueT> TypeT &Set(ExplicitT<TypeT> ValueTag, UnionT &Union, ValueT &&Value)
	{
		if (this->Tag == VARIANTTAG) DestroyImmediately(Union);
		return Set(ValueTag, Union.Next, std::move(Value));
	}
	
	template <typename ValueT> CurrentT &Set(ExplicitT<CurrentT>, UnionT &Union, ValueT const &Value)
	{
		if (this->Tag == VARIANTTAG) { return Union.Value = Value; }
		if (this->Tag) Destroy(Union);
		this->Tag = VARIANTTAG;
		new (&Union.Value) CurrentT(Value);
		return Union.Value;
	}
	
	template <typename ValueT> CurrentT &Set(ExplicitT<CurrentT>, UnionT &Union, ValueT &&Value)
	{
		if (this->Tag == VARIANTTAG) { return Union.Value = std::move(Value); }
		if (this->Tag) Destroy(Union);
		this->Tag = VARIANTTAG;
		new (&Union.Value) CurrentT(std::move(Value));
		return Union.Value;
	}
	
	void Set(UnionT &Union, UnionT const &OtherUnion, VariantTagT const &OtherTag)
	{
		if (OtherTag == VARIANTTAG) Set(ExplicitT<CurrentT>(), Union, OtherUnion.Value);
		else 
		{
			if (this->Tag == VARIANTTAG) DestroyImmediately(Union);
			Set(Union.Next, OtherUnion.Next, OtherTag);
		}
	}
	
	void Set(UnionT &Union, UnionT &&OtherUnion, VariantTagT const &OtherTag)
	{
		if (OtherTag == VARIANTTAG) Set(ExplicitT<CurrentT>(), Union, std::move(OtherUnion.Value));
		else 
		{
			if (this->Tag == VARIANTTAG) DestroyImmediately(Union);
			VariantInternalsT<RemainingT...>::Set(Union.Next, std::move(OtherUnion.Next), OtherTag);
		}
	}
	
	template <typename TypeT> TypeT &Get(ExplicitT<TypeT>, UnionT &Union) 
		{ return VariantInternalsT<RemainingT...>::Get(ExplicitT<TypeT>(), Union.Next); }
		
	template <typename TypeT> TypeT const &Get(ExplicitT<TypeT>, UnionT const &Union) const
		{ return VariantInternalsT<RemainingT...>::Get(ExplicitT<TypeT>(), Union.Next); }
	
	CurrentT &Get(ExplicitT<CurrentT>, UnionT &Union)
	{
		Assert(Is(ExplicitT<CurrentT>()));
		return Union.Value; 
	}

	CurrentT const &Get(ExplicitT<CurrentT>, UnionT const &Union) const
	{ 
		Assert(Is(ExplicitT<CurrentT>()));
		return Union.Value; 
	}
		
	void DestroyImmediately(UnionT &Union)
		{ AssertE(this->Tag, VARIANTTAG); Union.Value.~CurrentT(); }
	
	void DestroyImmediately(UnionT const &Union) const 
		{ AssertE(this->Tag, VARIANTTAG); Union.Value.~CurrentT(); }
		
	using VariantInternalsT<RemainingT...>::Destroy;
	
	void Destroy(UnionT &Union)
		{ if (this->Tag == VARIANTTAG) Union.Value.~CurrentT(); else Destroy(Union.Next); }
	
	void Destroy(UnionT const &Union) const 
		{ if (this->Tag == VARIANTTAG) Union.Value.~CurrentT(); else Destroy(Union.Next); }
};

template <> struct VariantInternalsT<>
{
	struct InvalidT {};
	union UnionT 
	{ 
		UnionT(void) {} 
		~UnionT(void) {}
	};
	VariantTagT Tag;
	
	VariantInternalsT(VariantTagT Tag) : Tag(Tag) {}
	
	VariantInternalsT(UnionT &Union, UnionT const &OtherUnion, VariantInternalsT<> const &Other) : Tag(Other.Tag) {}
	
	VariantInternalsT(UnionT &Union, UnionT &&OtherUnion, VariantInternalsT<> &Other) : Tag(Other.Tag) { Other.Tag = 0; }
	
	VariantInternalsT(VariantInternalsT<> &&Other) : Tag(Other.Tag) { Other.Tag = 0; }

	template <typename TypeT> bool Is(ExplicitT<TypeT>) const { return false; }
	
	void Set(UnionT &Union, UnionT const &OtherUnion, VariantTagT const &OtherTag) { if (OtherTag == 0) Tag = 0; }
	
	void Set(UnionT &Union, UnionT &&OtherUnion, VariantTagT const &OtherTag) { if (OtherTag == 0) Tag = 0; }
	
	template <typename ValueT> InvalidT &Set(ExplicitT<InvalidT> ValueTag, UnionT &Union, ValueT const &Value) { Tag = 0; return *reinterpret_cast<InvalidT *>(0); }
	
	template <typename ValueT> InvalidT &Set(ExplicitT<InvalidT> ValueTag, UnionT &Union, ValueT &&Value) { Tag = 0; return *reinterpret_cast<InvalidT *>(0); }

	void Destroy(UnionT &Union) {}
	
	void Destroy(UnionT const &Union) const {}

	std::string Dump(void) const { return StringT() << Tag; }
};
	
template <typename... TypesT> struct VariantT : private VariantInternalsT<TypesT...>
{
	protected:
		typedef VariantInternalsT<TypesT...> InternalsT;
		typename InternalsT::UnionT UnionBytes;
		#define Union this->UnionBytes
		#define ValueUnion Value.UnionBytes
	public:
		VariantT(void) : InternalsT(0) {}
		
		template <typename ValueT> explicit VariantT(ValueT const &Value) : 
			InternalsT(ExplicitT<ValueT>(), Union, Value) {}
			
		template <typename TypeT, typename ValueT> VariantT(ExplicitT<TypeT> ValueTag, ValueT const &Value) : 
			InternalsT(ValueTag, Union, Value) {}
			
		template 
		<
			typename ValueT,
			typename = typename std::enable_if<std::is_same<ValueT, typename std::decay<ValueT>::type>::value>::type
		> 
			VariantT(ValueT &&Value) : 
			InternalsT(ExplicitT<ValueT>(), Union, std::move(Value)) {}
			
		template // C++ is my favorite language
		<
			typename TypeT, 
			typename ValueT, 
			typename = typename std::enable_if<std::is_same<TypeT, typename std::decay<TypeT>::type>::value>::type
		> 
			VariantT(ExplicitT<TypeT> ValueTag, ValueT &&Value) : 
			InternalsT(ValueTag, Union, std::move(Value)) {}
			
		VariantT(VariantT<TypesT...> const &Value) : InternalsT(Union, ValueUnion, Value) {}
		
		VariantT(VariantT<TypesT...> &&Value) : InternalsT(Union, std::move(ValueUnion), Value) {}
		
		~VariantT(void) { this->Destroy(Union); }
		
		void Clear(void) { Destroy(Union); this->Tag = nullptr; }
		
		operator bool(void) const { return this->Tag; }
		
		bool operator !(void) const { return !this->Tag; }
		
		template <typename ValueT> ValueT &operator =(ValueT const &Value) 
			{ return InternalsT::Set(ExplicitT<ValueT>(), Union, Value); }
			
		template <typename ValueT> ValueT &operator =(ValueT &&Value) 
			{ return InternalsT::Set(ExplicitT<ValueT>(), Union, std::move(Value)); }
		
		VariantT<TypesT...> &operator =(VariantT<TypesT...> const &Value) 
			{ InternalsT::Set(Union, ValueUnion, Value.Tag); return *this; }
			
		VariantT<TypesT...> &operator =(VariantT<TypesT...> &&Value) 
			{ InternalsT::Set(Union, std::move(ValueUnion), Value.Tag); return *this; }
		
		void Unset(void) { InternalsT::Set(ExplicitT<typename InternalsT::InvalidT>(), Union, 0); }
		
		template <typename TypeT> bool Is(void) const 
			{ return InternalsT::Is(ExplicitT<TypeT>()); }
		
		template <typename TypeT, typename ValueT> TypeT &Set(ValueT const &Value) 
			{ return InternalsT::Set(ExplicitT<TypeT>(), Union, Value); }
			
		template <typename TypeT, typename ValueT> TypeT &Set(ValueT &&Value) 
			{ return InternalsT::Set(ExplicitT<TypeT>(), Union, std::move(Value)); }
			
		template <typename TypeT> TypeT &Get(void) { return InternalsT::Get(ExplicitT<TypeT>(), Union); }
		
		template <typename TypeT> TypeT const &Get(void) const { return InternalsT::Get(ExplicitT<TypeT>(), Union); }

		using VariantInternalsT<TypesT...>::Dump;
};

//----------------------------------------------------------------------------------------------------------------
// Optional (none or type)
template <typename TypeT> struct OptionalT : public VariantT<TypeT>
{
	private:
		template <typename CheckT, typename = typename std::enable_if<std::is_pointer<CheckT>::value>::type> 
			static CheckT Access(CheckT &Value) { return Value; }
		template <typename CheckT, typename = typename std::enable_if<std::is_pointer<CheckT>::value>::type> 
			static CheckT const Access(CheckT const &Value) { return Value; }
		template <typename CheckT, typename = typename std::enable_if<!std::is_pointer<CheckT>::value>::type> 
			static CheckT *Access(CheckT &Value) { return &Value; }
		template <typename CheckT, typename = typename std::enable_if<!std::is_pointer<CheckT>::value>::type> 
			static CheckT const *Access(CheckT const &Value) { return &Value; }
	public:
		using VariantT<TypeT>::Set;
		
		OptionalT(void) {}
		
		OptionalT(OptionalT<TypeT> const &Value) : VariantT<TypeT>((VariantT<TypeT> const &)Value) {}
		
		OptionalT(OptionalT<TypeT> &&Value) : VariantT<TypeT>((VariantT<TypeT> &&)std::move(Value)) {}
	
		OptionalT(TypeT const &Value) : VariantT<TypeT>(Value) {}
		
		OptionalT(TypeT &&Value) : VariantT<TypeT>(std::move(Value)) {}
		
		void Set(TypeT const &Value) { this->template Set<TypeT>(Value); }
		
		void Set(TypeT &&Value) { this->template Set<TypeT>(std::move(Value)); }
		
		OptionalT<TypeT> &operator =(OptionalT<TypeT> const &Value)
			{ VariantT<TypeT>::operator =((VariantT<TypeT> const &)Value); return *this; }
		
		template <typename ValueT> TypeT &operator =(ValueT const &Value) 
			{ return this->template Set<TypeT>(Value); }
		
		OptionalT<TypeT> &operator =(OptionalT<TypeT> &&Value)
			{ VariantT<TypeT>::operator =((VariantT<TypeT> &&)std::move(Value)); return *this; }
			
		template 
		<
			typename ValueT,
			typename = typename std::enable_if<std::is_same<ValueT, typename std::decay<ValueT>::type>::value>::type
		>  // C++ standards group for president
			TypeT &operator =(ValueT &&Value) 
			{ return this->template Set<TypeT>(std::move(Value)); }
		
		TypeT &operator *(void) { return VariantT<TypeT>::template Get<TypeT>(); }
		
		TypeT const &operator *(void) const { return VariantT<TypeT>::template Get<TypeT>(); }
		
		decltype(Access(*((TypeT *)0))) operator ->(void) 
			{ return Access(VariantT<TypeT>::template Get<TypeT>()); }
			
		decltype(Access(*((TypeT const *)0))) operator ->(void) const 
			{ return Access(&VariantT<TypeT>::template Get<TypeT>()); }
};

//----------------------------------------------------------------------------------------------------------------
// Error variants (error or type)
template <typename DefaultT, typename AltT> struct DefaultVariantT : public VariantT<DefaultT, AltT>
{
	DefaultT &operator *(void) { return VariantT<DefaultT>::template Get<DefaultT>(); }
	
	DefaultT const &operator *(void) const { return VariantT<DefaultT>::template Get<DefaultT>(); }
	
	decltype(Access(*((DefaultT *)0))) operator ->(void) 
		{ return Access(VariantT<DefaultT>::template Get<DefaultT>()); }
		
	decltype(Access(*((DefaultT const *)0))) operator ->(void) const 
		{ return Access(&VariantT<DefaultT>::template Get<DefaultT>()); }

	AltT &Alt(void) { return VariantT<DefaultT, AltT>::template Get<AltT>(); }

	AltT const &Alt(void) const { return VariantT<DefaultT, AltT>::template Get<AltT>(); }
};

/*template <typename ErrorT, typename TypeT> struct ErrorOrT : public VariantT<ErrorT, TypeT>
{
	operator bool(void) const { return VariantT<ErrorT, TypeT>::template Is<ErrorT>(); }
	bool operator !(void) const { return !VariantT<ErrorT, TypeT>::template Is<ErrorT>(); }
	TypeT &operator *(void) { return VariantT<ErrorT, TypeT>::template Get<TypeT>(); }
	TypeT const &operator *(void) const { return VariantT<ErrorT, TypeT>::template Get<TypeT>(); }
	TypeT *operator ->(void) { return &VariantT<ErrorT, TypeT>::template Get<TypeT>(); }
	TypeT const *operator ->(void) const { return &VariantT<ErrorT, TypeT>::template Get<TypeT>(); }
};*/

#endif
