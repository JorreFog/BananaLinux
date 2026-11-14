// (tools-test on a PC: Android's log as stderr)
#pragma once
#include <cstdio>
enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5 };
#define __android_log_print(prio, tag, ...) ((void)(prio), (void)(tag), 0)
