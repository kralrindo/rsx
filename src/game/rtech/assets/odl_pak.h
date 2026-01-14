#pragma once

struct ODLPakHeader_t
{
	const char* pakName;
	char gap8[0x10];
};

static_assert(sizeof(ODLPakHeader_t) == 0x18);