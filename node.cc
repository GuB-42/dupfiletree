#include "node.h"

#include <iostream>
#include <map>
#include <set>
#include <string.h>

MemPool<char> string_pool;
MemPool<Node> NodePoolAlloc::pool;

Node::Node(const std::string &n) :
	size(0), parent(NULL), child(NULL), child_count(0),
	sibling(NULL), group(NULL), dupe(NULL), slave(false), visited(false),
	vnode(n.find("%%%%") != std::string::npos)
{
	name = string_pool.alloc(n.size() + 1);
	memcpy(name, n.c_str(), n.size() + 1);
}

Node::~Node() {
	string_pool.free(name);
}

Node *Node::insert_node(const std::string &path, unsigned long long size)
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

std::string Node::get_path() const
{
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

void Node::break_sibling_cycles()
{
	if (!child) return;
	Node *p = child;
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
				p->dupe = it->second;
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
		if (p->dupe) continue;
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

void Node::reset_visited()
{
	for (Node *p = child; p; p = p->sibling) {
		p->reset_visited();
	}
	visited = false;
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

bool Node::group_dirs(bool equal_only)
{
	bool res = false;
	for (Node *p = child; p; p = p->sibling) {
		if (p->group_dirs(equal_only)) res = true;
	}
	if (group_dir(equal_only)) res = true;
	return res;
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
	if (!child_groups) {
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

void Node::print_group() const
{
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

void Node::clear_children()
{
	while (child) {
		Node *t = child;
		child = t->sibling;
		delete t;
	}
}
