#ifndef LILY_SRC_COROUTINE_CO_SELECT_H_
#define LILY_SRC_COROUTINE_CO_SELECT_H_
#include "co_chan.h"
#include "epoller.h"
#include <vector>
#include <functional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
namespace lily {

  // 表示一种case
  template<typename T>
  struct Case {
    Channel<T> &chan;
    std::function<void(T)> func;
    template<typename F>
    Case(Channel<T> &ch, F &&f):chan(ch), func(std::forward<F>(f)) {}
  };

  template<typename T, typename F>
  Case<T> CreateCase(Channel<T> &ch, F &&f) {
    return Case<T>(ch, std::forward<F>(f));
  }

  struct DefaultCase {};

  static Channel<DefaultCase> _default;

  template<typename ...Args>
  inline static void foreachHelper(Args ...args) {}
  // 模拟golang的select语法
  template<typename ...Ts>
  class Selector {
   private:
    using HandlerPtr = std::unique_ptr<std::function<void()>>;
   private:
    // 防止析构
    std::vector<HandlerPtr> m_func_vec;
    Epoller m_epoll;
    HandlerPtr m_default_handler;
    bool m_has_default = false;
   public:
    // 初始化Selector
    template<typename T>
    int fillSelector(Case<T> &&t) {
      HandlerPtr handler(new std::function<void()>(std::bind([this](Channel<T> &ch, std::function<void(T)> fn) {
        // 尝试从ch中获取一个元素
        auto ptr = ch.try_fetch();
        // 获取失败（多个协程同时被唤醒，该元素已被消耗）
        if (ptr == nullptr) { return; }
        // 消耗掉对应的event fd的值
        size_t buf;
        read(ch.GetEventFd(), &buf, sizeof(buf));
        // 调用回调函数
        fn(std::move(*ptr));
      }, std::ref(t.chan), std::move(t.func))));
      epoll_event ev[1];
      ev->events = EPOLLIN;
      ev->data.ptr = handler.get();
      // 防止析构
      m_func_vec.emplace_back(std::move(handler));
      m_epoll.Add(t.chan.GetEventFd(), ev);
      return 0;
    }
    int fillSelector(Case<DefaultCase> &&t) {
      m_default_handler = HandlerPtr(new std::function<void()>(std::bind(
          [this](Channel<DefaultCase> &ch, std::function<void(DefaultCase)> fn) {
            // 调用回调函数
            fn(DefaultCase{});
          }, std::ref(t.chan), std::move(t.func))));
      m_has_default = true;
      return 0;
    }
    explicit Selector(Case<Ts> &&...chans) {
      foreachHelper(fillSelector(std::move(chans))...);
    }
    void Select() {
      std::vector<epoll_event> ev(1);
      if (m_has_default) {
        if (m_epoll.Epoll(ev, 0) <= 0) {
          (*m_default_handler)();
          return;
        }
      } else {
        m_epoll.Epoll(ev, -1);
      }
      // 调用handle函数
      (*static_cast<std::function<void()> *>(ev.front().data.ptr))();
    }
  };

  template<typename ...Ts>
  inline Selector<Ts...> Select(Case<Ts> &&...cases) {
    return Selector<Ts...>(std::move(cases)...);
  }
}

#endif
