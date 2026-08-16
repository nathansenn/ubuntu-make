/* Isolated hot-path benches for Ubuntu 26.04 stock binaries vs 256 KiB I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <zlib.h>

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void fill(unsigned char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (unsigned char)(i * 131u + 17u);
}

static void bench_read(const char *path, size_t bufsz, int loops)
{
    unsigned char *buf = malloc(bufsz);
    double t0 = now();
    size_t total = 0;
    for (int i = 0; i < loops; i++) {
        int fd = open(path, O_RDONLY);
        for (;;) {
            ssize_t n = read(fd, buf, bufsz);
            if (n <= 0)
                break;
            total += (size_t)n;
        }
        close(fd);
    }
    double dt = now() - t0;
    printf("read %5zu KiB  %.3f s  %.2f GB/s\n",
           bufsz / 1024, dt, (total / dt) / 1e9);
    free(buf);
}

int main(void)
{
    const size_t n = 32u * 1024u * 1024u;
    unsigned char *buf = malloc(n);
    fill(buf, n);

    {
        uLong crc = crc32(0L, Z_NULL, 0);
        double t0 = now();
        crc = crc32(crc, buf, n);
        double dt = now() - t0;
        printf("zlib crc32     %.3f s  %.2f GB/s  val=%08lx\n",
               dt, (n / dt) / 1e9, crc);
    }
    {
        uLong ad = adler32(0L, Z_NULL, 0);
        double t0 = now();
        ad = adler32(ad, buf, n);
        double dt = now() - t0;
        printf("zlib adler32   %.3f s  %.2f GB/s  val=%08lx\n",
               dt, (n / dt) / 1e9, ad);
    }

    const char *path = "/tmp/bench32m.bin";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (write(fd, buf, n) != (ssize_t)n)
        return 1;
    close(fd);
    free(buf);

    bench_read(path, 8 * 1024, 8);
    bench_read(path, 16 * 1024, 8);
    bench_read(path, 64 * 1024, 8);
    bench_read(path, 128 * 1024, 8);
    bench_read(path, 256 * 1024, 8);
    bench_read(path, 512 * 1024, 8);
    return 0;
}
