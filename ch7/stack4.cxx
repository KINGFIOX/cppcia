//! no leak nodes

#include <atomic>
#include <memory>

template <typename T>
class lock_free_stack {
 private:
  struct node {
    std::shared_ptr<T> data;
    node* next;

    node(T const& data_) : data(std::make_shared<T>(data)) {}
  };
  std::atomic<node*> head;

 private:
  std::atomic<unsigned> threads_in_pop;
  std::atomic<node*> to_be_deleted;

  static void delete_nodes(node* nodes) {
    while (nodes) {
      node* next = nodes->next;
      delete nodes;
      nodes = next;
    }
  }
  void chain_pending_node(node* n) { chain_pending_nodes(n, n); }
  void chain_pending_nodes(node* first, node* last) {
    last->next = to_be_deleted;
    while (!to_be_deleted.compare_exchange_weak(last->next, first));
  }
  void chain_pending_nodes(node* nodes) {
    node* last = nodes;
    while (node* const next = last->next) {
      last = next;
    }
    chain_pending_nodes(nodes, last);
  }

  void try_reclaim(node* old_head) {
    if (threads_in_pop == 1) {
      node* nodes_to_delete = to_be_deleted.exchange(nullptr);
      if (!--threads_in_pop) {  // 再次检查, 确定自己是唯一的 pop 线程, 这个时候 delete_nodes 是好的
        delete_nodes(nodes_to_delete);
      } else if (nodes_to_delete) {
        chain_pending_nodes(nodes_to_delete);  // not safe to delete any node, 重新加回到 to_be_delete
      }
      delete old_head;
    } else {
      chain_pending_node(old_head);  // not safe to delete the old_head, pend it into the chain
      --threads_in_pop;
    }
  }

 public:
  void push(T const& data) {
    node* const new_node = new node(data);  // exception here
    new_node->next = head.load();
    while (!head.compare_exchange_weak(new_node->next /*expect*/, new_node /*desired*/));
  }

  // FIXME: 这里有 hazard 指针问题
  std::shared_ptr<T> pop() {
    ++threads_in_pop;
    node* old_head = head.load();
    while (old_head && !head.compare_exchange_weak(old_head, old_head->next));
    std::shared_ptr<T> res;  //
    if (old_head) {
      res.swap(old_head->data);
    }
    try_reclaim(old_head);
    return res;
  }
};