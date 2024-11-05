//! leaks nodes

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

 public:
  void push(T const& data) {
    node* const new_node = new node(data);  // exception here
    new_node->next = head.load();
    while (!head.compare_exchange_weak(new_node->next /*expect*/, new_node /*desired*/));
  }

  std::shared_ptr<T> pop() {
    node* old_head = head.load();
    while (!head.compare_exchange_weak(old_head, old_head->next));
    // 这里会有 node 资源泄露问题, 但是不可能马上释放的, 因为不知道有没有别的线程还在使用这个 node
    return old_head ? old_head->data : std::shared_ptr<T>();  // pop empty stack
  }
};