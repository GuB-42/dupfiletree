#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "skiplist.h"
#include "mempool.h"
#include "node.h"

unsigned long long total_alloc = 0;
struct timeval last_tv = { 0, 0 };

static std::string get_current_time()
{
	time_t rawtime;
	struct tm *timeinfo;
	char tbuf[256];
	struct timeval new_tv;
	struct timeval delta_tv;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(tbuf, sizeof (tbuf), "%F %T", timeinfo);

	gettimeofday(&new_tv, NULL);
	if (new_tv.tv_usec >= last_tv.tv_usec) {
		delta_tv.tv_usec = new_tv.tv_usec - last_tv.tv_usec;
		delta_tv.tv_sec = new_tv.tv_sec - last_tv.tv_sec;
	} else {
		delta_tv.tv_usec = 1000000 + new_tv.tv_usec - last_tv.tv_usec;
		delta_tv.tv_sec = new_tv.tv_sec - last_tv.tv_sec - 1;
	}
	last_tv = new_tv;

	sprintf(tbuf + strlen(tbuf), " (+%ld.%06ld)", delta_tv.tv_sec, delta_tv.tv_usec);
	return std::string(tbuf);
}

bool        option_equal = false;
bool        option_print_tree = false;
bool        option_child_groups = false;
bool        option_zero = false;
std::string option_format("5s");
std::string option_only_in;

struct HashElt;
struct HashPoolAlloc {
	static void *operator new(size_t size) {
		return pool.alloc(size);
	}
	static void operator delete(void *p) {
		pool.free(static_cast<HashElt *>(p));
	}
	static MemPool<HashElt> pool;
};
MemPool<HashElt> HashPoolAlloc::pool;

struct HashElt : public HashPoolAlloc {
	static const size_t HASH_SIZE = 16;

	Node *node;
	unsigned char hash[HASH_SIZE];

	HashElt(Node *n, const char *md5_str) : node(n) {
		set_hash(md5_str);
		node->group = node;
	}

	void set_hash(const char *md5_str) {
		unsigned hash_idx = 0;
		bool high = true;
		memset(hash, 0, HASH_SIZE);
		for (unsigned i = 0; md5_str[i] != '\0'; ++i) {
			unsigned char v;
			if (md5_str[i] >= '0' && md5_str[i] <= '9') {
				v = md5_str[i] - '0';
			} else if (md5_str[i] >= 'a' && md5_str[i] <= 'f') {
				v = md5_str[i] - 'a' + 10;
			} else if (md5_str[i] >= 'A' && md5_str[i] <= 'F') {
				v = md5_str[i] - 'A' + 10;
			} else {
				continue;
			}
			if (high) {
				hash[hash_idx] = v << 4;
			} else {
				hash[hash_idx++] |= v;
			}
			high = !high;
			if (hash_idx >= HASH_SIZE) break;
		}
	}

	int cmp(const HashElt &o) const {
		return memcmp(hash, o.hash, HASH_SIZE);
	}

	void merge(const HashElt &o) {
		Node *t = node->group;
		node->group = o.node->group;
		o.node->group = t;
	}
};

SkipList<HashElt> hash_skip_list;

static std::string to_human_str(unsigned long long v)
{
	const char mul[] = "-kMGTPEZY";
	const char *mp = mul;
	std::stringstream ss;

	if (v < 1000) {
		ss << v;
		return ss.str();
	}
	v *= 10;
	while (1) {
		++mp;
		if (*mp == '\0') break;
		v /= 1024;
		if (v < 100) {
			ss << v / 10 << "." << v % 10 << *mp;
			return ss.str();
		} else if (v < 10000) {
			ss << v / 10 << *mp;
			return ss.str();
		}
	}

	return "?";
}

void read_line(Node *root_node, char *line,
               const char *filename, size_t line_nb)
{
	char *p = line;

	while (*p == ' ' || *p == '\t') ++p;
	if (*p == '\0') return;

	try {
		char *b_md5 = NULL;
		char *b_size = NULL;
		char *b_path = NULL;

		for (size_t j = 0; j < option_format.length(); ++j) {
			if (*p == '\0') throw "parse error";
			char *p2 = p;
			while (*p2 != ' ' && *p2 != '\t' && *p2 != '\0') ++p2;
			if (*p2 == '\0') throw "parse error";
			*p2 = '\0';
			switch (option_format[j]) {
			case '5':
				b_md5 = p;
				break;
			case 's':
				b_size = p;
				break;
			}
			p = p2 + 1;
			while (*p == ' ' || *p == '\t') ++p;
		}
		b_path = p;

		unsigned long long size = 0;
		if (b_size) {
			size = strtoull(b_size, &p, 10);
			if (*p != '\0') size = 0;
		}

		if (option_zero || size != 0 ||
		    (b_md5 && strcmp(b_md5, "d41d8cd98f00b204e9800998ecf8427e") != 0)) {
			Node *node = root_node->insert_node(b_path, size);
			if (b_md5 && !node->group) {
				p = b_md5;
				while ((*p >= '0' && *p <= '9') ||
				       (*p >= 'a' && *p <= 'f') ||
				       (*p >= 'A' && *p <= 'F')) ++p;
				if (*p == '\0' && p - b_md5 == 32) {
					HashElt hash_elt(node, b_md5);
					hash_skip_list.insert(hash_elt);
				}
			}
		}
	} catch (const char *s) {
		std::cout << filename << ":" << line_nb << ": " << s << std::endl;
	}
}

void read_file(Node *root_node, FILE *stream, const char *filename)
{
	size_t line_nb = 1;
	size_t read_buf_size = 65536;
	char *read_buf = (char *)malloc(read_buf_size);
	char *read_p = read_buf;
	char *next_p = read_p;

	do {
		size_t sz = fread(next_p, 1, read_buf_size - (next_p - read_buf) - 1, stream);
		while (sz > 0) {
			while (sz > 0 && *next_p != '\n' && *next_p != '\r') {
				--sz;
				++next_p;
			}
			if (sz > 0) {
				size_t next_line_nb = line_nb;
				if (*next_p == '\n') ++next_line_nb;
				*next_p = '\0';
				read_line(root_node, read_p, filename, line_nb);
				read_p = next_p + 1;
				line_nb = next_line_nb;
			}
		}
		if (next_p >= read_buf + read_buf_size - 1) {
			if (read_p != read_buf) {
				memmove(read_buf, read_p, read_buf_size - (read_p - read_buf));
				next_p -= read_p - read_buf;
				read_p = read_buf;
			} else {
				read_buf_size *= 2;
				char *new_read_buf = (char *)realloc(read_buf, read_buf_size);
				next_p = new_read_buf + (next_p - read_buf);
				read_p = read_buf = new_read_buf;
			}
		}
	} while (!feof(stream) && !ferror(stream));

	*next_p = '\0';
	read_line(root_node, read_p, filename, line_nb);

	free(read_buf);
}

int group_list_cmp(const void *a, const void *b)
{
	const Node::GroupListElt *elt_a = (Node::GroupListElt *)a;
	const Node::GroupListElt *elt_b = (Node::GroupListElt *)b;

	if (elt_a->group_size < elt_b->group_size) return 1;
	if (elt_b->group_size < elt_a->group_size) return -1;
	return 0;
}

int delete_list_cmp(const void *a, const void *b)
{
	const Node * const *elt_a = static_cast<const Node * const *>(a);
	const Node * const *elt_b = static_cast<const Node * const *>(b);

	if ((*elt_a)->size < (*elt_b)->size) return 1;
	if ((*elt_b)->size < (*elt_a)->size) return -1;
	return 0;
}

int main(int argc, char* argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hef:tczo:")) != -1) {
		switch (opt) {
		case 'e':
			option_equal = true;
			break;
		case 'f':
			option_format = optarg;
			break;
		case 'c':
			option_child_groups = true;
			break;
		case 'z':
			option_zero = true;
			break;
		case 't':
			option_print_tree = true;
			break;
		case 'o':
			option_only_in = optarg;
			break;
		case 'h':
		default:
			std::cerr << "Usage: " << argv[0] << " [-ectz] [-f (5s)] file" << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	std::cout << "node size " << sizeof(Node) << std::endl;
	std::cout << "hash elt size " << sizeof(HashElt) << std::endl;

	Node *root_node = new Node(".", 1, false);

	std::cout << "building tree / " << get_current_time() << std::endl;
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			FILE *f = fopen(argv[i], "r");
			if (f) {
				read_file(root_node, f, argv[i]);
				fclose(f);
			}
		}
	} else {
		read_file(root_node, stdin, "stdin");
	}
	hash_skip_list.clear();

	std::cout << "breaking cycles / " << get_current_time() << std::endl;
	root_node->break_sibling_cycles();
	std::cout << "resizing vnodes / " << get_current_time() << std::endl;
	root_node->resize_vnodes();
	std::cout << "ungrouping directories / " << get_current_time() << std::endl;
	root_node->ungroup_dirs();
	std::cout << "finding dupes / " << get_current_time() << std::endl;
	root_node->find_dupes();
	root_node->set_visited(false);
	std::cout << "counting child / " << get_current_time() << std::endl;
	root_node->compute_child_counts();
	unsigned itn = 0;
	std::cout << "grouping directories (equal) / " << get_current_time() << std::endl;
	while (1) {
		std::cout << ++itn << " / " << get_current_time() << std::endl;
		if (!root_node->group_dirs(true)) break;
	}
	if (!option_equal) {
		std::cout << "grouping directories (master/slave) / " << get_current_time() << std::endl;
		root_node->kill_singles();
		while (1) {
			std::cout << ++itn << " / " << get_current_time() << std::endl;
			if (!root_node->group_dirs(false)) break;
		}
	}

	if (option_print_tree) root_node->print_tree();

	Node::GroupListElt *group_list = NULL;
	std::cout << "counting groups / " << get_current_time() << std::endl;
	root_node->set_visited(false);
	size_t group_list_count = root_node->build_count_group_list(NULL, option_child_groups);
	std::cout << "alloc " << group_list_count << " groups / " << get_current_time() << std::endl;
	group_list = new Node::GroupListElt[group_list_count];
	std::cout << "building groups / " << get_current_time() << std::endl;
	root_node->set_visited(false);
	root_node->build_count_group_list(group_list, option_child_groups);
	std::cout << "sorting groups / " << get_current_time() << std::endl;
	qsort(group_list, group_list_count, sizeof (*group_list), group_list_cmp);
	std::cout << "done / " << get_current_time() << std::endl;
	root_node->set_visited(false);
	for (size_t i = 0; i < group_list_count; ++i) {
		const Node::GroupListElt &ge = group_list[i];
		bool visited = false;
		if (!option_child_groups) {
			visited = true;
			bool first = true;
			for (Node *p = ge.group; first || p != ge.group; p = p->group, first = false) {
				if (!p->visited) visited = false;
			}
		}
		if (!visited) {
			std::cout << "group size : " << (visited ? "(VISITED) " : "") << ge.group_size <<
				" (" << to_human_str(ge.group_size) <<  ")" << std::endl;
			ge.group->print_group();
			bool first = true;
			for (Node *p = ge.group; first || p != ge.group; p = p->group, first = false) {
				if (!p->slave) p->set_visited(true);
			}
		}
	}
	delete[] group_list;

	std::cout << "finding keepers / " << get_current_time() << std::endl;
	root_node->set_visited(false);
	root_node->find_keepers();
	std::cout << "counting deletes / " << get_current_time() << std::endl;
	size_t delete_list_count = root_node->count_list_delete(NULL);
	std::cout << "alloc " << delete_list_count << " deletes / " << get_current_time() << std::endl;
	Node **delete_list = new Node *[delete_list_count];
	std::cout << "building deletes / " << get_current_time() << std::endl;
	root_node->count_list_delete(delete_list);
	std::cout << "sorting deletes / " << get_current_time() << std::endl;
	qsort(delete_list, delete_list_count, sizeof (*delete_list), delete_list_cmp);
	std::cout << "done / " << get_current_time() << std::endl;
	unsigned long long prev_size = 0;
	for (size_t i = 0; i < delete_list_count; ++i) {
		if (i == 0 || delete_list[i]->size != prev_size) {
			if (i != 0) std::cout << std::endl;
			std::cout << "delete size : " << delete_list[i]->size <<
				" (" << to_human_str(delete_list[i]->size) << ")" << std::endl;
			prev_size = delete_list[i]->size;
		}
		std::cout << delete_list[i]->get_path() << std::endl;
	}
	if (delete_list_count != 0) std::cout << std::endl;
	delete[] delete_list;

	if (!option_only_in.empty()) {
		Node *origin = root_node->find_node(option_only_in.c_str());
		if (origin) {
			origin->print_only_in_list(origin);
		}
	}

	root_node->clear_children();
	delete root_node;

	std::cout << "total_alloc : " << total_alloc <<
		" (" << to_human_str(total_alloc) <<
		") / " << (group_list_count * sizeof (*group_list)) <<
		" (" << to_human_str(group_list_count * sizeof (*group_list)) <<
		") / " << (delete_list_count * sizeof (*delete_list)) <<
		" (" << to_human_str(group_list_count * sizeof (*group_list)) << ") / " <<
		get_current_time() << std::endl;
}
