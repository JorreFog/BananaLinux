// (tools-test on a PC: bionic's funopen, which glibc hasn't; tools-test.cpp makes it from fopencookie)
#pragma once
#include <cstdio>
extern "C" FILE* funopen(const void*, int (*)(void*, char*, int), int (*)(void*, const char*, int), fpos_t (*)(void*, fpos_t, int), int (*)(void*));
