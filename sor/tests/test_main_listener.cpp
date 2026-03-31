// Register the VerboseTestListener with Catch2.
// This file should be compiled into both unit and integration test executables.

#include "test_helpers.h"

CATCH_REGISTER_LISTENER(VerboseTestListener)
