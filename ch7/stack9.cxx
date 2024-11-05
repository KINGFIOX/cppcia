//! leaks nodes

#include <atomic>
#include <memory>

template <typename T>
class lock_free_stack {
 private:
  struct node {
    std::shared_ptr<T> data;
    std::shared_ptr<node> next;

    node(T const& data_) : data(std::make_shared<T>(data)) {}
  };
  std::shared_ptr<node> head;

 public:
  void push(T const& data) {
    std::shared_ptr<node> const new_node = std::make_shared<node>(data);
    new_node->next = std::atomic_load(&head);  // 共享指针, 但是采用 atomic_load, 但是可以采用: Arc
    // 成员函数的版本: ( &, value )
    // 静态的版本: (*, *, value )
    while (!std::atomic_compare_exchange_weak(&head, &new_node->next /*expect*/, new_node /*desired*/));
  }

  std::shared_ptr<T> pop() {
    std::shared_ptr<node> old_head = std::atomic_load(&head);
    while (old_head && !std::atomic_compare_exchange_weak(&head, &old_head, std::atomic_load(&old_head->next)));
    if (old_head) {
      std::atomic_store(&old_head->next, std::shared_ptr<node>());
      return old_head->data;
    }
    return std::shared_ptr<T>();
  }
  ~lock_free_stack() { while (pop()); }
};