#include "joye_reader.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

#include "htobe64.h"

#if defined(__linux__)
#define _fseeki64 fseeko64
#define _ftelli64 ftello64
#endif

#define SIG_LENGTH 4
#define IV_LENGTH 16
#define LENGTH_LENGTH 8
#define AUTH_LENGTH 32
#define KEY_SIZE 32
#define BLOCK_SIZE 16

/*
 * Format of The Encrypted File:
 *   sig(4) + version(1) + iv(16) + length(8) + auth(32) + (tags: 2+...) + data
 */
#define FILE_SIG "JOYE"
#define FILE_VER 1

typedef struct enc_reader_ctx {
  EVP_CIPHER_CTX *dec_ctx;
  FILE *file;
  uint8_t key[100];
  uint8_t iv[16];
  uint8_t base_iv[16]; // Base IV from header
  char auth[32];
  int64_t size;
  int data_pos;
  int64_t current_pos;
  char *tags;
  int valid;
} enc_reader_ctx;

static int read_buffer_n(FILE *file, void *buf, int buf_size);
static int generate_key(const char *password, const unsigned char *salt, int saltLen,
                        unsigned char *key, int keyLen);

static int enc_reader_repos(enc_reader_ctx *ctx, uint64_t pos);

enc_reader_ctx *enc_reader_new() {
  enc_reader_ctx *ctx = (enc_reader_ctx *)malloc(sizeof(enc_reader_ctx));
  memset(ctx, 0, sizeof(enc_reader_ctx));
  ctx->valid = 1;
  return ctx;
}

int64_t enc_reader_file_size(enc_reader_ctx *ctx) { return ctx->size; }

const char *enc_reader_get_tags(enc_reader_ctx *ctx) { return ctx->tags; }

int enc_reader_close(enc_reader_ctx *ctx) {
  if (ctx->dec_ctx) {
    EVP_CIPHER_CTX_free(ctx->dec_ctx);
    ctx->dec_ctx = NULL;
  }

  if (ctx->file) {
    fclose(ctx->file);
  }
  if (ctx->tags) {
    free(ctx->tags);
  }
  ctx->valid = 0;
  return 0;
}

int enc_reader_open(enc_reader_ctx *ctx, const char *filename, const char *password) {
  char sig[SIG_LENGTH];
  uint8_t version = 0;

#ifdef _WIN32
  FILE *file = NULL;
  fopen_s(&file, filename, "rb");
#else
  FILE *file = fopen(filename, "rb");
#endif
  if (!file) {
    ctx->valid = 0;
    return 0;
  }

  ctx->file = file;
  if (!read_buffer_n(ctx->file, sig, SIG_LENGTH)) {
    ctx->valid = 0;
    return 0;
  }
  if (memcmp(sig, FILE_SIG, SIG_LENGTH) != 0) {
    ctx->valid = 0;
    return 0;
  }
  read_buffer_n(ctx->file, &version, 1);
  if (version != FILE_VER) {
    ctx->valid = 0;
    return 0;
  }

  if (!read_buffer_n(ctx->file, ctx->iv, IV_LENGTH)) {
    ctx->valid = 0;
    return 0;
  }

  memcpy(ctx->base_iv, ctx->iv, IV_LENGTH);

  static_assert(sizeof(int64_t) == 8, "int64_t should be 8 bytes");

  if (!read_buffer_n(ctx->file, &ctx->size, 8)) {
    ctx->valid = 0;
    return 0;
  }

  if (!read_buffer_n(ctx->file, ctx->auth, AUTH_LENGTH)) {
    ctx->valid = 0;
    return 0;
  }

  if (!generate_key(password, ctx->iv, IV_LENGTH, ctx->key, 32)) {
    ctx->valid = 0;
    return 0;
  }

  // read tags
  uint16_t tags_len = 0;
  if (!read_buffer_n(ctx->file, &tags_len, 2)) {
    ctx->valid = 0;
    return 0;
  }
  if (tags_len > 0) {
    ctx->tags = (char *)malloc(tags_len + 1);
    if (!ctx->tags) {
      ctx->valid = 0;
      return 0;
    }

    ctx->tags[tags_len] = '\0';
    if (!read_buffer_n(ctx->file, ctx->tags, tags_len)) {
      ctx->valid = 0;
      return 0;
    }
  }

  ctx->data_pos = (int)_ftelli64(ctx->file);
  if (ctx->size == 0) { // recover file size
    int rc = _fseeki64(ctx->file, 0, SEEK_END);
    if (rc != 0) {
      ctx->valid = 0;
      return 0;  
    }
    ctx->size = _ftelli64(ctx->file) - ctx->data_pos;
    rc = _fseeki64(ctx->file, ctx->data_pos, SEEK_SET);
    if (rc != 0) {
      ctx->valid = 0;
      return 0;
    }
  }

  if (!(ctx->dec_ctx = EVP_CIPHER_CTX_new())) {
    return 0;
  }

  if (1 != EVP_DecryptInit_ex(ctx->dec_ctx, EVP_aes_256_ctr(), NULL, ctx->key, ctx->iv)) {
    EVP_CIPHER_CTX_free(ctx->dec_ctx);
    ctx->valid = 0;
    return 0;
  }

  return 1;
}

int read_buffer_n(FILE *file, void *buf, int buf_size) {
  int readSize = fread(buf, 1, buf_size, file);
  return readSize == buf_size ? 1 : 0;
}

int generate_key(const char *password, const unsigned char *salt, int saltLen, unsigned char *key,
                 int keyLen) {
  const EVP_MD *digest = EVP_sha256();
  if (PKCS5_PBKDF2_HMAC(password, strlen(password), salt, saltLen, 2145, digest, keyLen, key) !=
      1) {
    return 0;
  }
  return 1;
}

int enc_reader_read(enc_reader_ctx *ctx, unsigned char *buf, int buf_size) {
  int readSize = fread(buf, 1, buf_size, ctx->file);
  if (readSize == 0) {
    return 0;
  }

  int len;
  if (1 != EVP_DecryptUpdate(ctx->dec_ctx, buf, &len, buf, readSize)) {
    EVP_CIPHER_CTX_free(ctx->dec_ctx);
    return -1;
  }

  assert(readSize == len);
  ctx->current_pos += len;
  return len;
}

int enc_reader_seek(enc_reader_ctx *ctx, int64_t offset, int whence) {
  int64_t new_position;
  switch (whence) {
  case SEEK_SET:
    new_position = offset;
    break;
  case SEEK_CUR:
    new_position = ctx->current_pos + offset;
    break;
  case SEEK_END:
    new_position = ctx->size + offset;
    break;
  default:
    return 0;
  }

  if (new_position < 0) {
    return 0;
  }

  uint64_t file_offset = ctx->data_pos + new_position;
  if (_fseeki64(ctx->file, file_offset, SEEK_SET) != 0) {
    return -1;
  }
  ctx->current_pos = new_position;
  return enc_reader_repos(ctx, new_position);
}

int64_t enc_reader_tell(enc_reader_ctx *ctx) {
  return ctx->current_pos;
}

static int enc_reader_repos(enc_reader_ctx *ctx, uint64_t pos) {
  uint64_t block_counter = pos / BLOCK_SIZE;

  // Calculate the counter value for this position
  memcpy(ctx->iv, ctx->base_iv, 16);

  // Add block counter to IV (big-endian)
  uint64_t counter = block_counter;
  int carry = 0;
  for (int i = 15; i >= 8; --i) {
      int add = ctx->iv[i] + (counter & 0xFF) + carry;
      ctx->iv[i] = add & 0xFF;
      carry = add >> 8;
      counter >>= 8;
  }

  // Reinitialize cipher with new counter
  if (EVP_DecryptInit_ex(ctx->dec_ctx, NULL, NULL, NULL, ctx->iv) != 1) {
    return -1;
  }

  // If we're not at a block boundary, we need to advance the cipher
  uint64_t block_offset = pos % BLOCK_SIZE;
  if (block_offset > 0) {
    uint8_t dummy_input[16] = {0};
    uint8_t dummy_output[16];
    int outlen;
    // Process partial block to advance cipher state
    EVP_DecryptUpdate(ctx->dec_ctx, dummy_output, &outlen, dummy_input, block_offset);
  }

  return 1;
}

// Check if the file is valid
int enc_file_is_valid(const char *filename, const char *password) {
  EVP_MAC *mac = NULL;
  OSSL_PARAM params[3];
  int rc = 0;
  unsigned char auth_tag[AUTH_LENGTH] = {0};
  size_t auth_len;
  const int buf_sz = 16 * 1024;
  unsigned char *buf = NULL;
  int read = 0;
  enc_reader_ctx *ctx = enc_reader_new();
  char alg_sha256[] = "SHA256";

  if (!enc_reader_open(ctx, filename, password)) {
    enc_reader_close(ctx);
    return 0;
  }

  /* Init HMAC */
  mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
  if (!mac) {
    enc_reader_close(ctx);
    return 0;
  }

  EVP_MAC_CTX *hmac_ctx =  EVP_MAC_CTX_new(mac);
  if (!hmac_ctx) {
    goto fail;
  }


  params[0] = OSSL_PARAM_construct_utf8_string("digest", alg_sha256, strlen(alg_sha256));
  params[1] = OSSL_PARAM_construct_end();
  if (EVP_MAC_init(hmac_ctx, ctx->key, KEY_SIZE, params) != 1) {
    goto fail;
  }

  if (ctx->tags) {
    int tags_len = strlen(ctx->tags);
    EVP_MAC_update(hmac_ctx, (unsigned char *)ctx->tags, tags_len);
  }


  buf = malloc(buf_sz);
  if (!buf) {
    goto fail;
  }

  for (;;) {
    int readSize = fread(buf, 1, buf_sz, ctx->file);
    if (readSize < 0) {
      goto fail;
    }
    if (readSize == 0) {
      break;
    }
    if (EVP_MAC_update(hmac_ctx, buf, readSize) != 1) {
      goto fail;
    }
  }

  EVP_MAC_final(hmac_ctx, auth_tag, &auth_len, AUTH_LENGTH);
  rc = strncmp((const char*)auth_tag, ctx->auth, AUTH_LENGTH) == 0 ? 1 : 0;
fail:
  if (buf) {
    free(buf);
  }
  if (hmac_ctx) {
    EVP_MAC_CTX_free(hmac_ctx);
  }
  if (mac) {
    EVP_MAC_free(mac);
  }
  enc_reader_close(ctx);
  return rc;
}
