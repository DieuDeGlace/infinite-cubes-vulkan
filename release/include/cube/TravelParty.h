#pragma once
#include <limits>

namespace cube {

// Travelers use a separate ID range from players, residents and the ordinary summons near INT64_MIN.
constexpr long long TravelPartyIdBase = std::numeric_limits<long long>::min() + (1LL << 32);
constexpr long long TravelPartyIdLimit = TravelPartyIdBase + (1LL << 32);

constexpr bool isTravelPartyMember(long long id)
{
    return id >= TravelPartyIdBase && id < TravelPartyIdLimit;
}

}
