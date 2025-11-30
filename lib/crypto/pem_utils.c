/**
 * @file crypto/pem_utils.c
 * @ingroup crypto
 * @brief 📄 PEM format encoding/decoding utilities for certificates and keys (adapted from BearSSL)
 *
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 * Adapted for ascii-chat by Zachary Fogg <me@zfo.gg>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "../common.h"
#include "pem_utils.h"
#include "asciichat_errno.h"

#include <bearssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== VECTOR MACROS (from BearSSL tools) ========== */

#define VECTOR(type)                                                                                                   \
  struct {                                                                                                             \
    type *buf;                                                                                                         \
    size_t ptr, len;                                                                                                   \
  }

// clang-format off
#define VEC_INIT {0, 0, 0}
// clang-format on

#define VEC_CLEAR(vec)                                                                                                 \
  do {                                                                                                                 \
    SAFE_FREE((vec).buf);                                                                                              \
    (vec).buf = NULL;                                                                                                  \
    (vec).ptr = 0;                                                                                                     \
    (vec).len = 0;                                                                                                     \
  } while (0)

#define VEC_CLEAREXT(vec, fun)                                                                                         \
  do {                                                                                                                 \
    size_t vec_tmp;                                                                                                    \
    for (vec_tmp = 0; vec_tmp < (vec).ptr; vec_tmp++) {                                                                \
      (fun)(&(vec).buf[vec_tmp]);                                                                                      \
    }                                                                                                                  \
    VEC_CLEAR(vec);                                                                                                    \
  } while (0)

#define VEC_ADD(vec, x)                                                                                                \
  do {                                                                                                                 \
    (vec).buf = vector_expand((vec).buf, sizeof *((vec).buf), &(vec).ptr, &(vec).len, 1);                              \
    (vec).buf[(vec).ptr++] = (x);                                                                                      \
  } while (0)

#define VEC_ADDMANY(vec, xp, num)                                                                                      \
  do {                                                                                                                 \
    size_t vec_num = (num);                                                                                            \
    (vec).buf = vector_expand((vec).buf, sizeof *((vec).buf), &(vec).ptr, &(vec).len, vec_num);                        \
    memcpy((vec).buf + (vec).ptr, (xp), vec_num * sizeof *((vec).buf));                                                \
    (vec).ptr += vec_num;                                                                                              \
  } while (0)

#define VEC_ELT(vec, idx) ((vec).buf[idx])
#define VEC_LEN(vec) ((vec).ptr)
#define VEC_TOARRAY(vec) xblobdup((vec).buf, sizeof *((vec).buf) * (vec).ptr)

/* ========== MEMORY UTILITIES (adapted from BearSSL xmem.c) ========== */

static void *xmalloc(size_t len) {
  void *buf;
  if (len == 0) {
    return NULL;
  }
  buf = SAFE_MALLOC(len, void *);
  return buf;
}

static void xfree(void *buf) {
  if (buf != NULL) {
    SAFE_FREE(buf);
  }
}

static void *xblobdup(const void *src, size_t len) {
  void *buf;
  if (len == 0) {
    return NULL;
  }
  buf = SAFE_MALLOC(len, void *);
  memcpy(buf, src, len);
  return buf;
}

static char *xstrdup(const char *src) {
  return (char *)xblobdup(src, strlen(src) + 1);
}

/* ========== VECTOR EXPANSION (from BearSSL vector.c) ========== */

static void *vector_expand(void *buf, size_t esize, size_t *ptr, size_t *len, size_t extra) {
  size_t nlen;
  void *nbuf;

  if (*len - *ptr >= extra) {
    return buf;
  }
  nlen = (*len << 1);
  if (nlen - *ptr < extra) {
    nlen = extra + *ptr;
    if (nlen < 8) {
      nlen = 8;
    }
  }
  nbuf = xmalloc(nlen * esize);
  if (buf != NULL) {
    memcpy(nbuf, buf, *len * esize);
    xfree(buf);
  }
  *len = nlen;
  return nbuf;
}

/* ========== DER DETECTION (from BearSSL files.c) ========== */

static int looks_like_DER(const unsigned char *buf, size_t len) {
  int fb;
  size_t dlen;

  if (len < 2) {
    return 0;
  }
  if (*buf++ != 0x30) {
    return 0;
  }
  fb = *buf++;
  len -= 2;
  if (fb < 0x80) {
    return (size_t)fb == len;
  }
  if (fb == 0x80) {
    return 0;
  }
  {
    fb -= 0x80;
    if (len < (size_t)fb + 2) {
      return 0;
    }
    len -= (size_t)fb;
    dlen = 0;
    while (fb-- > 0) {
      if (dlen > (len >> 8)) {
        return 0;
      }
      dlen = (dlen << 8) + (size_t)*buf++;
    }
    return dlen == len;
  }
}

/* ========== PEM DECODING (from BearSSL files.c) ========== */

typedef VECTOR(unsigned char) bvector;

typedef struct {
  char *name;
  unsigned char *data;
  size_t data_len;
} pem_object;

static void free_pem_object_contents(pem_object *po) {
  if (po != NULL) {
    xfree(po->name);
    xfree(po->data);
  }
}

static void vblob_append(void *cc, const void *data, size_t len) {
  bvector *bv = cc;
  VEC_ADDMANY(*bv, data, len);
}

/**
 * Decode PEM data from memory buffer
 * Returns array of pem_object, terminated by entry with NULL name
 */
static pem_object *decode_pem(const void *src, size_t len, size_t *num) {
  VECTOR(pem_object) pem_list = VEC_INIT;
  br_pem_decoder_context pc;
  pem_object po, *pos;
  const unsigned char *buf;
  bvector bv = VEC_INIT;
  int inobj;
  int extra_nl;

  *num = 0;
  br_pem_decoder_init(&pc);
  buf = src;
  inobj = 0;
  po.name = NULL;
  po.data = NULL;
  po.data_len = 0;
  extra_nl = 1;

  while (len > 0) {
    size_t tlen;

    tlen = br_pem_decoder_push(&pc, buf, len);
    buf += tlen;
    len -= tlen;

    switch (br_pem_decoder_event(&pc)) {

    case BR_PEM_BEGIN_OBJ:
      po.name = xstrdup(br_pem_decoder_name(&pc));
      br_pem_decoder_setdest(&pc, vblob_append, &bv);
      inobj = 1;
      break;

    case BR_PEM_END_OBJ:
      if (inobj) {
        po.data = VEC_TOARRAY(bv);
        po.data_len = VEC_LEN(bv);
        VEC_ADD(pem_list, po);
        VEC_CLEAR(bv);
        po.name = NULL;
        po.data = NULL;
        po.data_len = 0;
        inobj = 0;
      }
      break;

    case BR_PEM_ERROR:
      xfree(po.name);
      VEC_CLEAR(bv);
      log_error("Invalid PEM encoding");
      VEC_CLEAREXT(pem_list, free_pem_object_contents);
      return NULL;

    default:
      // Ignore other PEM events
      break;
    }

    // Add extra newline at end to support PEM files without trailing newline
    if (len == 0 && extra_nl) {
      extra_nl = 0;
      buf = (const unsigned char *)"\n";
      len = 1;
    }
  }

  if (inobj) {
    log_error("Unfinished PEM object");
    xfree(po.name);
    VEC_CLEAR(bv);
    VEC_CLEAREXT(pem_list, free_pem_object_contents);
    return NULL;
  }

  *num = VEC_LEN(pem_list);
  VEC_ADD(pem_list, po);
  pos = VEC_TOARRAY(pem_list);
  VEC_CLEAR(pem_list);
  return pos;
}

/* ========== CERTIFICATE PARSING (adapted from BearSSL files.c) ========== */

/**
 * Read certificates from memory buffer (PEM or DER format)
 * Returns array of br_x509_certificate, terminated by entry with NULL data
 */
static br_x509_certificate *read_certificates_from_memory(const unsigned char *buf, size_t len, size_t *num) {
  VECTOR(br_x509_certificate) cert_list = VEC_INIT;
  pem_object *pos;
  size_t u, num_pos;
  br_x509_certificate *xcs;
  br_x509_certificate dummy;

  *num = 0;

  // Check for DER-encoded certificate
  if (looks_like_DER(buf, len)) {
    xcs = xmalloc(2 * sizeof *xcs);
    xcs[0].data = xblobdup(buf, len);
    xcs[0].data_len = len;
    xcs[1].data = NULL;
    xcs[1].data_len = 0;
    *num = 1;
    return xcs;
  }

  // Decode PEM
  pos = decode_pem(buf, len, &num_pos);
  if (pos == NULL) {
    return NULL;
  }

  // Extract certificates
  for (u = 0; u < num_pos; u++) {
    // Windows CryptBinaryToStringA may append dashes to the name, so check for:
    // "CERTIFICATE", "CERTIFICATE-----", "X509 CERTIFICATE", "X509 CERTIFICATE-----"
    const char *name = pos[u].name;
    if (name) {
      // Strip trailing dashes for comparison
      size_t name_len = strlen(name);
      while (name_len > 0 && name[name_len - 1] == '-') {
        name_len--;
      }

      // Check if name matches CERTIFICATE or X509 CERTIFICATE (ignoring trailing dashes)
      if ((name_len == 11 && strncmp(name, "CERTIFICATE", 11) == 0) ||
          (name_len == 16 && strncmp(name, "X509 CERTIFICATE", 16) == 0)) {
        br_x509_certificate xc;
        xc.data = pos[u].data;
        xc.data_len = pos[u].data_len;
        pos[u].data = NULL; // Transfer ownership
        VEC_ADD(cert_list, xc);
      }
    }
  }

  // Free PEM objects
  for (u = 0; u < num_pos; u++) {
    free_pem_object_contents(&pos[u]);
  }
  xfree(pos);

  if (VEC_LEN(cert_list) == 0) {
    log_error("No certificates found in PEM data");
    return NULL;
  }

  *num = VEC_LEN(cert_list);
  dummy.data = NULL;
  dummy.data_len = 0;
  VEC_ADD(cert_list, dummy);
  xcs = VEC_TOARRAY(cert_list);
  VEC_CLEAR(cert_list);
  return xcs;
}

static void free_certificates(br_x509_certificate *certs, size_t num) {
  size_t u;
  for (u = 0; u < num; u++) {
    xfree((void *)certs[u].data);
  }
  xfree(certs);
}

/* ========== TRUST ANCHOR CONVERSION (from BearSSL certs.c) ========== */

static void dn_append(void *ctx, const void *buf, size_t len) {
  VEC_ADDMANY(*(bvector *)ctx, buf, len);
}

static asciichat_error_t certificate_to_trust_anchor_inner(br_x509_trust_anchor *ta, br_x509_certificate *xc) {
  br_x509_decoder_context dc;
  bvector vdn = VEC_INIT;
  br_x509_pkey *pk;

  br_x509_decoder_init(&dc, dn_append, &vdn);
  br_x509_decoder_push(&dc, xc->data, xc->data_len);
  pk = br_x509_decoder_get_pkey(&dc);
  if (pk == NULL) {
    VEC_CLEAR(vdn);
    return SET_ERRNO(ERROR_CRYPTO, "CA decoding failed with error %d", br_x509_decoder_last_error(&dc));
  }

  ta->dn.data = VEC_TOARRAY(vdn);
  ta->dn.len = VEC_LEN(vdn);
  VEC_CLEAR(vdn);
  ta->flags = 0;
  if (br_x509_decoder_isCA(&dc)) {
    ta->flags |= BR_X509_TA_CA;
  }

  switch (pk->key_type) {
  case BR_KEYTYPE_RSA:
    ta->pkey.key_type = BR_KEYTYPE_RSA;
    ta->pkey.key.rsa.n = xblobdup(pk->key.rsa.n, pk->key.rsa.nlen);
    ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
    ta->pkey.key.rsa.e = xblobdup(pk->key.rsa.e, pk->key.rsa.elen);
    ta->pkey.key.rsa.elen = pk->key.rsa.elen;
    break;
  case BR_KEYTYPE_EC:
    ta->pkey.key_type = BR_KEYTYPE_EC;
    ta->pkey.key.ec.curve = pk->key.ec.curve;
    ta->pkey.key.ec.q = xblobdup(pk->key.ec.q, pk->key.ec.qlen);
    ta->pkey.key.ec.qlen = pk->key.ec.qlen;
    break;
  default:
    xfree(ta->dn.data);
    return SET_ERRNO(ERROR_CRYPTO, "Unsupported public key type in CA certificate");
  }

  return ASCIICHAT_OK;
}

void free_ta_contents(br_x509_trust_anchor *ta) {
  if (!ta) {
    return;
  }
  xfree(ta->dn.data);
  switch (ta->pkey.key_type) {
  case BR_KEYTYPE_RSA:
    xfree((void *)ta->pkey.key.rsa.n);
    xfree((void *)ta->pkey.key.rsa.e);
    break;
  case BR_KEYTYPE_EC:
    xfree((void *)ta->pkey.key.ec.q);
    break;
  default:
    SET_ERRNO(ERROR_CRYPTO, "Unknown public key type in CA");
    break;
  }
}

/* ========== PUBLIC API ========== */

size_t read_trust_anchors_from_memory(anchor_list *dst, const unsigned char *pem_data, size_t pem_len) {
  br_x509_certificate *xcs;
  anchor_list tas = VEC_INIT;
  size_t u, num;

  xcs = read_certificates_from_memory(pem_data, pem_len, &num);
  if (xcs == NULL) {
    return 0;
  }

  for (u = 0; u < num; u++) {
    br_x509_trust_anchor ta;

    if (certificate_to_trust_anchor_inner(&ta, &xcs[u]) != ASCIICHAT_OK) {
      VEC_CLEAREXT(tas, free_ta_contents);
      free_certificates(xcs, num);
      return 0;
    }
    VEC_ADD(tas, ta);
  }

  VEC_ADDMANY(*dst, &VEC_ELT(tas, 0), num);
  VEC_CLEAR(tas);
  free_certificates(xcs, num);
  return num;
}
