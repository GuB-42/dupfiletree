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
	explicit Node(const char *name, size_t name_size, bool vnode);
	~Node();

	unsigned long long  size;
	char                *name;
	Node                *parent;
	Node                *child;
	Node                *sibling;
	Node                *group;
	unsigned            child_count;
	bool                sibling_dupe:1; // this node is the same as one of its siblings
	bool                parent_dupe:1; // this not is the same as its parent
	bool                slave:1;
	bool                visited:1;
	bool                vnode:1;
	bool                last_child:1;

	Node *insert_node(const char *path, unsigned long long size);
	Node *find_node(const char *path);
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
	void print_only_in_list(Node *origin);
	void build_group_list(std::multimap<unsigned long long, Node *> *group_list,
	                      bool child_groups);
	void print_group() const;
	void clear_children();
private:
	static bool group_sort_less(const Node *a, const Node *b);

	Node(const Node &);
	Node &operator=(const Node &);
};


#endif