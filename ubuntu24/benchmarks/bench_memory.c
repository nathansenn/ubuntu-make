/* Isolated memory-management benches for Ubuntu 24.04 userspace.
   Compile: cc -O2 -pthread -o bench_memory bench_memory.c
   Measures THP hints, glibc arenas, and trim — not a shipped binary. */

#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

enum { THP_MIN = 2 * 1024 * 1024 };

static double
now_s (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec + tv.tv_usec * 1e-6;
}

static long
rss_kb (void)
{
  struct rusage ru;
  getrusage (RUSAGE_SELF, &ru);
  return ru.ru_maxrss;
}

static unsigned long
anon_huge_kb (void)
{
  FILE *fp = fopen ("/proc/self/smaps_rollup", "r");
  char line[256];
  unsigned long kb = 0;
  if (!fp)
    return 0;
  while (fgets (line, sizeof line, fp))
    if (sscanf (line, "AnonHugePages: %lu kB", &kb) == 1)
      break;
  fclose (fp);
  return kb;
}

static void
touch_seq (char *p, size_t n)
{
  size_t i;
  for (i = 0; i < n; i += 4096)
    p[i] = (char) (i >> 12);
  if (n)
    p[n - 1] ^= 1;
}

static uint64_t
walk_stride (char *p, size_t n, size_t stride)
{
  uint64_t sum = 0;
  size_t i;
  for (i = 0; i < n; i += stride)
    sum += (unsigned char) p[i];
  return sum;
}

static int
cmp_u64 (void const *a, void const *b)
{
  uint64_t x = *(uint64_t const *) a;
  uint64_t y = *(uint64_t const *) b;
  return (x > y) - (x < y);
}

static int
cmp_strp (void const *a, void const *b)
{
  return strcmp (*(char * const *) a, *(char * const *) b);
}

enum hint
{
  HINT_NONE = 0,
  HINT_HUGEPAGE,
  HINT_COLLAPSE,
  HINT_HUGE_THEN_COLLAPSE,
  HINT_WILLNEED
};

static char const *
hint_name (enum hint h)
{
  switch (h)
    {
    case HINT_NONE:
      return "none";
    case HINT_HUGEPAGE:
      return "MADV_HUGEPAGE";
    case HINT_COLLAPSE:
      return "MADV_COLLAPSE";
    case HINT_HUGE_THEN_COLLAPSE:
      return "HUGEPAGE+COLLAPSE";
    case HINT_WILLNEED:
      return "MADV_WILLNEED";
    default:
      {
        /* Keep the switch exhaustive for new hint variants. */
        return "unknown";
      }
    }
}

static void
apply_hint (void *p, size_t n, enum hint h)
{
  switch (h)
    {
    case HINT_NONE:
      break;
    case HINT_HUGEPAGE:
#ifdef MADV_HUGEPAGE
      (void) madvise (p, n, MADV_HUGEPAGE);
#endif
      break;
    case HINT_COLLAPSE:
#ifdef MADV_COLLAPSE
      (void) madvise (p, n, MADV_COLLAPSE);
#endif
      break;
    case HINT_HUGE_THEN_COLLAPSE:
#ifdef MADV_HUGEPAGE
      (void) madvise (p, n, MADV_HUGEPAGE);
#endif
#ifdef MADV_COLLAPSE
      (void) madvise (p, n, MADV_COLLAPSE);
#endif
      break;
    case HINT_WILLNEED:
      (void) madvise (p, n, MADV_WILLNEED);
      break;
    default:
      break;
    }
}

static void
bench_thp (size_t bytes, int align_2m, enum hint h, int repeats)
{
  double t_fill = 0, t_walk = 0, t_qsort = 0;
  unsigned long huge = 0;
  long rss = 0;
  uint64_t sink = 0;
  int r;

  for (r = 0; r < repeats; r++)
    {
      void *p = NULL;
      char *buf;
      size_t nkeys;
      uint64_t *keys;
      double t0;

      if (align_2m)
        {
          if (posix_memalign (&p, (size_t) THP_MIN, bytes) != 0)
            {
              fprintf (stderr, "posix_memalign failed\n");
              return;
            }
        }
      else
        {
          p = malloc (bytes);
          if (!p)
            {
              fprintf (stderr, "malloc failed\n");
              return;
            }
        }
      buf = p;

      /* Hint before first touch so the kernel can back with huge pages. */
      if (h != HINT_COLLAPSE && h != HINT_HUGE_THEN_COLLAPSE)
        apply_hint (buf, bytes, h);

      t0 = now_s ();
      touch_seq (buf, bytes);
      t_fill += now_s () - t0;

      if (h == HINT_COLLAPSE || h == HINT_HUGE_THEN_COLLAPSE)
        apply_hint (buf, bytes, h);

      t0 = now_s ();
      sink += walk_stride (buf, bytes, 64);
      t_walk += now_s () - t0;

      nkeys = bytes / 64;
      keys = (uint64_t *) buf;
      t0 = now_s ();
      qsort (keys, nkeys, sizeof *keys, cmp_u64);
      t_qsort += now_s () - t0;

      if (r == repeats - 1)
        {
          huge = anon_huge_kb ();
          rss = rss_kb ();
        }
      free (p);
    }

  printf ("thp align=%s hint=%-18s fill=%.4fs walk=%.4fs qsort=%.4fs "
          "rss=%ldkB AnonHuge=%lukB sink=%llu\n",
          align_2m ? "2MiB" : "malloc", hint_name (h),
          t_fill / repeats, t_walk / repeats, t_qsort / repeats,
          rss, huge, (unsigned long long) sink);
}

enum { ARENA_THREADS = 8, ARENA_ITERS = 400000, ARENA_SIZES = 5 };

static size_t const arena_sizes[ARENA_SIZES] = { 64, 256, 1024, 4096, 16384 };

struct arena_arg
{
  int id;
  unsigned long ops;
};

static void *
arena_worker (void *vp)
{
  struct arena_arg *a = vp;
  void *hold[64];
  int i, k;
  memset (hold, 0, sizeof hold);
  for (i = 0; i < ARENA_ITERS; i++)
    {
      k = i % 64;
      free (hold[k]);
      hold[k] = malloc (arena_sizes[(i + a->id) % ARENA_SIZES]);
      if (hold[k])
        ((char *) hold[k])[0] = (char) i;
      a->ops++;
    }
  for (k = 0; k < 64; k++)
    free (hold[k]);
  return NULL;
}

static void *
arena_hold_worker (void *vp)
{
  struct arena_arg *a = vp;
  enum { HOLD = 512 };
  void **hold = calloc (HOLD, sizeof *hold);
  int i;
  if (!hold)
    return NULL;
  for (i = 0; i < HOLD; i++)
    {
      size_t n = arena_sizes[i % ARENA_SIZES] * 16;
      hold[i] = malloc (n);
      if (hold[i])
        memset (hold[i], 1, n);
      a->ops++;
    }
  /* Keep the allocations live until the process measures RSS. */
  usleep (200000);
  for (i = 0; i < HOLD; i++)
    free (hold[i]);
  free (hold);
  return NULL;
}

static void
bench_arena_hold (void)
{
  pthread_t th[ARENA_THREADS];
  struct arena_arg args[ARENA_THREADS];
  char *tunable = getenv ("GLIBC_TUNABLES");
  int i;
  for (i = 0; i < ARENA_THREADS; i++)
    {
      args[i].id = i;
      args[i].ops = 0;
      pthread_create (&th[i], NULL, arena_hold_worker, &args[i]);
    }
  usleep (50000);
  printf ("arena-hold tunables=%s threads=%d live_allocs=%d rss=%ldkB\n",
          tunable ? tunable : "(default)", ARENA_THREADS,
          ARENA_THREADS * 512, rss_kb ());
  for (i = 0; i < ARENA_THREADS; i++)
    pthread_join (th[i], NULL);
}

static void
bench_arenas (void)
{
  pthread_t th[ARENA_THREADS];
  struct arena_arg args[ARENA_THREADS];
  double t0, dt;
  unsigned long ops = 0;
  int i;
  char *tunable = getenv ("GLIBC_TUNABLES");

  t0 = now_s ();
  for (i = 0; i < ARENA_THREADS; i++)
    {
      args[i].id = i;
      args[i].ops = 0;
      pthread_create (&th[i], NULL, arena_worker, &args[i]);
    }
  for (i = 0; i < ARENA_THREADS; i++)
    {
      pthread_join (th[i], NULL);
      ops += args[i].ops;
    }
  dt = now_s () - t0;
  printf ("arena tunables=%s threads=%d ops=%lu time=%.4fs rate=%.1f kops/s "
          "rss=%ldkB\n",
          tunable ? tunable : "(default)", ARENA_THREADS, ops, dt,
          ops / dt / 1000.0, rss_kb ());
}

static void
bench_read_sort (size_t bytes)
{
  char path[] = "/tmp/u24-mem-read.XXXXXX";
  int fd = mkstemp (path);
  char *src;
  size_t i, nlines, nread;
  double t0, t_read, t_sort;
  uint64_t sink = 0;

  if (fd < 0)
    {
      perror ("mkstemp");
      return;
    }
  src = malloc (bytes);
  if (!src)
    {
      perror ("malloc");
      return;
    }
  for (i = 0; i < bytes; i += 32)
    {
      unsigned v = (unsigned) (i * 1103515245u + 12345u);
      int n = snprintf (src + i, 32, "%08x %08x\n", v, v ^ 0x9e3779b9u);
      if (n > 0 && n < 32)
        memset (src + i + n, ' ', (size_t) (31 - n));
      src[i + 31] = '\n';
    }
  if (write (fd, src, bytes) != (ssize_t) bytes)
    {
      perror ("write");
      return;
    }
  free (src);
  fsync (fd);

  for (i = 0; i < 2; i++)
    {
      int align = (int) i;
      void *p = NULL;
      char *buf;
      char **lines;
      size_t l;

      if (align)
        {
          if (posix_memalign (&p, (size_t) THP_MIN, bytes) != 0)
            {
              perror ("posix_memalign");
              return;
            }
#ifdef MADV_HUGEPAGE
          (void) madvise (p, bytes, MADV_HUGEPAGE);
#endif
        }
      else
        {
          p = malloc (bytes);
          if (!p)
            {
              perror ("malloc");
              return;
            }
        }
      buf = p;
      if (lseek (fd, 0, SEEK_SET) < 0)
        {
          perror ("lseek");
          return;
        }
      t0 = now_s ();
      nread = 0;
      while (nread < bytes)
        {
          ssize_t n = read (fd, buf + nread, bytes - nread);
          if (n <= 0)
            break;
          nread += (size_t) n;
        }
      t_read = now_s () - t0;

      nlines = 0;
      for (l = 0; l < nread; l++)
        if (buf[l] == '\n')
          {
            buf[l] = '\0';
            nlines++;
          }
      lines = malloc (nlines * sizeof *lines);
      if (!lines)
        {
          perror ("malloc lines");
          free (p);
          return;
        }
      nlines = 0;
      lines[nlines++] = buf;
      for (l = 0; l < nread; l++)
        if (buf[l] == '\0' && l + 1 < nread)
          lines[nlines++] = buf + l + 1;

      t0 = now_s ();
      qsort (lines, nlines, sizeof *lines, cmp_strp);
      t_sort = now_s () - t0;
      if (nlines)
        sink += (unsigned char) lines[0][0];

      printf ("read+sort align=%s read=%.4fs sort=%.4fs lines=%zu "
              "rss=%ldkB AnonHuge=%lukB sink=%llu\n",
              align ? "2MiB+THP" : "malloc", t_read, t_sort, nlines,
              rss_kb (), anon_huge_kb (), (unsigned long long) sink);
      free (lines);
      free (p);
    }

  close (fd);
  unlink (path);
}

static void
bench_trim (void)
{
  size_t const chunk = 256 * 1024;
  int const n = 64;
  void *p[64];
  int i;
  double t0, t_alloc, t_free, t_trim;
  long rss_after_alloc, rss_after_free, rss_after_trim;

  t0 = now_s ();
  for (i = 0; i < n; i++)
    {
      p[i] = malloc (chunk);
      if (p[i])
        memset (p[i], 1, chunk);
    }
  t_alloc = now_s () - t0;
  rss_after_alloc = rss_kb ();

  t0 = now_s ();
  for (i = 0; i < n; i++)
    free (p[i]);
  t_free = now_s () - t0;
  rss_after_free = rss_kb ();

  t0 = now_s ();
  malloc_trim (0);
  t_trim = now_s () - t0;
  rss_after_trim = rss_kb ();

  printf ("trim alloc=%.4fs free=%.4fs trim=%.4fs rss_alloc=%ldkB "
          "rss_free=%ldkB rss_trim=%ldkB (16MiB in 256KiB chunks)\n",
          t_alloc, t_free, t_trim, rss_after_alloc, rss_after_free,
          rss_after_trim);
}

int
main (int argc, char **argv)
{
  char const *mode = argc > 1 ? argv[1] : "all";
  size_t bytes = 64 * 1024 * 1024;
  int repeats = 3;

  if (argc > 2)
    bytes = (size_t) atol (argv[2]) * 1024 * 1024;
  if (argc > 3)
    repeats = atoi (argv[3]);

  printf ("# bench_memory bytes=%zu repeats=%d nproc=%ld\n",
          bytes, repeats, sysconf (_SC_NPROCESSORS_ONLN));

  if (strcmp (mode, "all") == 0 || strcmp (mode, "thp") == 0)
    {
      enum hint hints[] = {
        HINT_NONE, HINT_HUGEPAGE, HINT_WILLNEED, HINT_COLLAPSE,
        HINT_HUGE_THEN_COLLAPSE
      };
      int align, i;
      for (align = 0; align <= 1; align++)
        for (i = 0; i < 5; i++)
          bench_thp (bytes, align, hints[i], repeats);
    }

  if (strcmp (mode, "all") == 0 || strcmp (mode, "arena") == 0)
    {
      bench_arenas ();
      bench_arena_hold ();
    }

  if (strcmp (mode, "all") == 0 || strcmp (mode, "trim") == 0)
    bench_trim ();

  if (strcmp (mode, "all") == 0 || strcmp (mode, "readsort") == 0)
    bench_read_sort (bytes);

  return 0;
}
