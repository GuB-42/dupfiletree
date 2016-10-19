#ifndef mempool_h_
#define mempool_h_

#include <stdlib.h>

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
	MemPool() : head(NULL) {
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
				return reinterpret_cast<T *>(ret);
			}
		}
		MemPoolHeader *new_block;
		size_t new_block_size =
			size + header_size > BLOCK_SIZE ?
			size + header_size : BLOCK_SIZE;
		new_block = reinterpret_cast<MemPoolHeader *>(malloc(new_block_size));
		new_block->next = head;
		new_block->offset = size;
		head = new_block;
		return reinterpret_cast<T *>((char *)new_block + header_size);
	}

	void shallow_clear() {
		while (head && head->offset > BLOCK_SIZE) {
			MemPoolHeader *t = head->next;
			head->next = t->next;
			free(t);
		}
		if (head) {
			while (head->next) {
				MemPoolHeader *t = head->next;
				head->next = t->next;
				free(t);
			}
			head->offset = 0;
		}
	}

	void clear() {
		while (head) {
			MemPoolHeader *t = head;
			head = t->next;
			free(t);
		}
	}

private:
	MemPoolHeader *head;

	MemPool(const MemPool &);
	MemPool &operator= (const MemPool &);
};

#endif