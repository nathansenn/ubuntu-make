CC = cc
CFLAGS = -O3 -mpclmul -msse4.1

all: bench_hotpaths bench_100 bench_memory

bench_hotpaths: bench_hotpaths.c
	$(CC) $(CFLAGS) -o $@ bench_hotpaths.c

bench_100: bench_100.c
	$(CC) -O3 -o $@ bench_100.c

bench_memory: bench_memory.c
	$(CC) -O2 -pthread -o $@ bench_memory.c

clean:
	rm -f bench_hotpaths bench_100 bench_memory

.PHONY: all clean
