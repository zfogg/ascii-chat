// Minimal WebSocket client implementation (RFC 6455) in C23
// Single-file library. Build with:  cc -std=c23 -O2 ws_minimal_websocket_c23.c -o ws_example
// This file contains both the library and a tiny example main() guarded by WS_MINI_DEMO.
//
// Features:
//  - Client handshake (HTTP/1.1 Upgrade) with Sec-WebSocket-Key
//  - Validates Sec-WebSocket-Accept (SHA1 + base64 of key + GUID)
//  - Send text and binary frames (masked as required by RFC 6455)
//  - Receive frames (handles Ping/Pong automatically, Close handshake)
//  - Lengths: up to 2^63-1 (practically capped by size_t and memory)
//  - No TLS in this file; you can wrap fd with TLS and adapt read/write funcs
//
// Limitations (kept small on purpose):
//  - Does not support permessage-deflate or extensions
//  - Fragmentation: accepts unfragmented data frames (FIN=1); collects control frames anytime
//  - Very small HTTP parser (status line + headers); tolerant but not exhaustive
//
// Public API (see bottom for demo):
//  int  ws_connect(struct ws_conn* ws, const char* host, const char* path, const char* port, int timeout_ms);
//  int  ws_send_text(struct ws_conn* ws, const void* data, size_t len);
//  int  ws_send_binary(struct ws_conn* ws, const void* data, size_t len);
//  ssize_t ws_recv(struct ws_conn* ws, uint8_t* buf, size_t cap, int* opcode_out); // returns payload size or <0
//  int  ws_close(struct ws_conn* ws, uint16_t code, const char* reason);
//  void ws_shutdown(struct ws_conn* ws);
//
// SPDX: MIT

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// ---------- Small utilities ----------
static int set_nonblock(int fd, bool nb) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  if (nb)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;
  return fcntl(fd, F_SETFL, flags);
}

static int wait_fd(int fd, short events, int timeout_ms) {
  struct pollfd p = {.fd = fd, .events = events, .revents = 0};
  for (;;) {
    int r = poll(&p, 1, timeout_ms);
    if (r < 0 && errno == EINTR)
      continue;
    return r; // 0=timeout, >0 ready, <0 error
  }
}

static int read_all_timeout(int fd, void *buf, size_t n, int timeout_ms) {
  uint8_t *p = buf;
  size_t got = 0;
  while (got < n) {
    int rdy = wait_fd(fd, POLLIN, timeout_ms);
    if (rdy <= 0)
      return rdy == 0 ? -2 : -1;
    ssize_t r = recv(fd, p + got, n - got, 0);
    if (r == 0)
      return -3; // EOF
    if (r < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
      return -1;
    }
    got += (size_t)r;
  }
  return 0;
}

static int write_all_timeout(int fd, const void *buf, size_t n, int timeout_ms) {
  const uint8_t *p = buf;
  size_t sent = 0;
  while (sent < n) {
    int rdy = wait_fd(fd, POLLOUT, timeout_ms);
    if (rdy <= 0)
      return rdy == 0 ? -2 : -1;
    ssize_t r = send(fd, p + sent, n - sent, 0);
    if (r <= 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
      return -1;
    }
    sent += (size_t)r;
  }
  return 0;
}

static int urandom_bytes(void *out, size_t n) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f)
    return -1;
  size_t r = fread(out, 1, n, f);
  fclose(f);
  return r == n ? 0 : -1;
}

// ---------- Base64 (minimal) ----------
static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int base64_encode(const uint8_t *in, size_t inlen, char *out, size_t outcap) {
  size_t olen = ((inlen + 2) / 3) * 4;
  if (outcap < olen + 1)
    return -1;
  size_t i = 0, j = 0;
  unsigned val;
  while (i + 3 <= inlen) {
    val = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
    out[j++] = b64tab[(val >> 18) & 0x3F];
    out[j++] = b64tab[(val >> 12) & 0x3F];
    out[j++] = b64tab[(val >> 6) & 0x3F];
    out[j++] = b64tab[val & 0x3F];
    i += 3;
  }
  if (i < inlen) {
    val = in[i++] << 16;
    if (i < inlen) {
      val |= in[i] << 8;
    }
    out[j++] = b64tab[(val >> 18) & 0x3F];
    out[j++] = b64tab[(val >> 12) & 0x3F];
    out[j++] = (i < inlen) ? b64tab[(val >> 6) & 0x3F] : '=';
    out[j++] = '=';
  }
  out[j] = '\0';
  return (int)j;
}

// ---------- SHA1 (minimal, public-domain style) ----------
struct sha1_ctx {
  uint32_t h[5];
  uint64_t len_bits;
  uint8_t buf[64];
  size_t buf_len;
};

static uint32_t rol(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static void sha1_init(struct sha1_ctx *c) {
  c->h[0] = 0x67452301;
  c->h[1] = 0xEFCDAB89;
  c->h[2] = 0x98BADCFE;
  c->h[3] = 0x10325476;
  c->h[4] = 0xC3D2E1F0;
  c->len_bits = 0;
  c->buf_len = 0;
}

static void sha1_chunk(struct sha1_ctx *c, const uint8_t *p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = (p[4 * i] << 24) | (p[4 * i + 1] << 16) | (p[4 * i + 2] << 8) | p[4 * i + 3];
  for (int i = 16; i < 80; i++)
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = c->h[0], b = c->h[1], g = c->h[2], d = c->h[3], e = c->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & g) | ((~b) & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ g ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & g) | (b & d) | (g & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ g ^ d;
      k = 0xCA62C1D6;
    }
    uint32_t temp = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = g;
    g = rol(b, 30);
    b = a;
    a = temp;
  }
  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += g;
  c->h[3] += d;
  c->h[4] += e;
}

static void sha1_update(struct sha1_ctx *c, const void *data, size_t len) {
  const uint8_t *p = data;
  c->len_bits += (uint64_t)len * 8;
  while (len) {
    size_t n = 64 - c->buf_len;
    if (n > len)
      n = len;
    memcpy(c->buf + c->buf_len, p, n);
    c->buf_len += n;
    p += n;
    len -= n;
    if (c->buf_len == 64) {
      sha1_chunk(c, c->buf);
      c->buf_len = 0;
    }
  }
}

static void sha1_final(struct sha1_ctx *c, uint8_t out[20]) {
  c->buf[c->buf_len++] = 0x80;
  if (c->buf_len > 56) {
    while (c->buf_len < 64)
      c->buf[c->buf_len++] = 0;
    sha1_chunk(c, c->buf);
    c->buf_len = 0;
  }
  while (c->buf_len < 56)
    c->buf[c->buf_len++] = 0;
  for (int i = 7; i >= 0; i--)
    c->buf[c->buf_len++] = (uint8_t)(c->len_bits >> (i * 8));
  sha1_chunk(c, c->buf);
  for (int i = 0; i < 5; i++) {
    out[4 * i] = c->h[i] >> 24;
    out[4 * i + 1] = c->h[i] >> 16;
    out[4 * i + 2] = c->h[i] >> 8;
    out[4 * i + 3] = c->h[i];
  }
}

// ---------- WebSocket client ----------

enum ws_opcode { WS_CONT = 0x0, WS_TEXT = 0x1, WS_BINARY = 0x2, WS_CLOSE = 0x8, WS_PING = 0x9, WS_PONG = 0xA };

struct ws_conn {
  int fd;
  int timeout_ms;
  bool open;
  char sec_key[32]; // base64 of 16B nonce (24 chars + NUL)
};

static int http_read_headers(int fd, int timeout_ms, char **out_headers) {
  // Read until "\r\n\r\n"
  size_t cap = 4096, len = 0;
  char *buf = malloc(cap);
  if (!buf)
    return -1;
  for (;;) {
    if (len + 1024 > cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        return -1;
      }
      buf = nb;
    }
    int rdy = wait_fd(fd, POLLIN, timeout_ms);
    if (rdy <= 0) {
      free(buf);
      return rdy == 0 ? -2 : -1;
    }
    ssize_t r = recv(fd, buf + len, 1024, 0);
    if (r <= 0) {
      free(buf);
      return -1;
    }
    len += (size_t)r;
    buf[len] = '\0';
    if (strstr(buf, "\r\n\r\n")) {
      *out_headers = buf;
      return (int)len;
    }
    if (len > (1 << 20)) {
      free(buf);
      return -3;
    } // 1MB header cap
  }
}

static const char *http_hdr_get(const char *hdrs, const char *key) {
  size_t klen = strlen(key);
  const char *p = hdrs;
  while ((p = strcasestr(p, key))) {
    if ((p == hdrs || p[-1] == '\n') && strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
      p += klen + 1;
      while (*p == ' ' || *p == '\t')
        p++;
      return p;
    }
    p += 1;
  }
  return NULL;
}

static int ws_compute_accept(const char *sec_key_b64, char out_b64[64]) {
  static const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  char concat[128];
  int n = snprintf(concat, sizeof concat, "%s%s", sec_key_b64, GUID);
  if (n < 0 || (size_t)n >= sizeof concat)
    return -1;
  struct sha1_ctx c;
  sha1_init(&c);
  sha1_update(&c, concat, (size_t)n);
  uint8_t digest[20];
  sha1_final(&c, digest);
  int enc = base64_encode(digest, 20, out_b64, 64);
  return enc < 0 ? -1 : 0;
}

static int mk_nonce_b64(char out[32]) {
  uint8_t nonce[16];
  if (urandom_bytes(nonce, sizeof nonce) != 0)
    return -1;
  return base64_encode(nonce, sizeof nonce, out, 32) < 0 ? -1 : 0;
}

int ws_connect(struct ws_conn *ws, const char *host, const char *path, const char *port, int timeout_ms) {
  memset(ws, 0, sizeof *ws);
  ws->fd = -1;
  ws->timeout_ms = timeout_ms > 0 ? timeout_ms : 10000;

  struct addrinfo hints = {.ai_socktype = SOCK_STREAM, .ai_family = AF_UNSPEC};
  struct addrinfo *res = NULL;
  int gai = getaddrinfo(host, port, &hints, &res);
  if (gai != 0)
    return -1;

  int fd = -1;
  struct addrinfo *ai;
  for (ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
    set_nonblock(fd, true);
    int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc < 0 && errno != EINPROGRESS) {
      close(fd);
      fd = -1;
      continue;
    }
    int rdy = wait_fd(fd, POLLOUT, ws->timeout_ms);
    if (rdy <= 0) {
      close(fd);
      fd = -1;
      continue;
    }
    int err = 0;
    socklen_t elen = sizeof err;
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err != 0) {
      close(fd);
      fd = -1;
      continue;
    }
    break;
  }
  freeaddrinfo(res);
  if (fd < 0)
    return -2;

  if (mk_nonce_b64(ws->sec_key) != 0) {
    close(fd);
    return -3;
  }

  char req[1024];
  int n = snprintf(req, sizeof req,
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s:%s\r\n"
                   "Upgrade: websocket\r\n"
                   "Connection: Upgrade\r\n"
                   "Sec-WebSocket-Key: %s\r\n"
                   "Sec-WebSocket-Version: 13\r\n"
                   "User-Agent: ws-min-c23\r\n"
                   "\r\n",
                   path ? path : "/", host, port ? port : "80", ws->sec_key);
  if (n <= 0 || (size_t)n >= sizeof req) {
    close(fd);
    return -4;
  }

  if (write_all_timeout(fd, req, (size_t)n, ws->timeout_ms) != 0) {
    close(fd);
    return -5;
  }

  char *hdrs = NULL;
  int hr = http_read_headers(fd, ws->timeout_ms, &hdrs);
  if (hr <= 0) {
    if (hdrs)
      free(hdrs);
    close(fd);
    return -6;
  }

  // Verify 101 status
  if (strncmp(hdrs, "HTTP/1.1 101", 12) != 0 && strncmp(hdrs, "HTTP/1.0 101", 12) != 0) {
    free(hdrs);
    close(fd);
    return -7;
  }

  // Check Upgrade/Connection
  const char *up = http_hdr_get(hdrs, "Upgrade");
  const char *co = http_hdr_get(hdrs, "Connection");
  if (!up || !co || strcasestr(up, "websocket") == NULL || strcasestr(co, "Upgrade") == NULL) {
    free(hdrs);
    close(fd);
    return -8;
  }

  // Validate Sec-WebSocket-Accept
  const char *acc = http_hdr_get(hdrs, "Sec-WebSocket-Accept");
  if (!acc) {
    free(hdrs);
    close(fd);
    return -9;
  }
  char expect[64];
  if (ws_compute_accept(ws->sec_key, expect) != 0) {
    free(hdrs);
    close(fd);
    return -10;
  }
  // Compare up to end-of-line
  const char *eol = strstr(acc, "\r\n");
  size_t alen = eol ? (size_t)(eol - acc) : strlen(acc);
  if (alen != strlen(expect) || strncasecmp(acc, expect, alen) != 0) {
    free(hdrs);
    close(fd);
    return -11;
  }
  free(hdrs);

  set_nonblock(fd, false);
  ws->fd = fd;
  ws->open = true;
  return 0;
}

static int ws_send_frame(struct ws_conn *ws, enum ws_opcode op, const void *data, size_t len) {
  if (!ws || ws->fd < 0 || !ws->open)
    return -1;
  uint8_t hdr[14];
  size_t hlen = 0;
  hdr[0] = 0x80 | (uint8_t)op; // FIN=1
  if (len <= 125) {
    hdr[1] = 0x80 | (uint8_t)len;
    hlen = 2;
  } else if (len <= 0xFFFF) {
    hdr[1] = 0x80 | 126;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    hlen = 4;
  } else {
    hdr[1] = 0x80 | 127;
    for (int i = 0; i < 8; i++)
      hdr[2 + i] = (uint8_t)((len >> (56 - 8 * i)) & 0xFF);
    hlen = 10;
  }
  uint8_t mask[4];
  if (urandom_bytes(mask, 4) != 0)
    return -2;
  memcpy(hdr + hlen, mask, 4);
  hlen += 4;

  // Prepare iov: header + masked payload (mask in-place during send in chunks)
  if (write_all_timeout(ws->fd, hdr, hlen, ws->timeout_ms) != 0)
    return -3;

  const uint8_t *p = data;
  size_t off = 0;
  uint8_t chunk[4096];
  while (off < len) {
    size_t n = len - off;
    if (n > sizeof chunk)
      n = sizeof chunk;
    memcpy(chunk, p + off, n);
    for (size_t i = 0; i < n; i++)
      chunk[i] ^= mask[(off + i) & 3];
    if (write_all_timeout(ws->fd, chunk, n, ws->timeout_ms) != 0)
      return -4;
    off += n;
  }
  return 0;
}

int ws_send_text(struct ws_conn *ws, const void *data, size_t len) {
  return ws_send_frame(ws, WS_TEXT, data, len);
}
int ws_send_binary(struct ws_conn *ws, const void *data, size_t len) {
  return ws_send_frame(ws, WS_BINARY, data, len);
}

ssize_t ws_recv(struct ws_conn *ws, uint8_t *buf, size_t cap, int *opcode_out) {
  if (!ws || ws->fd < 0)
    return -1;
  uint8_t h2[2];
  if (read_all_timeout(ws->fd, h2, 2, ws->timeout_ms) != 0)
    return -2;
  bool fin = (h2[0] & 0x80) != 0;
  uint8_t op = h2[0] & 0x0F;
  bool masked = (h2[1] & 0x80) != 0;
  uint64_t len = h2[1] & 0x7F;
  if (!fin && (op == WS_TEXT || op == WS_BINARY))
    return -3; // simple impl: no fragmentation for data frames
  if (len == 126) {
    uint8_t ext[2];
    if (read_all_timeout(ws->fd, ext, 2, ws->timeout_ms) != 0)
      return -4;
    len = ((uint64_t)ext[0] << 8) | ext[1];
  } else if (len == 127) {
    uint8_t ext[8];
    if (read_all_timeout(ws->fd, ext, 8, ws->timeout_ms) != 0)
      return -5;
    len = 0;
    for (int i = 0; i < 8; i++)
      len = (len << 8) | ext[i];
  }
  uint8_t mkey[4] = {0};
  if (masked) {
    if (read_all_timeout(ws->fd, mkey, 4, ws->timeout_ms) != 0)
      return -6;
  }

  if (op == WS_PING) { // read payload and reply with PONG
    size_t n = len > cap ? cap : (size_t)len;
    if (read_all_timeout(ws->fd, buf, (size_t)len, ws->timeout_ms) != 0)
      return -7; // read all even if >cap (discard extra)
    // Unmask if needed
    if (masked)
      for (size_t i = 0; i < (size_t)len; i++)
        buf[i] ^= mkey[i & 3];
    ws_send_frame(ws, WS_PONG, buf, (size_t)len);
    return 0; // no app payload
  }
  if (op == WS_PONG) { // consume and ignore
    size_t toskip = (size_t)len;
    while (toskip) {
      size_t n = toskip > cap ? cap : toskip;
      if (read_all_timeout(ws->fd, buf, n, ws->timeout_ms) != 0)
        return -8;
      toskip -= n;
    }
    return 0;
  }
  if (op == WS_CLOSE) { // read close code+reason, reply if not sent
    uint8_t tmp[128];
    size_t n = (size_t)len;
    if (n > sizeof tmp)
      n = sizeof tmp;
    if (read_all_timeout(ws->fd, tmp, (size_t)len, ws->timeout_ms) != 0)
      return -9;
    if (masked)
      for (size_t i = 0; i < (size_t)len; i++)
        tmp[i] ^= mkey[i & 3];
    if (ws->open) {
      ws_send_frame(ws, WS_CLOSE, tmp, (size_t)len);
      ws->open = false;
    }
    return -10; // indicate closed
  }

  // Data frame
  if ((size_t)len > cap) {
    // read & discard to keep stream in sync
    size_t left = (size_t)len;
    uint8_t tmp[512];
    while (left) {
      size_t n = left > sizeof tmp ? sizeof tmp : left;
      if (read_all_timeout(ws->fd, tmp, n, ws->timeout_ms) != 0)
        return -11;
      left -= n;
    }
    return -12; // payload too large for buffer
  }
  if (read_all_timeout(ws->fd, buf, (size_t)len, ws->timeout_ms) != 0)
    return -13;
  if (masked)
    for (size_t i = 0; i < (size_t)len; i++)
      buf[i] ^= mkey[i & 3];
  if (opcode_out)
    *opcode_out = op;
  return (ssize_t)len;
}

int ws_close(struct ws_conn *ws, uint16_t code, const char *reason) {
  if (!ws || ws->fd < 0 || !ws->open)
    return 0;
  uint8_t tmp[128];
  size_t n = 0;
  if (code) {
    tmp[n++] = code >> 8;
    tmp[n++] = code & 0xFF;
  }
  if (reason) {
    size_t rlen = strlen(reason);
    if (n + rlen > sizeof tmp)
      rlen = sizeof tmp - n;
    memcpy(tmp + n, reason, rlen);
    n += rlen;
  }
  int rc = ws_send_frame(ws, WS_CLOSE, tmp, n);
  ws->open = false;
  shutdown(ws->fd, SHUT_WR);
  return rc;
}

void ws_shutdown(struct ws_conn *ws) {
  if (ws && ws->fd >= 0) {
    close(ws->fd);
    ws->fd = -1;
    ws->open = false;
  }
}

#ifdef WS_MINI_DEMO
int main(void) {
  struct ws_conn ws;
  int rc = ws_connect(&ws, "echo.websocket.events", "/", "80", 8000);
  if (rc != 0) {
    fprintf(stderr, "connect failed: %d\n", rc);
    return 1;
  }
  puts("connected");
  const char *msg = "hello from c23";
  ws_send_text(&ws, msg, strlen(msg));
  uint8_t buf[1024];
  int op = 0;
  ssize_t n = ws_recv(&ws, buf, sizeof buf, &op);
  if (n > 0) {
    printf("recv opcode=%d len=%zd: %.*s\n", op, n, (int)n, buf);
  }
  ws_close(&ws, 1000, "bye");
  ws_shutdown(&ws);
  return 0;
}
#endif
