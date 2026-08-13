/* LD_PRELOAD helper: madvise(MADV_HUGEPAGE) on mallocs >= 2 MiB.
   Used only to measure THP on stock binaries. Not a shipped library. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/mman.h>

enum { THP_MIN = 2 * 1024 * 1024 };

static void *(*real_malloc) (size_t);
static void *(*real_calloc) (size_t, size_t);
static void *(*real_realloc) (void *, size_t);
static int (*real_posix_memalign) (void **, size_t, size_t);
static int resolving;

static void
hint (void *p, size_t n)
{
#ifdef MADV_HUGEPAGE
  if (p && n >= (size_t) THP_MIN)
    (void) madvise (p, n, MADV_HUGEPAGE);
#else
  (void) p;
  (void) n;
#endif
}

__attribute__ ((constructor))
static void
init_reals (void)
{
  if (real_malloc || resolving)
    return;
  resolving = 1;
  real_malloc = dlsym (RTLD_NEXT, "malloc");
  real_calloc = dlsym (RTLD_NEXT, "calloc");
  real_realloc = dlsym (RTLD_NEXT, "realloc");
  real_posix_memalign = dlsym (RTLD_NEXT, "posix_memalign");
  resolving = 0;
}

void *
malloc (size_t n)
{
  void *p;
  init_reals ();
  p = real_malloc (n);
  hint (p, n);
  return p;
}

void *
calloc (size_t a, size_t b)
{
  void *p;
  size_t n;
  init_reals ();
  p = real_calloc (a, b);
  n = a * b;
  hint (p, n);
  return p;
}

void *
realloc (void *old, size_t n)
{
  void *p;
  init_reals ();
  p = real_realloc (old, n);
  hint (p, n);
  return p;
}

int
posix_memalign (void **memptr, size_t align, size_t n)
{
  int rc;
  init_reals ();
  rc = real_posix_memalign (memptr, align, n);
  if (rc == 0)
    hint (*memptr, n);
  return rc;
}
