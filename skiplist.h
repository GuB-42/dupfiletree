#ifndef skiplist_h_
#define skiplist_h_

#include <stdlib.h>

template <class T, unsigned NB_LEVELS = 32, unsigned PROB = 2> class SkipList {
	struct SkipListElt : public T {
		// cppcheck-suppress uninitMemberVar
		explicit SkipListElt(const T &o) : T(o) {
		}

		template <typename C, C> struct TypeCheck;
		template <typename C>
		static void *do_new(size_t size, TypeCheck<void *(*)(size_t), &C::operator new> *) {
			return C::operator new(size);
		}
		template <typename C>
		static void *do_new(size_t size, ...) {
			return ::operator new(size);
		}
		void *operator new(size_t size, unsigned level) {
			return do_new<T>(size + (level - 1) * sizeof (SkipListElt *), 0);
		}
		SkipListElt *next[1];
	};

public:
	// cppcheck-suppress uninitMemberVar
	SkipList() : max_level(0) {
	}

	T* insert(const T &elt, bool *is_new = NULL) {
		SkipListElt **pp[NB_LEVELS];

		unsigned level = max_level;

		SkipListElt *last_elt = NULL;
		while (level) {
			--level;
			if (last_elt) {
				pp[level] = &last_elt->next[level];
			} else {
				pp[level] = &root[level];
			}
			while (*pp[level]) {
				int cmp = (*pp[level])->cmp(elt);
				if (cmp == 0) {
					(*pp[level])->merge(elt);
					if (is_new) *is_new = false;
					return *pp[level];
				} else if (cmp > 0) {
					break;
				}
				last_elt = *pp[level];
				pp[level] = &last_elt->next[level];
			}
		};

		unsigned new_level = 1;
		while (new_level <= max_level && new_level < NB_LEVELS) {
			if (rand() % PROB != 0) break;
			++new_level;
		}

		SkipListElt *new_elt = new(new_level) SkipListElt(elt);
		for (level = 0; level < new_level; ++level) {
			if (new_level < max_level) {
				new_elt->next[level] = *pp[level];
				*pp[level] = new_elt;
			} else {
				new_elt->next[level] = NULL;
				root[level] = new_elt;
				max_level = level + 1;
			}
		}
		if (is_new) *is_new = false;
		return new_elt;
	}

	void clear() {
		if (max_level > 0) {
			while (root[0]) {
				SkipListElt *t = root[0];
				root[0] = t->next[0];
				delete t;
			}
			max_level = 0;
		}
	}

private:
	unsigned max_level;
	SkipListElt *root[NB_LEVELS];
};

#endif