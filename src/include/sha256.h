// ==========================================================================
//  sha256.h - Minimal SHA-256 for embedded systems
// ==========================================================================
#ifndef __SHA256_H__
#define __SHA256_H__

#include <stdint.h>
#include <string.h>

#define SHA256_DIGEST_SIZE  32
#define SHA256_BLOCK_SIZE   64

typedef struct {
	uint32_t state[8];
	uint64_t bitcount;
	uint8_t  buffer[SHA256_BLOCK_SIZE];
	uint32_t buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/* Convenience: compute SHA-256 in one call */
static inline void sha256(const uint8_t *data, size_t len,
                          uint8_t digest[SHA256_DIGEST_SIZE])
{
	sha256_ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, data, len);
	sha256_final(&ctx, digest);
}

#endif /* __SHA256_H__ */
