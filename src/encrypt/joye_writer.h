#ifndef C_ENC_WRITER_H
#define C_ENC_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * enc_wwwwww_ctxw_ctxw_ctxw_ctxw_ctxw_ctxw_ctxw_ctxw_ctxw_ctxr_ctxi_ctxt_ctxe_ctxr_ctx
 * ---------
 * A drop-in C implementation of the Go encfile.Writer.
 * It produces the exact same binary layout so the resulting
 * file can be consumed by the Go encfile.Reader without any
 * modification.
 *
 * Header layout (little endian):
 *   'JOYE' (4) | version (1) | iv (16) |
 *   size (8)   | hmac (32)   | tags_len (2) | tags | ciphertext …
 */

typedef struct enc_writer_ctx enc_writer_ctx;

/*
 * Create a new writer object bound to an already opened FILE pointer.
 * "out" MUST be opened in binary update mode ("wb+" or "w+b") so that
 * the function can perform seeking when finalising the stream.
 *
 * tags  – optional metadata attached to the file (may be NULL)
 * tags_len – length of the tags buffer (0 if no tags)
 *
 * Returns NULL on allocation/initialisation failure.
 */
enc_writer_ctx *enc_writer_new(FILE *out,
                          const char *password,
                          const unsigned char *tags,
                          uint16_t tags_len);

/*
 * Encrypt and write arbitrary bytes.
 * Returns the number of bytes successfully processed. If this value is
 * smaller than len an error occurred and enc_writer_error() can be used
 * to obtain the last OpenSSL/stdio error.
 */
size_t enc_writer_write(enc_writer_ctx *w, const void *data, size_t len);

/*
 * Seek to a specific position in the output stream.
 * Returns 0 on success, -1 on error.
 */
int enc_writer_seek(enc_writer_ctx *w, int64_t offset, int whence);

/*
 * Get the current position in the output stream.
 * Returns the current position in the output stream.
 */
int64_t enc_writer_tell(enc_writer_ctx *w);

/*
 * Flush the stream and write the size and HMAC into the reserved area.
/* Finalise the stream – writes size and HMAC into the reserved area. */
int enc_writer_flush(enc_writer_ctx *w);

/* Flush, free all resources and close the underlying FILE* (optional). */
int enc_writer_close(enc_writer_ctx *w);

/* Retrieve the last error code (<0) or 0 on success. */
int enc_writer_error(const enc_writer_ctx *w);

#ifdef __cplusplus
}
#endif

#endif /* C_ENC_WRITER_H */
