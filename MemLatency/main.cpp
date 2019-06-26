#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "util.h"
#include "asm-opt.h"
#include "version.h"
#include "main.h"
#include <Windows.h>
#include <type_traits>
#include <chrono>

#include "pch.h"

#define SIZE             (32 * 1024 * 1024)
#define BLOCKSIZE        2048
#ifndef MAXREPEATS
# define MAXREPEATS      10
#endif
#ifndef LATBENCH_COUNT
# define LATBENCH_COUNT  10000000
#endif

// Choose the most accurate timer possible
using Clock = std::conditional<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>::type;


void memcpy_wrapper(int64_t *dst, int64_t *src, int size)
{
   memcpy(dst, src, size);
}

void memset_wrapper(int64_t *dst, int64_t *src, int size)
{
   memset(dst, src[0], size);
}

static bench_info c_benchmarks[] =
{
    { "C copy backwards", 0, aligned_block_copy_backwards },
    { "C copy backwards (32 byte blocks)", 0, aligned_block_copy_backwards_bs32 },
    { "C copy backwards (64 byte blocks)", 0, aligned_block_copy_backwards_bs64 },
    { "C copy", 0, aligned_block_copy },
    { "C copy prefetched (32 bytes step)", 0, aligned_block_copy_pf32 },
    { "C copy prefetched (64 bytes step)", 0, aligned_block_copy_pf64 },
    { "C 2-pass copy", 1, aligned_block_copy },
    { "C 2-pass copy prefetched (32 bytes step)", 1, aligned_block_copy_pf32 },
    { "C 2-pass copy prefetched (64 bytes step)", 1, aligned_block_copy_pf64 },
    { "C fill", 0, aligned_block_fill },
    { "C fill (shuffle within 16 byte blocks)", 0, aligned_block_fill_shuffle16 },
    { "C fill (shuffle within 32 byte blocks)", 0, aligned_block_fill_shuffle32 },
    { "C fill (shuffle within 64 byte blocks)", 0, aligned_block_fill_shuffle64 },
    { NULL, 0, NULL }
};

static bench_info libc_benchmarks[] =
{
    { "standard memcpy", 0, memcpy_wrapper },
    { "standard memset", 0, memset_wrapper },
    { NULL, 0, NULL }
};

static void random_read_test(char *zerobuffer, int count, int nbits)
{
   uint32_t seed = 0;
   uintptr_t addrmask = (1 << nbits) - 1;
   uint32_t v;
   static volatile uint32_t dummy;

#define RANDOM_MEM_ACCESS()                 \
        seed = seed * 1103515245 + 12345;       \
        v = (seed >> 16) & 0xFF;                \
        seed = seed * 1103515245 + 12345;       \
        v |= (seed >> 8) & 0xFF00;              \
        seed = seed * 1103515245 + 12345;       \
        v |= seed & 0x7FFF0000;                 \
        seed |= zerobuffer[v & addrmask];

   while (count >= 16) {
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      count -= 16;
   }
   dummy = seed;
#undef RANDOM_MEM_ACCESS
}

static void random_dual_read_test(char *zerobuffer, int count, int nbits)
{
   uint32_t seed = 0;
   uintptr_t addrmask = (1 << nbits) - 1;
   uint32_t v1, v2;
   static volatile uint32_t dummy;

#define RANDOM_MEM_ACCESS()                 \
        seed = seed * 1103515245 + 12345;       \
        v1 = (seed >> 8) & 0xFF00;              \
        seed = seed * 1103515245 + 12345;       \
        v2 = (seed >> 8) & 0xFF00;              \
        seed = seed * 1103515245 + 12345;       \
        v1 |= seed & 0x7FFF0000;                \
        seed = seed * 1103515245 + 12345;       \
        v2 |= seed & 0x7FFF0000;                \
        seed = seed * 1103515245 + 12345;       \
        v1 |= (seed >> 16) & 0xFF;              \
        v2 |= (seed >> 24);                     \
        v2 &= addrmask;                         \
        v1 ^= v2;                               \
        seed |= zerobuffer[v2];                 \
        seed += zerobuffer[v1 & addrmask];

   while (count >= 16) {
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      RANDOM_MEM_ACCESS();
      count -= 16;
   }
   dummy = seed;
#undef RANDOM_MEM_ACCESS
}

static uint32_t rand32()
{
   static int seed = 0;
   uint32_t hi, lo;
   hi = (seed = seed * 1103515245 + 12345) >> 16;
   lo = (seed = seed * 1103515245 + 12345) >> 16;
   return (hi << 16) + lo;
}

uint64_t getNsSinceEpoch()
{
   return std::chrono::time_point_cast<std::chrono::nanoseconds>(Clock::now()).time_since_epoch().count();
}

int latency_bench(int size, int count)
{
   uint64_t t_before, t_after, t, t2, t_noaccess, t_noaccess2;
   double xs, xs0, xs1, xs2;
   double ys, ys0, ys1, ys2;
   double min_t, min_t2;
   int nbits, n;
   char *buffer, *buffer_alloc;



   buffer_alloc = (char *)malloc(size + 4095);
   if (!buffer_alloc)
      return 0;
   buffer = (char *)(((uintptr_t)buffer_alloc + 4095) & ~(uintptr_t)4095);

   memset(buffer, 0, size);

   for (n = 1; n <= MAXREPEATS; n++)
   {
      t_before = getNsSinceEpoch();
      random_read_test(buffer, count, 1);
      t_after = getNsSinceEpoch();
      if (n == 1 || t_after - t_before < t_noaccess)
         t_noaccess = t_after - t_before;

      t_before = getNsSinceEpoch();
      random_dual_read_test(buffer, count, 1);
      t_after = getNsSinceEpoch();
      if (n == 1 || t_after - t_before < t_noaccess2)
         t_noaccess2 = t_after - t_before;
   }

   printf("\nblock size : single random read / dual random read\n");

   for (nbits = 10; (1 << nbits) <= size; nbits++)
   {
      int testsize = 1 << nbits;
      xs1 = xs2 = ys = ys1 = ys2 = 0;
      for (n = 1; n <= MAXREPEATS; n++)
      {
         /*
          * Select a random offset in order to mitigate the unpredictability
          * of cache associativity effects when dealing with different
          * physical memory fragmentation (for PIPT caches). We are reporting
          * the "best" measured latency, some offsets may be better than
          * the others.
          */
         int testoffs = (rand32() % (size / testsize)) * testsize;

         t_before = getNsSinceEpoch();
         random_read_test(buffer + testoffs, count, nbits);
         t_after = getNsSinceEpoch();
         t = t_after - t_before - t_noaccess;
         if (t < 0) t = 0;

         xs1 += t;
         xs2 += t * t;

         if (n == 1 || t < min_t)
            min_t = t;

         t_before = getNsSinceEpoch();
         random_dual_read_test(buffer + testoffs, count, nbits);
         t_after = getNsSinceEpoch();
         t2 = t_after - t_before - t_noaccess2;
         if (t2 < 0) t2 = 0;

         ys1 += t2;
         ys2 += t2 * t2;

         if (n == 1 || t2 < min_t2)
            min_t2 = t2;

         if (n > 2)
         {
            xs = sqrt((xs2 * n - xs1 * xs1) / (n * (n - 1)));
            ys = sqrt((ys2 * n - ys1 * ys1) / (n * (n - 1)));
            if (xs < min_t / 1000. && ys < min_t2 / 1000.)
               break;
         }
      }
      printf("%10d : %6.1f ns          /  %6.1f ns \n", (1 << nbits), min_t / count, min_t2 / count);
   }
   free(buffer_alloc);
   return 1;
}


int main(void)
{

   int latbench_size = SIZE * 2, latbench_count = LATBENCH_COUNT;
   int64_t *srcbuf, *dstbuf, *tmpbuf;
   void *poolbuf;
   size_t bufsize = SIZE;

   printf("\n");
   printf("== Memory latency test ==\n");

   latency_bench(latbench_size, latbench_count);

   return 0;
}
