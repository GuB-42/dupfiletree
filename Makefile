CC = gcc
CXX = g++
RM = rm -f
CFLAGS = -O3 -ggdb3 -Wall -Wextra
# no -ansi -pedantic (long long, lstat, readdir_r)
CXXFLAGS = -O3 -ggdb3 -ansi -pedantic -Wall -Wextra -Wno-long-long
LDFLAGS =
# LIBARCHIVE_LDFLAGS = compiled_libarchive3/lib/libarchive.a -lz -lbz2 -lzstd -llzma -lxml2 -lb2 -llz4
# LIBARCHIVE_CFLAGS = -Icompiled_libarchive3/include
LIBARCHIVE_LDFLAGS = -larchive
LIBARCHIVE_CFLAGS =

all: xmd5 find_dup

re: fclean all

clean:
	$(RM) xmd5.o find_dup.o node.o

fclean: clean
	$(RM) xmd5 find_dup

xmd5: xmd5.o
	$(CC) -o $@ $(LDFLAGS) xmd5.o -lcrypto $(LIBARCHIVE_LDFLAGS)

find_dup: find_dup.o node.o
	$(CXX) -o $@ $(LDFLAGS) find_dup.o node.o

xmd5.o: xmd5.c
	$(CC) $(CFLAGS) -I$(LIBARCHIVE_PREFIX)/include -DDO_FORK -o $@ -c $<
find_dup.o: find_dup.cc skiplist.h mempool.h node.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<
node.o: node.cc mempool.h node.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: all re clean fclean
