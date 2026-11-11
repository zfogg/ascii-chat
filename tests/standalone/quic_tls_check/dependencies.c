#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>

int main(void) {
  if (!ngtcp2_version(0) || !nghttp3_version(0))
    return 1;
  ngtcp2_crypto_ossl_ctx *ctx = NULL;
  if (ngtcp2_crypto_ossl_ctx_new(&ctx, NULL) != 0 || !ctx)
    return 2;
  ngtcp2_crypto_ossl_ctx_del(ctx);
  return 0;
}
