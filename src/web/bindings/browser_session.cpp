#include <emscripten/bind.h>
extern "C" {
#include <ascii-chat/network/browser/session.h>
#include "../client.h"
#include <ascii-chat/network/browser/security.h>
}
#include "client_bytes.h"
#include <sodium.h>
#include "byte_arrays.h"

#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

EMSCRIPTEN_DECLARE_VAL_TYPE(NullablePeerBytes);
EMSCRIPTEN_DECLARE_VAL_TYPE(PeerBytes);

class BrowserSessionBinding {
public:
  BrowserSessionBinding(bool server, int encryption, PeerBytes password, PeerBytes identity, PeerBytes expected) {
    auto pass = binding_copy_input(password);
    auto secret = binding_copy_input(identity);
    auto key = binding_copy_input(expected);
    if ((!secret.empty() && secret.size() != 64) || (!key.empty() && key.size() != 32)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer identity or verification key length");
    } else {
      pass.push_back(0);
      session_.reset(browser_session_create(server, encryption,
                                     pass.size() > 1 ? reinterpret_cast<char *>(pass.data()) : nullptr,
                                     secret.empty() ? nullptr : secret.data(), key.empty() ? nullptr : key.data()));
    }
    if (!pass.empty())
      sodium_memzero(pass.data(), pass.size());
    if (!secret.empty())
      sodium_memzero(secret.data(), secret.size());
    if (!key.empty())
      sodium_memzero(key.data(), key.size());
  }
  BrowserSessionBinding(const BrowserSessionBinding &) = delete;
  BrowserSessionBinding &operator=(const BrowserSessionBinding &) = delete;

  bool valid() const {
    return static_cast<bool>(session_);
  }
  bool ready() const {
    return session_ && browser_session_ready(session_.get());
  }
  std::string error() const {
    return browser_session_error();
  }
  void close() {
    session_.reset();
  }
  emscripten::val process(emscripten::val event) {
    emscripten::val result = emscripten::val::object();
    if (!session_) {
      const int code = SET_ERRNO(ERROR_INVALID_STATE, "Browser session is not initialized");
      result.set("code", code);
      result.set("ok", false);
      result.set("message", "Browser session is not initialized");
      result.set("retryable", false);
      result.set("outputs", emscripten::val::array());
      result.set("received", emscripten::val::null());
      result.set("queuedBytes", 0);
      result.set("writable", false);
      return result;
    }
    result.set("code", 0);
    const std::string type = event["type"].as<std::string>();
    if (type == "start") {
      result.set("code", static_cast<int>(browser_session_start(session_.get())));
    } else if (type == "connected" || type == "disconnected") {
      browser_session_set_connected(session_.get(), type == "connected");
    } else if (type == "send" || type == "receive") {
      auto bytes = binding_copy_input(event["packet"]);
      const int code = input_bytes(bytes, type == "receive");
      result.set("code", code);
      if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size());
    } else if (type == "tick") {
      result.set("code", static_cast<int>(browser_session_tick(session_.get())));
    } else if (type == "quic-begin") {
      result.set("code", browser_session_quic_begin(session_.get()));
    } else if (type == "quic-init") {
      result.set("code", static_cast<int>(browser_security_init(browser_session_security_context(session_.get()))));
    } else if (type == "quic-tick") {
      result.set("code", static_cast<int>(browser_security_tick(browser_session_security_context(session_.get()))));
      result.set("epoch", browser_security_epoch(browser_session_security_context(session_.get())));
      emscripten::val quic_outputs = emscripten::val::array();
      for (unsigned i = 0; i < 16; ++i) {
        uint8_t data[256];
        size_t length = 0;
        uint32_t flow = 0;
        const int code = browser_security_output(browser_session_security_context(session_.get()), data,
                                                 sizeof(data), &length, &flow);
        if (code || !length) break;
        emscripten::val output = emscripten::val::object();
        output.set("code", code);
        output.set("flow", flow);
        output.set("packet", binding_copy_output(data, length));
        quic_outputs.call<void>("push", output);
      }
      result.set("quicOutputs", quic_outputs);
    } else if (type == "quic-epoch") {
      result.set("epoch", browser_security_epoch(browser_session_security_context(session_.get())));
    } else if (type == "quic-packet") {
      auto packet = binding_copy_input(event["packet"]);
      const bool seal = event["seal"].as<bool>();
      std::vector<uint8_t> output(packet.size() + (seal ? 94 : 0));
      size_t length = 0;
      uint64_t sequence = 0;
      const int code = browser_security_packet(browser_session_security_context(session_.get()), seal,
                                               event["flow"].as<unsigned>(), event["delivery"].as<unsigned>(),
                                               packet.data(), packet.size(), output.data(), output.size(), &length,
                                               &sequence);
      result.set("code", code);
      result.set("packet", binding_copy_output(output.data(), code ? 0 : length));
      result.set("sequence", emscripten::val::global("BigInt")(std::to_string(sequence)));
      if (!packet.empty()) sodium_memzero(packet.data(), packet.size());
      if (!output.empty()) sodium_memzero(output.data(), output.size());
    } else {
        result.set("code", SET_ERRNO(ERROR_INVALID_PARAM, "Unknown browser session event"));
    }
    emscripten::val outputs = emscripten::val::array();
    for (unsigned i = 0; i < 16; ++i) {
      const void *data = browser_session_output_data(session_.get());
      if (!data) break;
      outputs.call<void>("push", binding_copy_output(data, browser_session_output_size(session_.get())));
      browser_session_output_pop(session_.get());
    }
    result.set("outputs", outputs);
    const void *received = browser_session_received_data(session_.get());
    const size_t received_size = browser_session_received_size(session_.get());
    result.set("received", received && received_size ? binding_copy_output(received, received_size)
                                                       : emscripten::val::null());
    result.set("queuedBytes", browser_session_queued_bytes(session_.get()));
    result.set("writable", browser_session_writable(session_.get(), 0));
    result.set("ok", result["code"].as<int>() == 0);
    return result;
  }

  emscripten::val processBatch(emscripten::val events) {
    emscripten::val batch = emscripten::val::array();
    const unsigned count = events["length"].as<unsigned>();
    if (count > 128) {
      emscripten::val error = emscripten::val::object();
      error.set("code", static_cast<int>(SET_ERRNO(ERROR_INVALID_PARAM, "Browser session event batch exceeds 128 events")));
      error.set("ok", false);
      error.set("message", "Browser session event batch exceeds 128 events");
      error.set("retryable", false);
      batch.call<void>("push", error);
      return batch;
    }
    for (unsigned index = 0; index < count; ++index)
      batch.call<void>("push", process(events[index]));
    return batch;
  }

private:
  int input_bytes(const std::vector<uint8_t> &bytes, bool receive) {
    return receive ? browser_session_receive(session_.get(), bytes.data(), bytes.size())
                   : browser_session_send(session_.get(), bytes.data(), bytes.size());
  }
  std::unique_ptr<browser_session_t, decltype(&browser_session_destroy)> session_{nullptr, &browser_session_destroy};
};

EMSCRIPTEN_BINDINGS(ascii_chat_browser_session) {
  emscripten::register_type<PeerBytes>("PeerBytes", "Uint8Array");
  emscripten::register_type<NullablePeerBytes>("NullablePeerBytes", "Uint8Array | null");
  emscripten::class_<BrowserSessionBinding>("BrowserSessionBinding")
      .constructor<bool, int, PeerBytes, PeerBytes, PeerBytes>()
      .function("valid", &BrowserSessionBinding::valid)
      .function("ready", &BrowserSessionBinding::ready)
      .function("error", &BrowserSessionBinding::error)
      .function("close", &BrowserSessionBinding::close)
      .function("process", &BrowserSessionBinding::process)
      .function("processBatch", &BrowserSessionBinding::processBatch);
}
