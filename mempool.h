#ifndef mempool_h_
#define mempool_h_

#include <stdlib.h>

extern unsigned long long total_alloc;

template <class T, size_t BLOCK_SIZE = 1024 * 1024> class MemPool {
	struct MemPoolHeader {
		MemPoolHeader *next;
		size_t offset;
	};
	struct HeaderSizeStruct {
		MemPoolHeader h;
		T t;
	};
	struct AlignStruct {
		char c;
		T t;
	};

public:
	MemPool() : count(0), head(NULL) {
	}

	~MemPool() {
		clear();
	}

	T *alloc(size_t size = sizeof (T)) {
		const size_t header_size = sizeof (HeaderSizeStruct) - sizeof (T);
		const size_t align = sizeof (AlignStruct) - sizeof (T);

		if (head && size + header_size <= BLOCK_SIZE) {
			char *p = (char *)head + header_size + head->offset;
			size_t offset_from_head =
				(((size_t)p + align - 1) & ~(align - 1)) - (size_t)head;
			if (offset_from_head + size <= BLOCK_SIZE) {
				char *ret = (char *)head + offset_from_head;
				head->offset += size;
				++count;
				return reinterpret_cast<T *>(ret);
			}
		}
		MemPoolHeader *new_block;
		size_t new_block_size =
			size + header_size > BLOCK_SIZE ?
			size + header_size : BLOCK_SIZE;
		new_block = reinterpret_cast<MemPoolHeader *>(::malloc(new_block_size));
		total_alloc += new_block_size;
		if (new_block) {
			new_block->next = head;
			new_block->offset = size;
			head = new_block;
			++count;
			return reinterpret_cast<T *>((char *)new_block + header_size);
		} else {
			return NULL;
		}
	}

	void free(T *) {
		--count;
		if (!count) clear();
	}

	void clear() {
		count = 0;
		while (head) {
			MemPoolHeader *t = head;
			head = t->next;
			::free(t);
		}
	}

private:
	size_t count;
	MemPoolHeader *head;

	MemPool(const MemPool &);
	MemPool &operator= (const MemPool &);
};

#endif
