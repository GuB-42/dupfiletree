#include <string>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#include "skiplist.h"
#include "mempool.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool        option_equal = false;
bool        option_print_tree = false;
bool        option_child_groups = false;
bool        option_zero = false;
std::string option_format("5s");

MemPool<char> string_pool;

struct Node;
struct NodePoolAlloc {
	static void *operator new(size_t size) {
		++count;
		return pool.alloc(size);
	}
	static void operator delete(void *) {
		--count;
		if (!count) pool.clear();
	}
	static MemPool<Node> pool;
	static size_t count;
};
MemPool<Node> NodePoolAlloc::pool;
size_t NodePoolAlloc::count;

struct Node : public NodePoolAlloc {
	explicit Node(std::string n) :
		size(0), parent(NULL), child(NULL), child_count(0),
		sibling(NULL), group(NULL), dupe(NULL), slave(false), visited(false),
		vnode(n.find("%%%%") != std::string::npos) {
		name = string_pool.alloc(n.size() + 1);
		memcpy(name, n.c_str(), n.size() + 1);
	};

	char                *name;
	unsigned long long  size;
	Node                *parent;
	Node                *child;
	unsigned            child_count;
	Node                *sibling;
	Node                *group;
	Node                *dupe;
	bool                slave;
	bool                visited;
	bool                vnode;

	Node *insert_node(const std::string &path, unsigned long long size)
	{
		bool new_node = false;

		Node *cur_node = this;
		for (size_t pos = 0; pos != std::string::npos; ) {
			size_t slash_pos = path.find('/', pos);
			std::string new_name;
			if (slash_pos == std::string::npos) {
				new_name = path.substr(pos);
				pos = std::string::npos;
			} else {
				new_name = path.substr(pos, slash_pos - pos);
				pos = slash_pos + 1;
			}

			Node *p = NULL;
			if (cur_node->child) {
				p = cur_node->child;
				if (p->name != new_name) {
					p = p->sibling;
					while (p != cur_node->child) {
						if (p->name == new_name) break;
						p = p->sibling;
					}
					if (p == cur_node->child) p = NULL;
				}
			}

			if (!p) {
				p = new Node(new_name);
				p->parent = cur_node;
				if (cur_node->child) {
					p->sibling = cur_node->child->sibling;
					cur_node->child->sibling = p;
				} else {
					p->sibling = p;
				}
				p->vnode = (new_name.find("%%%%") != std::string::npos);
				new_node = true;
			}

			cur_node->child = p;
			cur_node = p;
		}

		if (new_node) {
			for (Node *p = cur_node; p; p = p->parent) {
				p->size += size;
				if (p->vnode) break;
			}
		}

		return cur_node;
	}

	std::string get_path() const {
		std::string res;

		for (const Node *p = this; p; p = p->parent) {
			if (res.empty()) {
				res = std::string(p->name);
			} else {
				res = std::string(p->name) + '/' + res;
			}
		}
		return res;
	}

	void break_sibling_cycles() {
		if (!child) return;
		Node *p = child;
		child = p->sibling;
		p->sibling = NULL;
		for (p = child; p; p = p->sibling) {
			p->break_sibling_cycles();
		}
	}

	void find_dupes() {
		for (Node *p = child; p; p = p->sibling) {
			p->find_dupes();
		}

		if (visited) return;
		if (!group) return;
		if (group == this) return;

		std::map<Node *, Node *> parents;

		for (unsigned i = 0; i < 4; ++i) {
			bool first = true;
			for (Node *p = this; first || p != this; p = p->group, first = false) {
				if (p->visited) continue;
				if (!(i & 1) && p->slave) continue;
				if (!(i & 2) && p->vnode) continue;

				std::map<Node *, Node *>::const_iterator it = parents.find(p->parent);
				if (it != parents.end()) {
					p->dupe = it->second;
				} else {
					parents[p->parent] = p;
				}
				p->visited = true;
			}
		}
	}

	void compute_child_counts() {
		for (Node *p = child; p; p = p->sibling) {
			p->compute_child_counts();
		}

		child_count = 0;
		for (Node *p = child; p; p = p->sibling) {
			if (p->vnode) continue;
			if (p->dupe) continue;
			++child_count;
		}
	}

	void kill_singles() {
		for (Node *p = child; p; p = p->sibling) {
			p->kill_singles();
		}
		if (child && group == this) group = NULL;
	}

	void ungroup_dirs() {
		for (Node *p = child; p; p = p->sibling) {
			p->ungroup_dirs();
		}
		if (child && group) {
			Node *last = this;
			for (Node *p = group; p != this; p = last->group) {
				if (p->child) {
					last->group = p->group;
					p->group = NULL;
				} else {
					last = p;
				}
			}
			last->group = group;
			group = NULL;
		}
	}

	void reset_visited() {
		for (Node *p = child; p; p = p->sibling) {
			p->reset_visited();
		}
		visited = false;
	}

	bool parent_slave() const {
		for (const Node *p = this; p; p = p->parent) {
			if (p->slave) return true;
		}
		return false;
	}

	void enslave_group() {
		bool first = true;
		for (Node *p = this; first || p != this; p = p->group, first = false) {
			p->slave = true;
		}
	}

	void print_tree(const std::string &prefix = "") const {
		std::cout << prefix << "+-" << name << " " << size << " " << child_count;
		if (dupe) std::cout << "*";
		if (group) {
			if (group == group->group) {
				std::cout << " (U) ";
			} else if (slave) {
				std::cout << " (S) ";
			} else {
				std::cout << " (G) ";
			}
		}
		std::cout << std::endl;
		for (Node *p = child; p; p = p->sibling) {
			if (sibling) {
				p->print_tree(prefix + "| ");
			} else {
				p->print_tree(prefix + "  ");
			}
		}
	}

	struct IdElt {
		IdElt() : count(0), slave(false), master(false) {};
		unsigned count;
		bool     slave;
		bool     master;
	};

	bool group_dir(bool equal_only)	{
		if (group) return false;
		if (!child) return false;
		for (Node *p = child; p; p = p->sibling) {
			if (!p->vnode && !p->group) return false;
		}

		// std::cout << get_path() << std::endl;

		if (child_count == 1) {
			for (Node *p = child; p; p = p->sibling) {
				if (!p->dupe && !p->vnode) {
					slave = p->slave;
					p->dupe = this;
					child_count = p->child_count;
					group = p->group;
					p->group = this;
				}
			}
		} else {
			group = this;
		}

		std::map<Node *, IdElt> id_map;
		for (Node *p = child; p; p = p->sibling) {
			if (p->vnode) continue;
			if (p->dupe) continue;
			for (Node *p2 = p->group; p2 != p; p2 = p2->group) {
				if (p2->vnode) continue;
				if (p2->dupe) continue;
				if (p2->parent == this) continue;
				if (!p2->parent) continue;
				Node *p2_parent = p2->parent;
				while (p2_parent->dupe &&
				       p2_parent->dupe == p2_parent->parent) {
					p2_parent = p2_parent->dupe;
				}
				if (!p2_parent) continue;
				if (p2_parent->dupe) continue;
				IdElt &ide = id_map[p2_parent];
				++ide.count;
				if (p->slave) ide.master = true;
				if (p2->parent_slave()) ide.slave = true;
			}
		}

		bool group_has_slaves = false;
		for (std::map<Node *, IdElt>::const_iterator it = id_map.begin();
		     it != id_map.end(); ++it) {
			bool master_node = false;
			bool slave_node = false;

			if (it->first->child_count == child_count &&
				!it->second.master && !it->second.slave) { // equal
				if (it->second.count != child_count) continue;
			} else if (it->first->child_count >= child_count && !it->second.slave) { // master
				if (equal_only) continue;
				if (it->second.count != child_count) continue;
				if (group_has_slaves) continue;
				master_node = true;
			} else if (it->first->child_count <= child_count && !it->second.master) { // slave
				if (equal_only) continue;
				if (it->second.count != it->first->child_count) continue;
				if (slave) continue;
				slave_node = true;
			} else {
				continue;
			}

			if (it->first->group) {
				bool slave_group = false;

				if (it->first->slave) slave_group = true;
				for (Node *p = it->first->group; p != it->first; p = p->group) {
					if (p->slave) slave_group = true;
					id_map.erase(p);
				}
				if (!slave_group || !slave_node) {
					if (slave_node || slave) {
						it->first->enslave_group();
						group_has_slaves = true;
					} else if (master_node) {
						enslave_group();
						group_has_slaves = true;
					} else if (slave_group) {
						group_has_slaves = true;
					}

					Node *t = it->first->group;
					it->first->group = group;
					group = t;
				}
			} else {
				if (slave_node || slave) {
					it->first->slave = true;
					group_has_slaves = true;
				} else if (master_node) {
					enslave_group();
					group_has_slaves = true;
				}
				it->first->group = group;
				group = it->first;
			}
		}

		std::map<Node *, Node *> seen;
		if (group && group != this) {
			for (unsigned i = 0; i < 4; ++i) {
				bool first = true;
				for (Node *p = this; first || p != this; p = p->group, first = false) {
					if (!(i & 1) && p->slave) continue;
					if ((i & 1) && !p->slave) continue;
					if (!(i & 2) && p->vnode) continue;
					if ((i & 2) && !p->vnode) continue;
					if (!p->dupe) {
						std::map<Node *, Node *>::const_iterator seen_it =
							seen.find(p->parent);
						if (seen_it == seen.end()) {
							seen[p->parent] = p;
						} else {
							p->dupe = seen_it->second;
							if (!p->vnode) --p->parent->child_count;
						}
					}
				}
			}
		}

		return true;
	}

	bool group_dirs(bool equal_only)
	{
		bool res = false;
		for (Node *p = child; p; p = p->sibling) {
			if (p->group_dirs(equal_only)) res = true;
		}
		if (group_dir(equal_only)) res = true;
		return res;
	}

	void build_group_list(std::multimap<unsigned long long, Node *> *group_list)
	{
		for (Node *p = child; p; p = p->sibling) {
			p->build_group_list(group_list);
		}

		if (visited) return;
		if (!group) return;
		if (group == this) return;

		std::set<Node *> cur_group;
		std::map<Node *, Node *> cur_group_parents;

		bool first = true;
		for (Node *p = this; first || p != this; p = p->group, first = false) {
			bool parent_found = false;
			for (Node *p2 = p->parent; p2; p2 = p2->parent) {
				if (cur_group.find(p2) != cur_group.end()) {
					parent_found = true;
					break;
				}
			}
			if (!parent_found) {
				std::map<Node *, Node *>::const_iterator pit = cur_group_parents.find(p);
				if (pit != cur_group_parents.end()) {
					cur_group.erase(pit->second);
				}
				cur_group.insert(p);
				for (Node *p2 = p->parent; p2; p2 = p2->parent) {
					cur_group_parents[p2] = p;
				}
			}
			p->visited = true;
		}

		bool all_parent = false;
		if (!option_child_groups) {
			std::set<Node *> parent_group;
			bool parent_group_init = true;
			for (std::set<Node *>::const_iterator it =
			     cur_group.begin(); it != cur_group.end(); ++it) {
				if ((*it)->dupe) continue;
				all_parent = false;
				if (!(*it)->parent) break;
				if (!(*it)->parent->group) break;
				// if (!(*it)->slave && (*it)->parent->slave) break;
				if (parent_group_init) {
					bool first = true;
					for (Node *p = (*it)->parent; first || p != (*it)->parent;
					     p = p->group, first = false) {
						parent_group.insert(p);
					}
					parent_group_init = false;
				} else {
					if (parent_group.find((*it)->parent) == parent_group.end()) break;
					all_parent = true;
				}
			}
		}

		if (!all_parent) {
			unsigned long long total_size = 0;
			unsigned long long master_size = 0;
			for (std::set<Node *>::const_iterator it =
			     cur_group.begin(); it != cur_group.end(); ++it) {
				total_size += (*it)->size;
				if (!(*it)->slave) master_size = (*it)->size;
			}
			group_list->insert(std::pair<unsigned long long, Node *>(total_size - master_size, this));
		}
	}

	void print_group() const {
		std::multimap<unsigned long long, const Node *> cur_group;

		bool first = true;
		for (const Node *p = this; first || p != this; p = p->group, first = false) {
			cur_group.insert(std::pair<unsigned long long, const Node *>(p->size, p));
		}
		for (std::map<unsigned long long, const Node *>::const_reverse_iterator it =
		     cur_group.rbegin(); it != cur_group.rend(); ++it) {
			std::cout << (it->second->slave ? " S " : " M ") <<
				it->second->size << " " << it->second->get_path() << std::endl;
		}
		std::cout << std::endl;
	}

	void clear_children() {
		while (child) {
			Node *t = child;
			child = t->sibling;
			delete t;
		}
	}
};

struct HashElt;
struct HashPoolAlloc {
	static void *operator new(size_t size) {
		++count;
		return pool.alloc(size);
	}
	static void operator delete(void *) {
		--count;
		if (!count) pool.clear();
	}
	static MemPool<HashElt> pool;
	static size_t count;
};
MemPool<HashElt> HashPoolAlloc::pool;
size_t HashPoolAlloc::count = 0;

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

int main(int argc, char* argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "hef:tcz")) != -1) {
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
		case 'h':
		default:
			std::cerr << "Usage: " << argv[0] << " [-ectz] [-f (5s)] file" << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	Node root_node(".");

	std::multimap<unsigned long long, Node *> group_list;

	std::cout << "building tree" << std::endl;

	for (int i = 1; i < argc; ++i) {
		std::ifstream infile;
		infile.open(argv[i]);
		for (std::string line; std::getline(infile, line); ) {
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
					Node *node = root_node.insert_node(f_path, size);

					if (!node->group && f_md5.length() == 32 &&
					    f_md5.find_first_not_of("0123456789abcdef") == std::string::npos) {
						HashElt hash_elt(node, f_md5);
						hash_skip_list.insert(hash_elt);
					}
				}
			} catch (const char *s) {
				std::cout << s << " : " << line << std::endl;
			}
		}
		infile.close();
	}
	hash_skip_list.clear();

	std::cout << "breaking cycles" << std::endl;
	root_node.break_sibling_cycles();
	std::cout << "ungrouping directories" << std::endl;
	root_node.ungroup_dirs();
	std::cout << "finding dupes" << std::endl;
	root_node.find_dupes();
	root_node.reset_visited();
	std::cout << "counting child" << std::endl;
	root_node.compute_child_counts();
	unsigned itn = 0;
	std::cout << "grouping directories (equal)" << std::endl;
	while (1) { std::cout << ++itn << std::endl; if (!root_node.group_dirs(true)) break; }
	if (!option_equal) {
		std::cout << "grouping directories (master/slave)" << std::endl;
		root_node.kill_singles();
		while (1) { std::cout << ++itn << std::endl; if (!root_node.group_dirs(false)) break; }
	}

	if (option_print_tree) root_node.print_tree();
	std::cout << "building groups" << std::endl;
	root_node.build_group_list(&group_list);
	for (std::multimap<unsigned long long, Node *>::const_reverse_iterator it =
	     group_list.rbegin(); it != group_list.rend(); ++it) {
//	for (std::multimap<unsigned long long, Node *>::const_iterator it =
//		 group_list.begin(); it != group_list.end(); ++it) {
		std::cout << "group size : " << it->first <<
			" (" << to_human_str(it->first) <<  ")" << std::endl;
		it->second->print_group();
	}

	group_list.clear();
	root_node.clear_children();
	string_pool.clear();
}