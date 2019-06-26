#pragma once

#include <Windows.h>
#include <stdint.h>

class QPCTimer
{
   LARGE_INTEGER StartingTime, EndingTime, ElapsedNanoseconds;
   LARGE_INTEGER Frequency;
public:
   QPCTimer();
   ~QPCTimer();
   int64_t GetNanoseconds();
};

