/* Microbenches for the 100-way Ubuntu 24.04 survey.
 * cc -O3 -o bench_100 bench_100.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double
now(void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void
die(const char *m)
{
  perror (m);
  exit (1);
}

static unsigned char *src;
static size_t nbytes = 32u * 1024u * 1024u;

static double
best_of(int n, double (*fn)(void))
{
  double b = 1e99;
  for (int i = 0; i < n; i++)
    {
      double t = fn ();
      if (t < b)
        b = t;
    }
  return b;
}

static size_t g_iosz;

static double
do_read(void)
{
  int fd = open ("/tmp/u24_32m.bin", O_RDONLY);
  if (fd < 0)
    die ("open");
  char *buf = malloc (g_iosz);
  double t0 = now ();
  ssize_t r;
  while ((r = read (fd, buf, g_iosz)) > 0)
    ;
  double dt = now () - t0;
  close (fd);
  free (buf);
  return dt;
}

static double
do_write(void)
{
  int fd = open ("/tmp/u24_w.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    die ("open w");
  char *buf = malloc (g_iosz);
  memset (buf, 'x', g_iosz);
  size_t left = nbytes;
  double t0 = now ();
  while (left)
    {
      size_t n = left < g_iosz ? left : g_iosz;
      if (write (fd, buf, n) != (ssize_t) n)
        die ("write");
      left -= n;
    }
  double dt = now () - t0;
  close (fd);
  free (buf);
  return dt;
}

static double
do_fread(void)
{
  FILE *fp = fopen ("/tmp/u24_32m.bin", "rb");
  if (!fp)
    die ("fopen");
  char *buf = malloc (g_iosz);
  setvbuf (fp, NULL, _IOFBF, g_iosz);
  double t0 = now ();
  while (fread (buf, 1, g_iosz, fp) > 0)
    ;
  double dt = now () - t0;
  fclose (fp);
  free (buf);
  return dt;
}

static double
do_getc(void)
{
  FILE *fp = fopen ("/tmp/u24_32m.bin", "rb");
  if (!fp)
    die ("fopen getc");
  volatile unsigned long sum = 0;
  double t0 = now ();
  int c;
  while ((c = getc (fp)) != EOF)
    sum += (unsigned) c;
  double dt = now () - t0;
  fclose (fp);
  (void) sum;
  return dt;
}

static double
do_mmap_touch(void)
{
  int fd = open ("/tmp/u24_32m.bin", O_RDONLY);
  if (fd < 0)
    die ("mmap open");
  double t0 = now ();
  unsigned char *p = mmap (NULL, nbytes, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    die ("mmap");
  volatile unsigned long sum = 0;
  for (size_t i = 0; i < nbytes; i += 64)
    sum += p[i];
  munmap (p, nbytes);
  double dt = now () - t0;
  close (fd);
  (void) sum;
  return dt;
}

static double
do_sendfile(void)
{
  int in = open ("/tmp/u24_32m.bin", O_RDONLY);
  int out = open ("/tmp/u24_sf.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (in < 0 || out < 0)
    die ("sendfile open");
  off_t off = 0;
  double t0 = now ();
  while (off < (off_t) nbytes)
    {
      ssize_t n = sendfile (out, in, &off, nbytes - (size_t) off);
      if (n <= 0)
        break;
    }
  double dt = now () - t0;
  close (in);
  close (out);
  return dt;
}

static double
do_memcpy(void)
{
  unsigned char *dst = malloc (nbytes);
  double t0 = now ();
  memcpy (dst, src, nbytes);
  double dt = now () - t0;
  free (dst);
  return dt;
}

static double
do_bytecopy(void)
{
  unsigned char *dst = malloc (nbytes);
  double t0 = now ();
  for (size_t i = 0; i < nbytes; i++)
    dst[i] = src[i];
  double dt = now () - t0;
  free (dst);
  return dt;
}

static double
do_memchr(void)
{
  src[nbytes - 1] = 0xff;
  double t0 = now ();
  volatile void *p = memchr (src, 0xff, nbytes);
  double dt = now () - t0;
  (void) p;
  src[nbytes - 1] = 0;
  return dt;
}

static double
do_bytescan(void)
{
  src[nbytes - 1] = 0xff;
  double t0 = now ();
  size_t i;
  for (i = 0; i < nbytes; i++)
    if (src[i] == 0xff)
      break;
  double dt = now () - t0;
  (void) i;
  src[nbytes - 1] = 0;
  return dt;
}

static double
do_fadvise(void)
{
  int fd = open ("/tmp/u24_32m.bin", O_RDONLY);
  if (fd < 0)
    die ("fadvise open");
  posix_fadvise (fd, 0, 0, POSIX_FADV_SEQUENTIAL);
  char *buf = malloc (256 * 1024);
  double t0 = now ();
  ssize_t r;
  while ((r = read (fd, buf, 256 * 1024)) > 0)
    ;
  double dt = now () - t0;
  close (fd);
  free (buf);
  return dt;
}

static void
report(const char *name, double sec)
{
  printf ("%-28s %8.4f s  %8.1f MB/s\n", name, sec, (nbytes / 1e6) / sec);
}

int
main(void)
{
  int fd = open ("/tmp/u24_32m.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    die ("create");
  src = malloc (nbytes);
  if (!src)
    die ("malloc");
  for (size_t i = 0; i < nbytes; i++)
    src[i] = (unsigned char) (i * 131u + 17u);
  if (write (fd, src, nbytes) != (ssize_t) nbytes)
    die ("seed");
  close (fd);

  static const size_t sizes[] = {
    4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576
  };
  printf ("# read() sizes\n");
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
      g_iosz = sizes[i];
      char name[64];
      snprintf (name, sizeof name, "read %zuKiB", sizes[i] / 1024);
      report (name, best_of (4, do_read));
    }
  printf ("# write() sizes\n");
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
      g_iosz = sizes[i];
      char name[64];
      snprintf (name, sizeof name, "write %zuKiB", sizes[i] / 1024);
      report (name, best_of (3, do_write));
    }
  printf ("# fread+setvbuf\n");
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
      g_iosz = sizes[i];
      char name[64];
      snprintf (name, sizeof name, "fread %zuKiB", sizes[i] / 1024);
      report (name, best_of (3, do_fread));
    }
  report ("getc", best_of (2, do_getc));
  report ("mmap+touch64", best_of (3, do_mmap_touch));
  report ("sendfile", best_of (3, do_sendfile));
  report ("memcpy", best_of (5, do_memcpy));
  report ("bytecopy", best_of (2, do_bytecopy));
  report ("memchr", best_of (5, do_memchr));
  report ("bytescan", best_of (2, do_bytescan));
  report ("read+fadvise", best_of (4, do_fadvise));
  g_iosz = 262144;
  report ("read 256KiB (ref)", best_of (4, do_read));
  return 0;
}
