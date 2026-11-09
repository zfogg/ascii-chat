#include <emscripten/bind.h>
extern "C" {
#include <session/av_state.h>
}

class AVStateBinding {
public:
  AVStateBinding() : state_(session_av_state_create()) {}
  ~AVStateBinding() {
    close();
  }
  AVStateBinding(const AVStateBinding &) = delete;
  AVStateBinding &operator=(const AVStateBinding &) = delete;
  bool valid() const {
    return state_ != nullptr;
  }
  void close() {
    if (!state_) return;
    session_av_state_destroy(state_);
    state_ = nullptr;
  }
  unsigned generation() const {
    return session_av_state_generation(state_);
  }
  unsigned begin() {
    return session_av_state_begin(state_);
  }
  bool complete(unsigned generation) {
    return session_av_state_complete(state_, generation);
  }
  bool current(unsigned generation) {
    return session_av_state_is_current(state_, generation);
  }
  bool active() {
    return session_av_state_is_active(state_);
  }
  void beginStop() {
    session_av_state_begin_stop(state_);
  }
  void finishStop() {
    session_av_state_finish_stop(state_);
  }

private:
  session_av_state_t *state_;
};

EMSCRIPTEN_BINDINGS(ascii_chat_av_state) {
  emscripten::class_<AVStateBinding>("AVStateBinding")
      .constructor<>()
      .function("valid", &AVStateBinding::valid)
      .function("close", &AVStateBinding::close)
      .function("generation", &AVStateBinding::generation)
      .function("begin", &AVStateBinding::begin)
      .function("complete", &AVStateBinding::complete)
      .function("current", &AVStateBinding::current)
      .function("active", &AVStateBinding::active)
      .function("beginStop", &AVStateBinding::beginStop)
      .function("finishStop", &AVStateBinding::finishStop);
}
