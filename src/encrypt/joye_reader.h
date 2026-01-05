#ifndef C_DECFILE_H
#define C_DECFILE_H

#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct enc_reader_ctx enc_reader_ctx;

enc_reader_ctx *enc_reader_new();

// Open a file for decryption.
// returns 1 on success, 0 on error.
int enc_reader_open(enc_reader_ctx *ctx, const char *filename, const char *password);

// Get the tags from the encrypted file.
const char* enc_reader_get_tags(enc_reader_ctx *ctx);

// Get File size
int64_t enc_reader_file_size(enc_reader_ctx *ctx);

/*
 * Seek to a specific position in the output stream.
 * Returns 0 on success, -1 on error.
 */
int enc_reader_seek(enc_reader_ctx *ctx, int64_t offset, int whence);

/*
 * Get the current position in the output stream.
 */
int64_t enc_reader_tell(enc_reader_ctx *ctx);

// Read data from the encrypted file.
// returns the number of bytes read, or -1 on error.
int enc_reader_read(enc_reader_ctx *ctx, unsigned char *buf, int buf_size);

// Close the encrypted file.
int enc_reader_close(enc_reader_ctx *ctx);

// Check if a file is a valid encrypted.
int enc_file_is_valid(const char *filename, const char *password);
#ifdef __cplusplus
}
#endif

#endif // C_DECFILE_H
