#ifndef node_h_
#define node_h_

#include <string>
#include <map>
#include "mempool.h"

struct Node;
struct NodePoolAlloc {
	static void *operator new(size_t size) {
		return pool.alloc(size);
	}
	static void operator delete(void *p) {
		pool.free(static_cast<Node *>(p));
	}
	static MemPool<Node> pool;
};

struct Node : public NodePoolAlloc {
	explicit Node(const std::string &n);
	~Node();

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

	Node *insert_node(const std::string &path, unsigned long long size);
	std::string get_path() const;
	void break_sibling_cycles();
	void find_dupes();
	void compute_child_counts();
	void kill_singles();
	void ungroup_dirs();
	void reset_visited();
	bool parent_slave() const;
	void enslave_group();
	void print_tree(const std::string &prefix = "") const;
	bool group_dir(bool equal_only);
	bool group_dirs(bool equal_only);
	void build_group_list(std::multimap<unsigned long long, Node *> *group_list,
	                      bool child_groups);
	void print_group() const;
	void clear_children();
private:
	Node(const Node &);
	Node &operator=(const Node &);
};


#endif