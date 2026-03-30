// venue_adapter.cpp -- compilation unit for the abstract VenueAdapter.
// All methods are either pure-virtual or defined inline in the header.
// This file exists to satisfy the build system and anchor the vtable.

#include "connectors/venue_adapter.h"

namespace sor::connectors
{

    // VenueAdapter destructor is defaulted in the header; no out-of-line
    // definitions are required at this time.  The translation unit ensures
    // the vtable is emitted exactly once.

} // namespace sor::connectors
