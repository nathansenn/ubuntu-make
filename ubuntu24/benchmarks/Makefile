CC = cc
CFLAGS = -O3 -mpclmul -msse4.1

all: bench_hotpaths bench_100

bench_hotpaths: bench_hotpaths.c
	$(CC) $(CFLAGS) -o $@ bench_hotpaths.c

bench_100: bench_100.c
	$(CC) -O3 -o $@ bench_100.c

clean:
	rm -f bench_hotpaths bench_100

.PHONY: all clean
