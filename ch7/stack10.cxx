//! NOTE: `std::experimental::atomic_shared_ptr` Merged into ISO C++ The functionality
//! was merged into the mainline ISO C++ standard as of 11/2017; see `std::atomic<shared_ptr<T>>`

#include <atomic>
#include <memory>

template <typename T>
class lock_free_stack {
 private:
  struct node {
    std::shared_ptr<T> data;
    std::atomic<std::shared_ptr<node>> next;

    node(T const& data_) : data(std::make_shared<T>(data)) {}
  };
  std::atomic<std::shared_ptr<node>> head;

 public:
  void push(T const& data) {
    std::shared_ptr<node> const new_node = std::make_shared<node>(data);
    new_node->next = head.load();
    while (!head.compare_exchange_weak(new_node->next, new_node));
  }

  std::shared_ptr<T> pop() {
    std::shared_ptr<node> old_head = head.load();
    while (old_head && !head.compare_exchange_weak(old_head, old_head->next.load()));
    if (old_head) {
      old_head->next = std::shared_ptr<node>();
      return old_head->data;
    }
    return std::shared_ptr<T>();
  }
  ~lock_free_stack() { while (pop()); }
};