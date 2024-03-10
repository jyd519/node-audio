#include "decfile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#define SIG_LENGTH 4
#define IV_LENGTH 16
#define LENGTH_LENGTH 8
#define AUTH_LENGTH 32

/*
 * Format of The Encrypted File:
 *   sig(4) + version(1) + iv(16) + length(8) + auth(32) + (tags: 2+...) + data
 */
#define FILE_SIG "JOYE"
#define FILE_VER 1

typedef struct dec_file_ctx {
  EVP_CIPHER_CTX *dec_ctx;
  FILE *file;
  unsigned char key[100];
  unsigned char iv[16];
  char auth[32];
  int64_t size;
  char *comment;
  int valid;
} dec_file_ctx;

static int read_buffer_n(FILE *file, void *buf, int buf_size);
static int generate_key(const char *password, const unsigned char *salt,
                        int saltLen, unsigned char *key, int keyLen);

dec_file_ctx *dec_file_new() {
  dec_file_ctx *ctx = (dec_file_ctx *)malloc(sizeof(dec_file_ctx));
  memset(ctx, 0, sizeof(dec_file_ctx));
  ctx->valid = 1;
  return ctx;
}

int dec_file_close(dec_file_ctx *ctx) {
  if (ctx->dec_ctx) {
    EVP_CIPHER_CTX_free(ctx->dec_ctx);
    ctx->dec_ctx = NULL;
  }

  if (ctx->file) {
    fclose(ctx->file);
  }
  if (ctx->comment) {
    free(ctx->comment);
  }
  ctx->valid = 0;
  return 0;
}

static int dec_file_open(dec_file_ctx *ctx, const char *filename, const char *password) {
  char sig[SIG_LENGTH];
  uint8_t version = 0;

#ifdef _WIN32
  FILE *file = NULL;
  fopen_s(&file, filename, "rb");
#else
  FILE *file = fopen(filename, "rb");
#endif
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

  if (!(ctx->dec_ctx = EVP_CIPHER_CTX_new())) {
    return 0;
  }

  if (1 != EVP_DecryptInit_ex(ctx->dec_ctx, EVP_aes_256_ctr(), NULL, ctx->key,
                              ctx->iv)) {
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

int generate_key(const char *password, const unsigned char *salt, int saltLen,
                 unsigned char *key, int keyLen) {
  const EVP_MD *digest = EVP_sha256();
  if (PKCS5_PBKDF2_HMAC(password, strlen(password), salt, saltLen, 2145, digest,
                        keyLen, key) != 1) {
    return 0;
  }
  return 1;
}

int dec_file_read(dec_file_ctx *ctx, unsigned char *buf, int buf_size) {
  int readSize = fread(buf, 1, buf_size, ctx->file);
  if (readSize == 0) {
    return 0;
  }
  if (readSize < 0) {
    return readSize;
  }

  int len;
  if (1 != EVP_DecryptUpdate(ctx->dec_ctx, buf, &len, buf, readSize)) {
    EVP_CIPHER_CTX_free(ctx->dec_ctx);
    return 0;
  }
  return 1;
}
