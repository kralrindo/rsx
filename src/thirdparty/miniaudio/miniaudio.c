#define MINIAUDIO_IMPLEMENTATION

// Since RSX compiles with /Wx, we need to reduce the warning level when compiling
// miniaudio.h, as otherwise building will fail
#pragma warning(push, 2)
#include "miniaudio.h"
#pragma warning(pop)