//! no leak nodes

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

unsigned const max_hazard_pointers = 100;

struct hazard_pointer {
  std::atomic<std::thread::id> id;
  std::atomic<void*> pointer;
};

hazard_pointer hazard_pointers[max_hazard_pointers];

class hp_owner {
  hazard_pointer* hp;

 public:
  hp_owner(hp_owner const&) = delete;
  hp_owner operator=(hp_owner const&) = delete;
  hp_owner() : hp{nullptr} {
    for (unsigned i = 0; i < max_hazard_pointers; ++i) {
      std::thread::id old_id;  // 默认构造是: constructs an id that does not represent a thread
      if (hazard_pointers[i].id.compare_exchange_strong(old_id, std::this_thread::get_id())) {
        hp = &hazard_pointers[i];
        break;
      }
    }
    if (!hp) {
      throw std::runtime_error("No hazard pointers available");
    }
  }
  std::atomic<void*>& get_pointer() { return hp->pointer; }
  ~hp_owner() {
    hp->pointer.store(nullptr);
    hp->id.store(std::thread::id());
  }
};

std::atomic<void*>& get_hazard_pointer_for_current_thread() {
  thread_local static hp_owner hazard;  // each thread has its own hazard pointer
  return hazard.get_pointer();
}

template <typename T>
void do_delete(void* p) {
  delete static_cast<T*>(p);
}

struct data_to_reclaim {
  void* data;
  std::function<void(void*)> deleter;
  data_to_reclaim* next;
  template <typename T>  // 比较神奇, 泛型的构造函数
  data_to_reclaim(T* p) : data(p), deleter(&do_delete<T>), next(nullptr) {}
  ~data_to_reclaim() { deleter(data); }
};

std::atomic<data_to_reclaim*> nodes_to_reclaim;

void add_to_reclaim_list(data_to_reclaim* node) {
  node->next = nodes_to_reclaim.load();
  while (!nodes_to_reclaim.compare_exchange_weak(node->next, node));  // 链表头插
}

bool outstanding_hazard_pointers_for(void* p) {
  for (unsigned i = 0; i < max_hazard_pointers; ++i) {
    if (hazard_pointers[i].pointer.load() == p) {
      return true;
    }
  }
  return false;
}

void delete_nodes_with_no_hazards() {
  data_to_reclaim* current = nodes_to_reclaim.exchange(nullptr);
  while (current) {
    data_to_reclaim* const next = current->next;
    if (!outstanding_hazard_pointers_for(current->data)) {
      delete current;
    } else {
      add_to_reclaim_list(current);
    }
    current = next;
  }
}

template <typename T>
void reclaim_later(T* data) {
  add_to_reclaim_list(new data_to_reclaim(data));
}

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
    std::atomic<void*>& hp = get_hazard_pointer_for_current_thread();
    node* old_head = head.load();
    do {
      node* temp;
      do {
        temp = old_head;
        hp.store(old_head);      // 1. 保存 old_head, 声明当前线程正在访问 old_head
        old_head = head.load();  // 2. 重新加载 old_head
      } while (old_head != temp);  // 如果 head 没有被其他线程修改, 则说明 old_head 是安全的
    } while (old_head && !head.compare_exchange_strong(old_head, old_head->next));
    hp.store(nullptr);
    std::shared_ptr<T> res;
    if (old_head) {
      res.swap(old_head->data);
      if (outstanding_hazard_pointers_for(old_head)) {
        reclaim_later(old_head);
      } else {
        delete old_head;
      }
      delete_nodes_with_no_hazards();
    }
    return res;
  }
};