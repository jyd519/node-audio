#include "joye_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#if defined(__linux__)
#define _fseeki64 fseeko64
#define _ftelli64 ftello64
#endif

/* --- Constants replicated from Go implementation --- */
#define FILE_SIG "JOYE"
#define ENC_VERSION 1
#define IV_SIZE 16
#define AUTHTAG_SIZE 32
#define PBKDF2_ITERS 2145
#define KEY_SIZE 32
#define BUF_SIZE (16 * 1024)
#define BLOCK_SIZE 16

struct enc_writer_ctx {
  FILE *out;
  uint64_t total;
  long auth_pos;       /* position of size+auth_tag placeholder */
  uint8_t base_iv[16]; // Base IV from header
  unsigned char iv[IV_SIZE];
  unsigned char key[KEY_SIZE];
  long data_pos;
  unsigned char buf[BUF_SIZE];
  /* Crypto context */
  EVP_CIPHER_CTX *cipher_ctx;
#if OPENSSL_API_LEVEL >= 30000
  EVP_MAC_CTX *hmac_ctx;
  EVP_MAC *mac;
#else
  HMAC_CTX *hmac_ctx;
#endif
  int delay_hmac;
  int flushed;
  uint64_t current_position;
  int error; /* last error (<0) */
};

int enc_ctx_repos(enc_writer_ctx *w, uint64_t pos);

static int write_u16_le(FILE *f, uint16_t v) {
  unsigned char b[2];
  b[0] = (unsigned char)(v & 0xFF);
  b[1] = (unsigned char)((v >> 8) & 0xFF);
  return fwrite(b, 1, 2, f) != 2;
}

static int write_u64_le(FILE *f, uint64_t v) {
  unsigned char b[8];
  for (int i = 0; i < 8; ++i) {
    b[i] = (unsigned char)((v >> (i * 8)) & 0xFF);
  }
  return fwrite(b, 1, 8, f) != 8;
}

static int set_error(enc_writer_ctx *w, int err) {
  if (w && !w->error) {
    w->error = err;
  }
  return err;
}

enc_writer_ctx *enc_writer_new(FILE *out, const char *password, const unsigned char *tags,
                               uint16_t tags_len) {
  /* Reserve space for size (8) + auth tag (32) */
  unsigned char zero[8 + AUTHTAG_SIZE] = {0};
#if OPENSSL_API_LEVEL >= 30000
  OSSL_PARAM params[2];
  char alg_sha256[] = "SHA256";
#endif
  if (!out || !password) {
    return NULL;
  }

  enc_writer_ctx *w = (enc_writer_ctx *)calloc(1, sizeof(enc_writer_ctx));
  if (!w) {
    return NULL;
  }
  w->delay_hmac = 1;
  w->out = out;
  w->error = 0;

  /* Header: FILE_SIG + version */
  if (fwrite(FILE_SIG, 1, 4, out) != 4 || fputc(ENC_VERSION, out) == EOF) {
    set_error(w, -1);
    goto fail;
  }

  /* Random IV */
  if (RAND_bytes(w->iv, IV_SIZE) != 1) {
    set_error(w, -2);
    goto fail;
  }
  memcpy(w->base_iv, w->iv, IV_SIZE);
  if (fwrite(w->iv, 1, IV_SIZE, out) != IV_SIZE) {
    set_error(w, -3);
    goto fail;
  }

  /* Reserve space for size (8) + auth tag (32) */
  w->auth_pos = (long)_ftelli64(out);
  if (fwrite(zero, 1, sizeof(zero), out) != sizeof(zero)) {
    set_error(w, -4);
    goto fail;
  }

  /* Write tags */
  if (write_u16_le(out, tags_len)) {
    set_error(w, -5);
    goto fail;
  }

  if (tags_len && fwrite(tags, 1, tags_len, out) != tags_len) {
    set_error(w, -6);
    goto fail;
  }

  // offset of the real data
  w->data_pos = (long)_ftelli64(out);

  /* Derive key from password */
  if (!PKCS5_PBKDF2_HMAC(password, (int)strlen(password), w->iv, IV_SIZE, PBKDF2_ITERS,
                         EVP_sha256(), KEY_SIZE, w->key)) {
    set_error(w, -7);
    goto fail;
  }

  /* Init cipher (AES-256-CTR) */
  w->cipher_ctx = EVP_CIPHER_CTX_new();
  if (!w->cipher_ctx) {
    set_error(w, -8);
    goto fail;
  }
  if (EVP_EncryptInit_ex(w->cipher_ctx, EVP_aes_256_ctr(), NULL, w->key, w->iv) != 1) {
    set_error(w, -9);
    goto fail;
  }

  /* Init HMAC */
#if OPENSSL_API_LEVEL >= 30000
  w->mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
  if (!w->mac) {
    set_error(w, -10);
    goto fail;
  }
  w->hmac_ctx = EVP_MAC_CTX_new(w->mac);
  if (!w->hmac_ctx) {
    set_error(w, -10);
    goto fail;
  }
  params[0] = OSSL_PARAM_construct_utf8_string("digest", alg_sha256, 0);
  params[1] = OSSL_PARAM_construct_end();
  if (EVP_MAC_init(w->hmac_ctx, w->key, KEY_SIZE, params) != 1) {
    set_error(w, -11);
    goto fail;
  }
#else
  w->hmac_ctx = HMAC_CTX_new();
  if (!w->hmac_ctx) {
    set_error(w, -10);
    goto fail;
  }
  if (HMAC_Init_ex(w->hmac_ctx, w->key, KEY_SIZE, EVP_sha256(), NULL) != 1) {
    set_error(w, -11);
    goto fail;
  }
#endif

#if OPENSSL_API_LEVEL >= 30000
  /* Seed HMAC with tags */
  if (tags_len && EVP_MAC_update(w->hmac_ctx, tags, tags_len) != 1) {
    set_error(w, -12);
    goto fail;
  }
#else
  /* Seed HMAC with tags */
  if (tags_len && HMAC_Update(w->hmac_ctx, tags, tags_len) != 1) {
    set_error(w, -12);
    goto fail;
  }
#endif

  return w;
fail:
  enc_writer_close(w);
  return NULL;
}

size_t enc_writer_write(enc_writer_ctx *w, const void *data, size_t len) {
  if (!w || w->error) {
    return 0;
  }

  if (len == 0)
    return 0;

  const unsigned char *in = (const unsigned char *)data;
  while (len) {
    int sz = len > BUF_SIZE ? BUF_SIZE : len;

    int outlen = 0;
    if (EVP_EncryptUpdate(w->cipher_ctx, w->buf, &outlen, in, sz) != 1) {
      set_error(w, -21);
      return 0;
    }

    if (outlen != sz) {
      set_error(w, -21);
      return 0;
    }

    size_t written = fwrite(w->buf, 1, outlen, w->out);
    if (written != (size_t)sz) {
      set_error(w, -22);
      return 0;
    }

    if (!w->delay_hmac) {
      /* Update HMAC with ciphertext */
#if OPENSSL_API_LEVEL >= 30000
      if (EVP_MAC_update(w->hmac_ctx, w->buf, outlen) != 1) {
        return set_error(w, -23);
      }
#else
      if (HMAC_Update(w->hmac_ctx, w->buf, outlen) != 1) {
        return set_error(w, -23);
      }
#endif
    }

    in += sz;
    len -= sz;
  }

  w->current_position += (in-(const unsigned char *)data);
  if (w->current_position > w->total) {
    w->total = w->current_position;
  }

  return in-(const unsigned char *)data;
}

static int finalize_hmac(enc_writer_ctx *w, unsigned char *out) {
#if OPENSSL_API_LEVEL >= 30000
  size_t len = 0;
  if (EVP_MAC_final(w->hmac_ctx, out, &len, AUTHTAG_SIZE) != 1) {
    return set_error(w, -30);
  }
#else
  unsigned int len = 0;
  if (HMAC_Final(w->hmac_ctx, out, &len) != 1) {
    return set_error(w, -30);
  }
#endif
  if (len != AUTHTAG_SIZE) {
    return set_error(w, -31);
  }
  return 0;
}

int recalc_hmac(enc_writer_ctx *w, unsigned char *out) {
  if (!w->delay_hmac) {
    return set_error(w, -23);
  }

  fflush(w->out);

  if (_fseeki64(w->out, w->data_pos, SEEK_SET) != 0) {
    return set_error(w, -32);
  }

  int outlen = 0;
  for (;;) {
    outlen = fread(w->buf, 1, BUF_SIZE, w->out);
    if (outlen == 0) {
      break;
    }
#if OPENSSL_API_LEVEL >= 30000
    if (EVP_MAC_update(w->hmac_ctx, w->buf, outlen) != 1) {
      return set_error(w, -23);
    }
#else
    if (HMAC_Update(w->hmac_ctx, w->buf, outlen) != 1) {
      return set_error(w, -23);
    }
#endif
  }

  return finalize_hmac(w, out);
}

int64_t enc_writer_tell(enc_writer_ctx *w) { return w->current_position; }

int enc_writer_seek(enc_writer_ctx *w, int64_t offset, int whence) {
  if (!w || w->error) {
    return w ? w->error : -1;
  }
  if (!w->delay_hmac) {
    return set_error(w, -99);
  }

  int64_t new_position;
  switch (whence) {
  case SEEK_SET:
    new_position = offset;
    break;
  case SEEK_CUR:
    new_position = w->current_position + offset;
    break;
  case SEEK_END:
    new_position = w->total + offset;
    break;
  default:
    return set_error(w, -99);
  }

  if (new_position < 0) {
    return set_error(w, -99);
  }

  if (_fseeki64(w->out, w->data_pos + new_position, SEEK_SET) != 0) {
    return set_error(w, -99);
  }
  w->current_position = new_position;
  return enc_ctx_repos(w, new_position);
}

int enc_ctx_repos(enc_writer_ctx *w, uint64_t pos) {
  uint64_t block_counter = pos / BLOCK_SIZE;

  // Calculate the counter value for this position
  memcpy(w->iv, w->base_iv, 16);

  // Add block counter to IV (big-endian)
  uint64_t counter = block_counter;
  int carry = 0;
  for (int i = 15; i >= 8; --i) {
    int add = w->iv[i] + (counter & 0xFF) + carry;
    w->iv[i] = add & 0xFF;
    carry = add >> 8;
    counter >>= 8;
  }

  // Reinitialize cipher with new counter
  if (EVP_EncryptInit_ex(w->cipher_ctx, NULL, NULL, NULL, w->iv) != 1) {
    return 0;
  }

  // If we're not at a block boundary, we need to advance the cipher
  uint64_t block_offset = pos % BLOCK_SIZE;
  if (block_offset > 0) {
    uint8_t dummy_input[16] = {0};
    uint8_t dummy_output[16];
    int outlen;
    // Process partial block to advance cipher state
    EVP_EncryptUpdate(w->cipher_ctx, dummy_output, &outlen, dummy_input, block_offset);
  }

  return 1;
}

int enc_writer_flush(enc_writer_ctx *w) {
  if (!w || w->flushed) {
    return w ? w->error : -1;
  }

  /* No buffered data left in EVP for CTR mode, but call Final anyway */
  unsigned char dummy[16];
  int dummylen = 0;
  EVP_EncryptFinal_ex(w->cipher_ctx, dummy, &dummylen);

  unsigned char tag[AUTHTAG_SIZE];

  if (w->delay_hmac) {
    if (recalc_hmac(w, tag) != 0) {
      return 0;
    }
  } else {
    if (finalize_hmac(w, tag) != 0) {
      return w->error;
    }
  }

  /* Seek back to reserved position and write size + tag */
  if (_fseeki64(w->out, w->auth_pos, SEEK_SET) != 0) {
    return set_error(w, -32);
  }

  if (write_u64_le(w->out, w->total)) {
    return set_error(w, -33);
  }
  if (fwrite(tag, 1, AUTHTAG_SIZE, w->out) != AUTHTAG_SIZE) {
    return set_error(w, -34);
  }

  /* Flush to disk */
  fflush(w->out);

  w->flushed = 1;
  return 0;
}

int enc_writer_close(enc_writer_ctx *w) {
  if (!w)
    return -1;

  if (!w->flushed) {
    enc_writer_flush(w);
  }

  if (w->cipher_ctx)
    EVP_CIPHER_CTX_free(w->cipher_ctx);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  if (w->hmac_ctx)
    HMAC_CTX_free(w->hmac_ctx);
#else
  if (w->hmac_ctx)
    EVP_MAC_CTX_free(w->hmac_ctx);
  if (w->mac)
    EVP_MAC_free(w->mac);
#endif

  int err = w->error;
  free(w);
  return err;
}

int enc_writer_error(const enc_writer_ctx *w) { return w ? w->error : -1; }
