/* Isolated write-size plateau for uutils/GNU yes (16 KiB vs 256 KiB). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void bench(size_t bufsz, size_t total)
{
    unsigned char *buf = malloc(bufsz);
    memset(buf, 'y', bufsz);
    buf[bufsz - 1] = '\n';
    int fd = open("/tmp/yesbuf.out", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    size_t left = total;
    double t0 = now();
    while (left) {
        size_t n = left < bufsz ? left : bufsz;
        if (write(fd, buf, n) != (ssize_t)n)
            exit(1);
        left -= n;
    }
    close(fd);
    double dt = now() - t0;
    printf("yes-write %5zu KiB  %.3f s  %.2f GB/s\n",
           bufsz / 1024, dt, (total / dt) / 1e9);
    free(buf);
}

int main(void)
{
    const size_t total = 256u * 1024u * 1024u;
    bench(16 * 1024, total);
    bench(32 * 1024, total);
    bench(256 * 1024, total);
    unlink("/tmp/yesbuf.out");
    return 0;
}
