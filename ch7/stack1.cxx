#include <atomic>

template <typename T>
class lock_free_stack {
 private:
  struct node {
    T data;
    node* next;

    node(T const& data_) : data(data_) {}
  };
  std::atomic<node*> head;

 public:
  void push(T const& data) {
    node* const new_node = new node(data);
    new_node->next = head.load();
    // 因为我这个是 external explicit looping, 所以我是可以使用 compare_exchange_weak 的
    // 就是要轮询 failure 的情况
    while (!head.compare_exchange_weak(new_node->next /*expect*/, new_node /*desired*/));
  }

  void pop(T& result) {
    node* old_head = head.load();
    while (!head.compare_exchange_weak(old_head, old_head->next));
    result = old_head->data;
  }
};