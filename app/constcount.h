#ifndef constcount_h
#define constcount_h

#include <iostream>
#include <type_traits>

// Credit: Potatoswatter http://stackoverflow.com/a/6174263/1190123
namespace ConstCount
{
	template <std::size_t n> using Index = std::integral_constant<std::size_t, n>;

	template <typename TagID, std::size_t rank, std::size_t acc>
		constexpr Index<acc> Crumb(TagID, Index<rank>, Index<acc>) { return {}; }
}

#define GetConstCountCrumb(TagID, rank, acc) \
	ConstCount::Crumb(TagID(), ConstCount::Index<rank>(), ConstCount::Index<acc>())

#define GetConstCount(TagID) \
	GetConstCountCrumb(TagID, 1, \
		GetConstCountCrumb(TagID, 2, \
			GetConstCountCrumb(TagID, 4, \
				GetConstCountCrumb(TagID, 8, \
					GetConstCountCrumb(TagID, 16, \
						GetConstCountCrumb(TagID, 32, \
							GetConstCountCrumb(TagID, 64, \
								GetConstCountCrumb(TagID, 128, 0) \
							) \
						) \
					) \
				) \
			) \
		) \
	)

#define IncrementConstCount(TagID) \
namespace ConstCount \
{ \
	constexpr Index<GetConstCount(TagID) + 1> \
		Crumb( \
			TagID, \
			Index<(GetConstCount(TagID) + 1) & ~GetConstCount(TagID)>, \
			Index<(GetConstCount(TagID) + 1) & GetConstCount(TagID)>) \
	{ \
		return {}; \
	} \
}

#endif
