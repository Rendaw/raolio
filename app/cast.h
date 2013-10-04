#ifndef cast_h
#define cast_h

template <typename TargetType> struct ExplicitType {};
template <size_t Uniqueness, typename ValueType, typename Enabled = void> struct ExplicitCastable;
template <size_t Uniqueness, typename ValueType> struct ExplicitCastable<Uniqueness, ValueType, typename std::enable_if<!std::is_integral<ValueType>::value>::type>
{
	template <typename TargetType> TargetType operator()(ExplicitType<TargetType>) const { return static_cast<TargetType>(Value); }
	ValueType &operator *(void) { return Value; }
	ValueType const &operator *(void) const { return Value; }
	constexpr static size_t Size = sizeof(ValueType);
	typedef ValueType Type;
	ValueType Value;
};
template <size_t Uniqueness, typename ValueType> constexpr size_t ExplicitCastable<Uniqueness, ValueType, typename std::enable_if<!std::is_integral<ValueType>::value>::type>::Size;

template <size_t Uniqueness, typename ValueType> struct ExplicitCastable<Uniqueness, ValueType, typename std::enable_if<std::is_integral<ValueType>::value>::type>
{
	typedef ExplicitCastable<Uniqueness, ValueType> ThisType;

	constexpr ExplicitCastable(void) {}
	constexpr ExplicitCastable(ThisType const &That) : Value(*That) {}
	constexpr ExplicitCastable(ValueType const &That) : Value(That) {}
	template <typename ThatType> constexpr ExplicitCastable(ThatType const &) = delete;

	template <typename TargetType> TargetType operator()(ExplicitType<TargetType>) const { return static_cast<TargetType>(Value); }
	ValueType &operator *(void) { return Value; }
	constexpr ValueType const &operator *(void) const { return Value; }
	constexpr static size_t Size = sizeof(ValueType);

	constexpr ThisType operator +(ThisType const &That) const { return Value + *That; }
	constexpr ThisType operator +(ValueType const &That) const { return Value + That; }
	template <typename ThatType> ThisType operator +(ThatType const &) const = delete;

	constexpr ThisType operator -(ThisType const &That) const { return Value - *That; }
	constexpr ThisType operator -(ValueType const &That) const { return Value - That; }
	template <typename ThatType> ThisType operator -(ThatType const &) const = delete;

	constexpr ThisType operator *(ThisType const &That) const { return Value * *That; }
	constexpr ThisType operator *(ValueType const &That) const { return Value * That; }
	template <typename ThatType> ThisType operator *(ThatType const &) const = delete;

	constexpr ThisType operator /(ThisType const &That) const { return Value / *That; }
	constexpr ThisType operator /(ValueType const &That) const { return Value / That; }
	template <typename ThatType> ThisType operator /(ThatType const &) const = delete;

	template <typename ThatType> ThisType operator +=(ThatType const &) = delete;
	ThisType operator +=(ThisType const &That) { return Value += *That; }
	ThisType operator +=(ValueType const &That) { return Value += That; }

	ThisType operator ++(void) { return ++**this; }

	ThisType operator -=(ThisType const &That) { return Value -= *That; }
	ThisType operator -=(ValueType const &That) { return Value -= That; }
	template <typename ThatType> ThisType operator -=(ThatType const &) = delete;

	ThisType operator --(void) { return --**this; }

	ThisType operator *=(ThisType const &That) { return Value *= *That; }
	ThisType operator *=(ValueType const &That) { return Value *= That; }
	template <typename ThatType> ThisType operator *=(ThatType const &) = delete;

	ThisType operator /=(ThisType const &That) { return Value /= *That; }
	ThisType operator /=(ValueType const &That) { return Value /= That; }
	template <typename ThatType> ThisType operator /=(ThatType const &) = delete;

	constexpr bool operator ==(ThisType const &That) const { return Value == *That; }
	constexpr bool operator ==(ValueType const &That) const { return Value == That; }
	template <typename ThatType> constexpr bool operator ==(ThatType const &) const = delete;

	constexpr bool operator !=(ThisType const &That) const { return Value != *That; }
	constexpr bool operator !=(ValueType const &That) const { return Value != That; }
	template <typename ThatType> constexpr bool operator !=(ThatType const &) const = delete;

	constexpr bool operator <(ThisType const &That) const { return Value < *That; }
	constexpr bool operator <(ValueType const &That) const { return Value < That; }
	template <typename ThatType> constexpr bool operator <(ThatType const &) const = delete;

	constexpr bool operator >(ThisType const &That) const { return Value > *That; }
	constexpr bool operator >(ValueType const &That) const { return Value > That; }
	template <typename ThatType> constexpr bool operator >(ThatType const &) const = delete;

	constexpr bool operator <=(ThisType const &That) const { return Value <= *That; }
	constexpr bool operator <=(ValueType const &That) const { return Value <= That; }
	template <typename ThatType> constexpr bool operator <=(ThatType const &) const = delete;

	constexpr bool operator >=(ThisType const &That) const { return Value >= *That; }
	constexpr bool operator >=(ValueType const &That) const { return Value >= That; }
	template <typename ThatType> constexpr bool operator >=(ThatType const &) const = delete;

	typedef ValueType Type;
	ValueType Value;
};
template <size_t Uniqueness, typename ValueType> constexpr size_t ExplicitCastable<Uniqueness, ValueType, typename std::enable_if<std::is_integral<ValueType>::value>::type>::Size;

#define StrictType(Type) ::ExplicitCastable<__COUNTER__, Type>
#define StrictCast(Value, ToType) Value(ExplicitType<ToType>())


#endif
