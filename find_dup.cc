#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
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

	HashElt(Node *n, std::string md5_str) : node(n) {
		set_hash(md5_str);
		node->group = node;
	}

	void set_hash(std::string md5_str) {
		unsigned hash_idx = 0;
		bool high = true;
		memset(hash, 0, HASH_SIZE);
		for (unsigned i = 0; i < md5_str.size(); ++i) {
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

void read_file(Node *root_node, std::istream *infile, const char *filename)
{
	size_t line_nb = 1;
	for (std::string line; std::getline(*infile, line); ) {
		try {
			std::string f_md5, f_size, f_path;
			size_t pos = 0;
			size_t pos2 = 0;

			for (size_t j = 0; j < option_format.length(); ++j) {
				pos = line.find_first_not_of(' ', pos2);
				if (pos == std::string::npos) throw "parse error";
				pos2 = line.find_first_of(' ', pos);
				if (pos2 == std::string::npos) throw "parse error";
				switch (option_format[j]) {
				case '5':
					f_md5 = line.substr(pos, pos2 - pos);
					break;
				case 's':
					f_size = line.substr(pos, pos2 - pos);
					break;
				}
			}
			pos = line.find_first_not_of(' ', pos2);
			if (pos == std::string::npos) throw "parse error";
			f_path = line.substr(pos);

			unsigned long long size = 0;
			if (f_size.find_first_not_of("0123456789") == std::string::npos) {
				size = strtoull(f_size.c_str(), NULL, 10);
			}

			if (option_zero ||
			    size != 0 || f_md5 != "d41d8cd98f00b204e9800998ecf8427e") {
				Node *node = root_node->insert_node(f_path.c_str(), size);

				if (!node->group && f_md5.length() == 32 &&
				    f_md5.find_first_not_of("0123456789abcdef") == std::string::npos) {
					HashElt hash_elt(node, f_md5);
					hash_skip_list.insert(hash_elt);
				}
			}
		} catch (const char *s) {
			std::cout << filename << ":" << line_nb << ": " << s << std::endl;
		}
		++line_nb;
	}
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

	printf("node size : %u\n", sizeof(Node));
	printf("hash elt size : %u\n", sizeof(HashElt));

	Node *root_node = new Node(".", 1, false);

	std::multimap<unsigned long long, Node *> group_list;

	std::cout << "building tree / " << get_current_time() << std::endl;
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			std::ifstream infile;
			infile.open(argv[i]);
			read_file(root_node, &infile, argv[i]);
			infile.close();
		}
	} else {
		read_file(root_node, &std::cin, "stdin");
	}
	hash_skip_list.clear();

	std::cout << "breaking cycles / " << get_current_time() << std::endl;
	root_node->break_sibling_cycles();
	std::cout << "ungrouping directories / " << get_current_time() << std::endl;
	root_node->ungroup_dirs();
	std::cout << "finding dupes / " << get_current_time() << std::endl;
	root_node->find_dupes();
	root_node->reset_visited();
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
	std::cout << "building groups / " << get_current_time() << std::endl;
	root_node->build_group_list(&group_list, option_child_groups);
	std::cout << "done / " << get_current_time() << std::endl;
	for (std::multimap<unsigned long long, Node *>::const_reverse_iterator it =
	     group_list.rbegin(); it != group_list.rend(); ++it) {
//	for (std::multimap<unsigned long long, Node *>::const_iterator it =
//		 group_list.begin(); it != group_list.end(); ++it) {
		std::cout << "group size : " << it->first <<
			" (" << to_human_str(it->first) <<  ")" << std::endl;
		it->second->print_group();
	}

	if (!option_only_in.empty()) {
		Node *origin = root_node->find_node(option_only_in.c_str());
		if (origin) {
			origin->print_only_in_list(origin);
		}
	}

	group_list.clear();
	root_node->clear_children();
	delete root_node;

	printf("total_alloc : %llu %s\n", total_alloc, get_current_time().c_str());
}
