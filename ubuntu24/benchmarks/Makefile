CC = cc
CFLAGS = -O3 -mpclmul -msse4.1

bench_hotpaths: bench_hotpaths.c
	$(CC) $(CFLAGS) -o $@ bench_hotpaths.c

clean:
	rm -f bench_hotpaths

.PHONY: clean
