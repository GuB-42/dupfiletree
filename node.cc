#include "node.h"

#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <string.h>

#define VNODE_MARKER "%%%%"

MemPool<char> string_pool;
MemPool<Node> NodePoolAlloc::pool;

Node::Node(const char *n, size_t n_size, bool vnode) :
	size(0), parent(NULL), child(NULL),
	sibling(NULL), group(NULL), child_count(0),
	sibling_dupe(false), parent_dupe(false),
	slave(false), visited(false), vnode(vnode),
	last_child(false)
{
	name = string_pool.alloc(n_size + 1);
	memcpy(name, n, n_size);
	name[n_size] = '\0';
}

Node::~Node() {
	string_pool.free(name);
}

Node *Node::insert_node(const char *path, unsigned long long size)
{
	bool new_node = false;

	Node *cur_node = this;
	size_t path_len = strlen(path);
	const char *path_ptr = path;
	while (path_ptr) {
		size_t subpath_len = path_len - (path_ptr - path);
		bool subpath_vnode = false;

		const char *slash_ptr = strchr(path_ptr, '/');
		if (slash_ptr) {
			subpath_len = slash_ptr - path_ptr;
		}
		size_t vmsz = strlen(VNODE_MARKER);
		if (subpath_len >= vmsz &&
		    memcmp(path_ptr + subpath_len - vmsz, VNODE_MARKER, vmsz) == 0) {
			subpath_len -= vmsz;
			subpath_vnode = true;
		}

		Node *insert_after_point = NULL;
		bool insert_last_child = false;

		Node *found_p = NULL;
		if (cur_node->child) {
			Node *p = cur_node->child;
			Node *prev_p = NULL;
			while (!found_p && !insert_after_point) {
				int cmp_res = strncmp(path_ptr, p->name, subpath_len);
				if (cmp_res == 0) {
					if (p->name[subpath_len] != '\0') cmp_res = -1;
					if (!subpath_vnode && p->vnode) cmp_res = -1;
					if (subpath_vnode && !p->vnode) cmp_res = 1;
				}
				if (cmp_res == 0) {
					found_p = p;
				} else if (cmp_res > 0) {
					if (p->last_child) {
						insert_after_point = p;
						insert_last_child = true;
					} else {
						prev_p = p;
						p = p->sibling;
					}
				} else if (cmp_res < 0) {
					if (prev_p) {
						insert_after_point = prev_p;
						insert_last_child = false;
					} else {
						if (p == p->sibling) {
							insert_after_point = p;
							insert_last_child = false;
						} else {
							prev_p = p;
							while (!prev_p->last_child) {
								prev_p = prev_p->sibling;
							}
							p = prev_p->sibling;
						}
					}
				}
			}
		}

		if (!found_p) {
			Node *new_p = new Node(path_ptr, subpath_len, subpath_vnode);
			new_p->parent = cur_node;
			if (insert_after_point) {
				if (insert_last_child) {
					new_p->last_child = true;
					insert_after_point->last_child = false;
				}
				new_p->sibling = insert_after_point->sibling;
				insert_after_point->sibling = new_p;
			} else {
				new_p->last_child = true;
				new_p->sibling = new_p;
			}
			new_node = true;
			found_p = new_p;
		}

		cur_node->child = found_p;
		cur_node = found_p;
		path_ptr = slash_ptr;
		if (path_ptr) ++path_ptr;
	}

	if (new_node) {
		for (Node *p = cur_node; p; p = p->parent) {
			p->size += size;
			if (p->vnode) break;
		}
	}

	return cur_node;
}

Node *Node::find_node(const char *path)
{
	Node *cur_node = this;
	size_t path_len = strlen(path);
	const char *path_ptr = path;
	while (*path_ptr && cur_node) {
		size_t subpath_len = path_len - (path_ptr - path);
		const char *slash_ptr = strchr(path_ptr, '/');
		if (slash_ptr) {
			subpath_len = slash_ptr - path_ptr;
		}
		Node *p = cur_node->child;
		while (p != cur_node->child) {
			if (strncmp(path_ptr, p->name, subpath_len) == 0 &&
			    p->name[subpath_len] == '\0' && !p->vnode) break;
			p = p->sibling;
		}
		cur_node = p;
		path_ptr += subpath_len;
		while (*path_ptr == '/') ++path_ptr;
	}

	return cur_node;
}

std::string Node::get_flag_str() const
{
//	std::string res = "";
//	if (parent_dupe && !sibling_dupe) res += ":";
//	if (!parent_dupe && sibling_dupe) res += "*";
//	if (parent_dupe && sibling_dupe) res += "#";
//	if (vnode) res += "%";
//	if (slave) res += "s";
//	return res.empty() ? "" : std::string("{") + res + "}";
	return vnode ? VNODE_MARKER : "";
}

std::string Node::get_path() const
{
	std::string res;

	for (const Node *p = this; p && p->parent; p = p->parent) {
		std::string n = p->name + p->get_flag_str();
		res = res.empty() ? n : n + '/' + res;
	}
	return res;
}

void Node::break_sibling_cycles()
{
	if (!child) return;
	Node *p = child;
	while (!p->last_child) p = p->sibling;
	child = p->sibling;
	p->sibling = NULL;
	for (p = child; p; p = p->sibling) {
		p->break_sibling_cycles();
	}
}

void Node::find_dupes()
{
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
				p->sibling_dupe = true;
			} else {
				parents[p->parent] = p;
			}
			p->visited = true;
		}
	}
}

void Node::compute_child_counts()
{
	for (Node *p = child; p; p = p->sibling) {
		p->compute_child_counts();
	}

	child_count = 0;
	for (Node *p = child; p; p = p->sibling) {
		if (p->vnode) continue;
		if (p->sibling_dupe || p->parent_dupe) continue;
		++child_count;
	}
}

void Node::kill_singles()
{
	for (Node *p = child; p; p = p->sibling) {
		p->kill_singles();
	}
	if (child && group == this) group = NULL;
}

void Node::ungroup_dirs()
{
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

void Node::set_visited(bool value)
{
	for (Node *p = child; p; p = p->sibling) {
		p->set_visited(value);
	}
	visited = value;
}

bool Node::parent_slave() const
{
	for (const Node *p = this; p; p = p->parent) {
		if (p->slave) return true;
	}
	return false;
}

void Node::enslave_group()
{
	bool first = true;
	for (Node *p = this; first || p != this; p = p->group, first = false) {
		p->slave = true;
	}
}

void Node::print_tree(const std::string &prefix) const
{
	std::cout << prefix << "+-" << name << get_flag_str() <<
		" " << size << " " << child_count;
	if (parent_dupe && !sibling_dupe) std::cout << ":";
	if (!parent_dupe && sibling_dupe) std::cout << "*";
	if (parent_dupe && sibling_dupe) std::cout << "#";
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

bool Node::group_dir(bool equal_only)
{
	if (group) return false;
	if (!child) return false;
	for (Node *p = child; p; p = p->sibling) {
		if (!p->vnode && !p->group) return false;
	}

	// std::cout << get_path() << std::endl;

	if (child_count == 1) {
		for (Node *p = child; p; p = p->sibling) {
			if (!p->vnode) {
				p->parent_dupe = true;
				if (!p->sibling_dupe) {
					child_count = p->child_count;
					slave = p->slave;
					group = p->group;
					p->group = this;
				}
			}
		}
	} else {
		group = this;
	}

	std::map<Node *, IdElt> id_map;
	for (Node *p = child; p; p = p->sibling) {
		if (p->vnode) continue;
		if (p->sibling_dupe || p->parent_dupe) continue;
		for (Node *p2 = p->group; p2 != p; p2 = p2->group) {
			if (p2->vnode) continue;
			if (p2->sibling_dupe || p2->parent_dupe) continue;
			if (p2->parent == this) continue;
			if (!p2->parent) continue;
			Node *p2_parent = p2->parent;
			while (p2_parent->parent_dupe) {
				p2_parent = p2_parent->parent;
			}
			if (!p2_parent) continue;
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
				if (!p->sibling_dupe && !p->parent_dupe) {
					std::map<Node *, Node *>::const_iterator seen_it =
						seen.find(p->parent);
					if (seen_it == seen.end()) {
						seen[p->parent] = p;
					} else {
						p->sibling_dupe = true;
						if (!p->vnode) --p->parent->child_count;
					}
				}
			}
		}
	}

	return true;
}

bool Node::group_dirs(bool equal_only)
{
	bool res = false;
	for (Node *p = child; p; p = p->sibling) {
		if (p->group_dirs(equal_only)) res = true;
	}
	if (group_dir(equal_only)) res = true;
	return res;
}

void Node::print_only_in_list(Node *origin)
{
	bool found_elsewhere = false;

	if (group) {
		bool first = true;
		for (Node *p = this; first || p != this; p = p->group, first = false) {
			if (!p->slave) {
				bool found_origin = false;
				for (Node *p2 = p; p2; p2 = p2->parent) {
					if (p2 == origin) {
						found_origin = true;
						break;
					}
				}
				if (!found_origin) {
					found_elsewhere = true;
					break;
				}
			}
		}
	}

	if (!found_elsewhere) {
		std::cout << get_path() << std::endl;
		for (Node *p = child; p; p = p->sibling) {
			p->print_only_in_list(origin);
		}
	}
}

void Node::build_group_list(std::multimap<unsigned long long, Node *> *group_list,
                            bool child_groups)
{
	for (Node *p = child; p; p = p->sibling) {
		p->build_group_list(group_list, child_groups);
	}

	if (visited) return;
	if (!group) return;
	if (group == this) return;

	bool first = true;
	for (Node *p = this; first || p != this; p = p->group, first = false) {
		p->visited = true;
	}

	bool all_parent = false;
	if (!child_groups) {
		std::set<Node *> parent_group;
		bool parent_group_init = true;
		first = true;
		for (Node *p = this; first || p != this; p = p->group, first = false) {
			if (p->sibling_dupe || p->parent_dupe) continue;
			all_parent = false;
			if (!p->parent) break;
			if (!p->parent->group) break;
			if (p->vnode) break;
			if (p->parent->slave) break;
			if (parent_group_init) {
				bool first2 = true;
				for (Node *p2 = p->parent; first2 || p2 != p->parent;
				     p2 = p2->group, first2 = false) {
					parent_group.insert(p2);
				}
				parent_group_init = false;
			} else {
				if (parent_group.find(p->parent) == parent_group.end()) break;
				all_parent = true;
			}
		}
	}

	if (!all_parent) {
		bool have_master_size = false;
		unsigned long long total_size = 0;
		unsigned long long master_size = 0;
		first = true;
		for (Node *p = this; first || p != this; p = p->group, first = false) {
			if (!p->parent_dupe) {
				total_size += p->size;
			}
			if (!p->slave) {
				if (have_master_size) {
					if (p->size < master_size) {
						master_size = p->size;
					}
				} else {
					master_size = p->size;
					have_master_size = true;
				}
			}
		}
		group_list->insert(std::pair<unsigned long long, Node *>(total_size - master_size, this));
	}
}

bool Node::group_sort_less(const Node *a, const Node *b)
{
	if (!a->slave && b->slave) return true;
	if (a->slave && !b->slave) return false;
	if (b->size < a->size) return true;
	if (a->size < b->size) return false;

	unsigned depth_a = 0;
	for (const Node *p = a; p; p = p->parent) {
		if (b == p) return false;
		++depth_a;
	}
	unsigned depth_b = 0;
	for (const Node *p = b; p; p = p->parent) {
		if (a == p) return true;
		++depth_b;
	}

	const Node *pa = a;
	const Node *pb = b;
	while (depth_a < depth_b) {
		pb = pb->parent;
		--depth_b;
	}
	while (depth_b < depth_a) {
		pa = pa->parent;
		--depth_a;
	}
	while (pa->parent) {
		if (pa->parent == pb->parent) {
			int cmp_res = strcmp(pa->name, pb->name);
			if (cmp_res < 0) return true;
			if (cmp_res > 0) return false;
			if (!pa->vnode && pb->vnode) return true;
			if (pa->vnode && !pb->vnode) return false;
			return true; // out of options
		}
		pa = pa->parent;
		pb = pb->parent;
	}

	return true;
}

void Node::print_group() const
{
	std::vector<const Node *> cur_group;

	if (group) {
		bool first = true;
		for (const Node *p = this; first || p != this; p = p->group, first = false) {
			cur_group.push_back(p);
		}
		std::sort(cur_group.begin(), cur_group.end(), group_sort_less);
		for (std::vector<const Node *>::const_iterator it =
		     cur_group.begin(); it != cur_group.end(); ++it) {
			std::cout << ((*it)->slave ? " S " : " M ") <<
				(*it)->size << " " << (*it)->get_path() << std::endl;
		}
	} else {
		std::cout << "N" << size << " " << get_path() << std::endl;
	}
	std::cout << std::endl;
}

void Node::clear_children()
{
	while (child) {
		Node *t = child;
		child = t->sibling;
		delete t;
	}
}
