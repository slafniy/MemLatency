#include "pch.h"
#include "QPCTimer.h"


QPCTimer::QPCTimer()
{
   QueryPerformanceFrequency(&Frequency);
   QueryPerformanceCounter(&StartingTime);
}


QPCTimer::~QPCTimer()
{
   
}

int64_t QPCTimer::GetNanoseconds()
{
   QueryPerformanceCounter(&EndingTime);
   ElapsedNanoseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
   ElapsedNanoseconds.QuadPart *= 1000000000;
   ElapsedNanoseconds.QuadPart /= Frequency.QuadPart;
   return ElapsedNanoseconds.QuadPart;
}
