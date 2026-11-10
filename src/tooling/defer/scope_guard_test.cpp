#include "../../../include/ascii-chat/tooling/defer/defer.h"

#include <cstdio>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int return_before_cleanup(int &observed) {
  int value = 4;
  defer({
    observed = value;
    value = 9;
  });
  return value++;
}

int main() {
  int trace = 0;
  {
    defer(trace = trace * 10 + 1);
    defer(trace = trace * 10 + 2);
    {
      defer(trace = trace * 10 + 3);
    }
    CHECK(trace == 3);
  }
  CHECK(trace == 321);

  trace = 0;
  {
    defer_named(first, trace = trace * 10 + 1);
    defer_named(second, trace = trace * 10 + 2);
    defer_cancel(first);
    defer_cancel(first);
  }
  CHECK(trace == 2);

  trace = 0;
  for (int i = 0; i < 3; ++i) {
    defer(trace = trace * 10 + i + 1);
    if (i == 0)
      continue;
    break;
  }
  CHECK(trace == 12);

  int observed = 0;
  CHECK(return_before_cleanup(observed) == 4);
  CHECK(observed == 5);

  trace = 0;
  try {
    defer(trace = trace * 10 + 1);
    {
      defer(trace = trace * 10 + 2);
      throw 7;
    }
  } catch (int value) {
    CHECK(value == 7);
  }
  CHECK(trace == 21);

  trace = 0;
  {
    auto first = ascii_defer_detail::make_guard([&]() { ++trace; });
    auto second = static_cast<decltype(first) &&>(first);
  }
  CHECK(trace == 1);
  return 0;
}
