CC = gcc
CXX = g++
RM = rm -f
CFLAGS = -O3 -ggdb3 -Wall -Wextra
# no -ansi -pedantic (flexible arrays, long long, lstat, readdir_r)
CXXFLAGS = -O3 -ggdb3 -ansi -pedantic -Wall -Wextra -Wno-long-long
LDFLAGS =
LIBARCHIVE_PREFIX = ../compiled_libarchive3

all: xmd5 find_dup

re: fclean all

clean:
	$(RM) xmd5.o find_dup.o

fclean: clean
	$(RM) xmd5 find_dup

xmd5: xmd5.o
	$(CC) -o $@ $(LDFLAGS) -lz -lbz2 -llzma -lcrypto -lxml2 xmd5.o $(LIBARCHIVE_PREFIX)/lib/libarchive.a

find_dup: find_dup.o node.o
	$(CXX) -o $@ $(LDFLAGS) find_dup.o node.o

xmd5.o: xmd5.c
	$(CC) $(CFLAGS) -I$(LIBARCHIVE_PREFIX)/include -DDO_FORK -o $@ -c $<
find_dup.o: find_dup.cc skiplist.h mempool.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<
node.o: node.cc mempool.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: all re clean fclean
