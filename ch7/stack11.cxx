//! 这个版本是: 自己维护引用计数

#include <atomic>
#include <memory>

template <typename T>
class lock_free_stack {
 private:
  struct node;
  struct counted_node_ptr {  // 这个可能不是 atomic 的
    int external_count;
    node* ptr;
  };
  struct node {
    std::shared_ptr<T> data;
    std::atomic<int> internal_count;
    counted_node_ptr next;

    node(T const& data_) : data(std::make_shared<T>(data)), internal_count(0) {}
  };
  std::atomic<counted_node_ptr> head;

  void increase_head_count(counted_node_ptr& old_counter) {
    counted_node_ptr new_counter;
    do {
      new_counter = old_counter;
      ++new_counter.external_count;
    } while (!head.compare_exchange_strong(old_counter, new_counter));
    old_counter.external_count = new_counter.external_count;
  }

 public:
  void push(T const& data) {
    counted_node_ptr new_node;
    new_node.ptr = new node(data);
    new_node.external_count = 1;
    new_node.ptr->next = head.load();
    while (!head.compare_exchange_weak(new_node.ptr->next, new_node));
  }

  std::shared_ptr<T> pop() {
    std::shared_ptr<node> old_head = head.load();
    for (;;) {
      increase_head_count(old_head);
      node* const ptr = old_head.ptr;
      if (!ptr) {  // empty
        return std::shared_ptr<T>();
      }
      if (head.compare_exchange_strong(old_head, ptr->next)) {
        std::shared_ptr<T> res;
        res.swap(ptr->data);
        int const count_increase = old_head.external_count - 2;  // 一个引用是: head, 一个引用是: pop
        if (ptr->internal_count.fetch_add(count_increase) == -count_increase) {
          delete ptr;
        }
        return res;
      } else if (ptr->internal_count.fetch_sub(1) == 1) {
        delete ptr;
      }
    }
    return std::shared_ptr<T>();
  }
  ~lock_free_stack() { while (pop()); }
};