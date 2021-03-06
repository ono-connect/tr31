/**
 * @file tr31_crypto.c
 *
 * Copyright (c) 2020, 2021 ono//connect
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include "tr31_crypto.h"
#include "tr31_config.h"
#include "tr31.h"

#include <string.h>

#define TR31_KBEK_VARIANT_XOR (0x45)
#define TR31_KBAK_VARIANT_XOR (0x4D)

// see NIST SP 800-38B, section 5.3
static const uint8_t tr31_subkey_r64[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B };
static const uint8_t tr31_subkey_r128[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87 };

// see TR-31:2018, section 5.3.2.1
static const uint8_t tr31_derive_kbek_tdes2_input[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 };
static const uint8_t tr31_derive_kbek_tdes3_input[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0 };
static const uint8_t tr31_derive_kbak_tdes2_input[] = { 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80 };
static const uint8_t tr31_derive_kbak_tdes3_input[] = { 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0xC0 };

// see TR-31:2018, section 5.3.2.3
static const uint8_t tr31_derive_kbek_aes128_input[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80 };
static const uint8_t tr31_derive_kbek_aes192_input[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0xC0 };
static const uint8_t tr31_derive_kbek_aes256_input[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00 };
static const uint8_t tr31_derive_kbak_aes128_input[] = { 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x80 };
static const uint8_t tr31_derive_kbak_aes192_input[] = { 0x01, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0xC0 };
static const uint8_t tr31_derive_kbak_aes256_input[] = { 0x01, 0x00, 0x01, 0x00, 0x00, 0x04, 0x01, 0x00 };

#if defined(USE_MBEDTLS)
#include <mbedtls/des.h>
#include <mbedtls/aes.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

static int tr31_tdes_encrypt(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	int r;
	mbedtls_des3_context ctx;
	uint8_t iv_buf[DES_BLOCK_SIZE];

	// ensure that plaintext length is a multiple of the DES block length
	if ((plen & (DES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && plen != DES_BLOCK_SIZE) {
		return -2;
	}

	mbedtls_des3_init(&ctx);

	switch (key_len) {
		case TDES2_KEY_SIZE: // double length 3DES key
			r = mbedtls_des3_set2key_enc(&ctx, key);
			break;

		case TDES3_KEY_SIZE: // triple length 3DES key
			r = mbedtls_des3_set3key_enc(&ctx, key);
			break;

		default:
			r = -3;
			goto exit;
	}
	if (r) {
		r = -4;
		goto exit;
	}

	if (iv) { // IV implies CBC block mode
		memcpy(iv_buf, iv, DES_BLOCK_SIZE);
		r = mbedtls_des3_crypt_cbc(&ctx, MBEDTLS_DES_ENCRYPT, plen, iv_buf, plaintext, ciphertext);
	} else {
		r = mbedtls_des3_crypt_ecb(&ctx, plaintext, ciphertext);
	}
	if (r) {
		r = -5;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	mbedtls_des3_free(&ctx);

	return r;
}

static int tr31_tdes_decrypt(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	int r;
	mbedtls_des3_context ctx;
	uint8_t iv_buf[DES_BLOCK_SIZE];

	// ensure that ciphertext length is a multiple of the DES block length
	if ((clen & (DES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && clen != DES_BLOCK_SIZE) {
		return -2;
	}

	mbedtls_des3_init(&ctx);

	switch (key_len) {
		case TDES2_KEY_SIZE: // double length 3DES key
			r = mbedtls_des3_set2key_dec(&ctx, key);
			break;

		case TDES3_KEY_SIZE: // triple length 3DES key
			r = mbedtls_des3_set3key_dec(&ctx, key);
			break;

		default:
			r = -3;
			goto exit;
	}
	if (r) {
		r = -4;
		goto exit;
	}

	if (iv) { // IV implies CBC block mode
		memcpy(iv_buf, iv, DES_BLOCK_SIZE);
		r = mbedtls_des3_crypt_cbc(&ctx, MBEDTLS_DES_DECRYPT, clen, iv_buf, ciphertext, plaintext);
	} else {
		r = mbedtls_des3_crypt_ecb(&ctx, ciphertext, plaintext);
	}
	if (r) {
		r = -5;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	mbedtls_des3_free(&ctx);

	return r;
}

static int tr31_aes_encrypt(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	int r;
	mbedtls_aes_context ctx;
	uint8_t iv_buf[AES_BLOCK_SIZE];

	// ensure that plaintext length is a multiple of the AES block length
	if ((plen & (AES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && plen != AES_BLOCK_SIZE) {
		return -2;
	}

	if (key_len != AES128_KEY_SIZE &&
		key_len != AES192_KEY_SIZE &&
		key_len != AES256_KEY_SIZE
	) {
		return -3;
	}

	mbedtls_aes_init(&ctx);
	r = mbedtls_aes_setkey_enc(&ctx, key, key_len * 8);
	if (r) {
		r = -4;
		goto exit;
	}

	if (iv) { // IV implies CBC block mode
		memcpy(iv_buf, iv, AES_BLOCK_SIZE);
		r = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, plen, iv_buf, plaintext, ciphertext);
	} else {
		r = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, plaintext, ciphertext);
	}
	if (r) {
		r = -5;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	mbedtls_aes_free(&ctx);

	return r;
}

static int tr31_aes_decrypt(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	int r;
	mbedtls_aes_context ctx;
	uint8_t iv_buf[AES_BLOCK_SIZE];

	// ensure that ciphertext length is a multiple of the AES block length
	if ((clen & (AES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && clen != AES_BLOCK_SIZE) {
		return -2;
	}

	if (key_len != AES128_KEY_SIZE &&
		key_len != AES192_KEY_SIZE &&
		key_len != AES256_KEY_SIZE
	) {
		return -3;
	}

	mbedtls_aes_init(&ctx);
	r = mbedtls_aes_setkey_dec(&ctx, key, key_len * 8);
	if (r) {
		r = -4;
		goto exit;
	}

	if (iv) { // IV implies CBC block mode
		memcpy(iv_buf, iv, AES_BLOCK_SIZE);
		r = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, clen, iv_buf, ciphertext, plaintext);
	} else {
		r = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, ciphertext, plaintext);
	}
	if (r) {
		r = -5;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	mbedtls_aes_free(&ctx);

	return r;
}

static void tr31_rand_impl(void* buf, size_t len)
{
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
	mbedtls_ctr_drbg_random(&ctr_drbg, buf, len);
	mbedtls_ctr_drbg_free(&ctr_drbg);
}

#elif defined(USE_OPENSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>

static int tr31_tdes_encrypt(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	int r;
	EVP_CIPHER_CTX* ctx;
	int clen;
	int clen2;

	// ensure that plaintext length is a multiple of the DES block length
	if ((plen & (DES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && plen != DES_BLOCK_SIZE) {
		return -2;
	}

	ctx = EVP_CIPHER_CTX_new();

	switch (key_len) {
		case TDES2_KEY_SIZE: // double length 3DES key
			if (iv) { // IV implies CBC block mode
				r = EVP_EncryptInit_ex(ctx, EVP_des_ede_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_EncryptInit_ex(ctx, EVP_des_ede_ecb(), NULL, key, NULL);
			}
			break;

		case TDES3_KEY_SIZE: // triple length 3DES key
			if (iv) { // IV implies CBC block mode
				r = EVP_EncryptInit_ex(ctx, EVP_des_ede3_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_EncryptInit_ex(ctx, EVP_des_ede3_ecb(), NULL, key, NULL);
			}
			break;

		default:
			r = -3;
			goto exit;
	}
	if (!r) {
		r = -4;
		goto exit;
	}

	// disable padding
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	clen = 0;
	r = EVP_EncryptUpdate(ctx, ciphertext, &clen, plaintext, plen);
	if (!r) {
		r = -5;
		goto exit;
	}

	clen2 = 0;
	r = EVP_EncryptFinal_ex(ctx, ciphertext + clen, &clen2);
	if (!r) {
		r = -6;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	EVP_CIPHER_CTX_free(ctx);
	return r;
}

static int tr31_tdes_decrypt(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	int r;
	EVP_CIPHER_CTX* ctx;
	int plen;
	int plen2;

	// ensure that ciphertext length is a multiple of the DES block length
	if ((clen & (DES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && clen != DES_BLOCK_SIZE) {
		return -2;
	}

	ctx = EVP_CIPHER_CTX_new();

	switch (key_len) {
		case TDES2_KEY_SIZE: // double length 3DES key
			if (iv) { // IV implies CBC block mode
				r = EVP_DecryptInit_ex(ctx, EVP_des_ede_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_DecryptInit_ex(ctx, EVP_des_ede_ecb(), NULL, key, NULL);
			}
			break;

		case TDES3_KEY_SIZE: // triple length 3DES key
			if (iv) { // IV implies CBC block mode
				r = EVP_DecryptInit_ex(ctx, EVP_des_ede3_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_DecryptInit_ex(ctx, EVP_des_ede3_ecb(), NULL, key, NULL);
			}
			break;

		default:
			r = -3;
			goto exit;
	}
	if (!r) {
		r = -4;
		goto exit;
	}

	// disable padding
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	plen = 0;
	r = EVP_DecryptUpdate(ctx, plaintext, &plen, ciphertext, clen);
	if (!r) {
		r = -5;
		goto exit;
	}

	plen2 = 0;
	r = EVP_DecryptFinal_ex(ctx, plaintext + plen, &plen2);
	if (!r) {
		r = -6;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	EVP_CIPHER_CTX_free(ctx);
	return r;
}

static int tr31_aes_encrypt(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	int r;
	EVP_CIPHER_CTX* ctx;
	int clen;
	int clen2;

	// ensure that plaintext length is a multiple of the AES block length
	if ((plen & (AES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && plen != AES_BLOCK_SIZE) {
		return -2;
	}

	ctx = EVP_CIPHER_CTX_new();

	switch (key_len) {
		case AES128_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
			}
			break;

		case AES192_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_192_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_192_ecb(), NULL, key, NULL);
			}
			break;

		case AES256_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL);
			}
			break;

		default:
			r = -3;
			goto exit;
	}
	if (!r) {
		r = -4;
		goto exit;
	}

	// disable padding
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	clen = 0;
	r = EVP_EncryptUpdate(ctx, ciphertext, &clen, plaintext, plen);
	if (!r) {
		r = -5;
		goto exit;
	}

	clen2 = 0;
	r = EVP_EncryptFinal_ex(ctx, ciphertext + clen, &clen2);
	if (!r) {
		r = -6;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	EVP_CIPHER_CTX_free(ctx);
	return r;
}

static int tr31_aes_decrypt(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	int r;
	EVP_CIPHER_CTX* ctx;
	int plen;
	int plen2;

	// ensure that ciphertext length is a multiple of the AES block length
	if ((clen & (AES_BLOCK_SIZE-1)) != 0) {
		return -1;
	}

	// only allow a single block for ECB block mode
	if (!iv && clen != AES_BLOCK_SIZE) {
		return -2;
	}

	ctx = EVP_CIPHER_CTX_new();

	switch (key_len) {
		case AES128_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
			}
			break;

		case AES192_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_192_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_192_ecb(), NULL, key, NULL);
			}
			break;

		case AES256_KEY_SIZE:
			if (iv) { // IV implies CBC block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
			} else { // no IV implies ECB block mode
				r = EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, key, NULL);
			}
			break;

		default:
			r = -3;
			goto exit;
	}
	if (!r) {
		r = -4;
		goto exit;
	}

	// disable padding
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	plen = 0;
	r = EVP_DecryptUpdate(ctx, plaintext, &plen, ciphertext, clen);
	if (!r) {
		r = -5;
		goto exit;
	}

	plen2 = 0;
	r = EVP_DecryptFinal_ex(ctx, plaintext + plen, &plen2);
	if (!r) {
		r = -6;
		goto exit;
	}

	r = 0;
	goto exit;

exit:
	EVP_CIPHER_CTX_free(ctx);
	return r;
}

static void tr31_rand_impl(void* buf, size_t len)
{
	RAND_bytes(buf, len);
}

#endif

static int tr31_memcmp(const void* a, const void* b, size_t n)
{
	int r = 0;
	const uint8_t* ptr_a = a;
	const uint8_t* ptr_b = b;

	while (n) {
		r |= *ptr_a ^ *ptr_b;
		++ptr_a;
		++ptr_b;
		--n;
	}

	// not-not is to sanitise result
	return !!r;
}

int tr31_tdes_encrypt_ecb(const void* key, size_t key_len, const void* plaintext, void* ciphertext)
{
	return tr31_tdes_encrypt(key, key_len, NULL, plaintext, DES_BLOCK_SIZE, ciphertext);
}

int tr31_tdes_decrypt_ecb(const void* key, size_t key_len, const void* ciphertext, void* plaintext)
{
	return tr31_tdes_decrypt(key, key_len, NULL, ciphertext, DES_BLOCK_SIZE, plaintext);
}

int tr31_tdes_encrypt_cbc(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	return tr31_tdes_encrypt(key, key_len, iv, plaintext, plen, ciphertext);
}

int tr31_tdes_decrypt_cbc(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	return tr31_tdes_decrypt(key, key_len, iv, ciphertext, clen, plaintext);
}

int tr31_tdes_cbcmac(const void* key, size_t key_len, const void* buf, size_t len, void* mac)
{
	int r;
	uint8_t iv[DES_BLOCK_SIZE];
	const void* ptr = buf;

	// see ISO 9797-1:2011 MAC algorithm 1

	// compute CBC-MAC
	memset(iv, 0, sizeof(iv)); // start with zero IV
	for (size_t i = 0; i < len; i += DES_BLOCK_SIZE) {
		r = tr31_tdes_encrypt_cbc(key, key_len, iv, ptr, DES_BLOCK_SIZE, iv);
		if (r) {
			// internal error
			return r;
		}

		ptr += DES_BLOCK_SIZE;
	}

	// copy MAC output
	memcpy(mac, iv, DES_MAC_SIZE);

	return 0;
}

int tr31_tdes_verify_cbcmac(const void* key, size_t key_len, const void* buf, size_t len, const void* mac_verify)
{
	int r;
	uint8_t mac[DES_MAC_SIZE];

	r = tr31_tdes_cbcmac(key, key_len, buf, len, mac);
	if (r) {
		return r;
	}

	return tr31_memcmp(mac, mac_verify, sizeof(mac));
}

static int tr31_lshift(uint8_t* x, size_t len)
{
	uint8_t lsb;
	uint8_t msb;

	x += (len - 1);
	lsb = 0x00;
	while (len--) {
		msb = *x & 0x80;
		*x <<= 1;
		*x |= lsb;
		--x;
		lsb = msb >> 7;
	}

	// return carry bit
	return lsb;
}

static void tr31_xor(uint8_t* x, const uint8_t* y, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		*x ^= *y;
		++x;
		++y;
	}
}

static int tr31_tdes_derive_subkeys(const void* key, size_t key_len, void* k1, void* k2)
{
	int r;
	uint8_t zero[DES_BLOCK_SIZE];
	uint8_t l_buf[DES_BLOCK_SIZE];

	// see NIST SP 800-38B, section 6.1

	// encrypt zero block with input key
	memset(zero, 0, sizeof(zero));
	r = tr31_tdes_encrypt_ecb(key, key_len, zero, l_buf);
	if (r) {
		// internal error
		return r;
	}

	// generate K1 subkey
	memcpy(k1, l_buf, DES_BLOCK_SIZE);
	r = tr31_lshift(k1, DES_BLOCK_SIZE);
	// if carry bit is set, XOR with R64
	if (r) {
		tr31_xor(k1, tr31_subkey_r64, sizeof(tr31_subkey_r64));
	}

	// generate K2 subkey
	memcpy(k2, k1, DES_BLOCK_SIZE);
	r = tr31_lshift(k2, DES_BLOCK_SIZE);
	// if carry bit is set, XOR with R64
	if (r) {
		tr31_xor(k2, tr31_subkey_r64, sizeof(tr31_subkey_r64));
	}

	// cleanup
	tr31_cleanse(l_buf, sizeof(l_buf));

	return 0;
}

int tr31_tdes_cmac(const void* key, size_t key_len, const void* buf, size_t len, void* cmac)
{
	int r;
	uint8_t k1[DES_BLOCK_SIZE];
	uint8_t k2[DES_BLOCK_SIZE];
	uint8_t iv[DES_BLOCK_SIZE];
	const void* ptr = buf;

	size_t last_block_len;
	uint8_t last_block[DES_BLOCK_SIZE];

	if (!key || !buf || !cmac) {
		return -1;
	}
	if (key_len != TDES2_KEY_SIZE && key_len != TDES3_KEY_SIZE) {
		return -2;
	}

	// See NIST SP 800-38B, section 6.2
	// See ISO 9797-1:2011 MAC algorithm 5
	// If CMAC message input (M) is a multiple of the cipher block size, then
	// the last message input block is XOR'd with subkey K1.
	// If CMAC message input (M) is not a multiple of the cipher block size,
	// then the last message input block is padded and XOR'd with subkey K2.
	// The cipher is applied in CBC mode to all message input blocks,
	// including the modified last block.

	// derive CMAC subkeys
	r = tr31_tdes_derive_subkeys(key, key_len, k1, k2);
	if (r) {
		// internal error
		return r;
	}

	// compute CMAC
	// see NIST SP 800-38B, section 6.2
	// see ISO 9797-1:2011 MAC algorithm 5
	memset(iv, 0, sizeof(iv)); // start with zero IV
	if (len > DES_BLOCK_SIZE) {
		// for all blocks except the last block
		for (size_t i = 0; i < len - DES_BLOCK_SIZE; i += DES_BLOCK_SIZE) {
			r = tr31_tdes_encrypt_cbc(key, key_len, iv, ptr, DES_BLOCK_SIZE, iv);
			if (r) {
				// internal error
				return r;
			}

			ptr += DES_BLOCK_SIZE;
		}
	}

	// prepare last block
	last_block_len = len - (ptr - buf);
	if (last_block_len == DES_BLOCK_SIZE) {
		// if message input is a multple of cipher block size,
		// use subkey K1
		tr31_xor(iv, k1, sizeof(iv));
	} else {
		// if message input is a multple of cipher block size,
		// use subkey K2
		tr31_xor(iv, k2, sizeof(iv));

		// build new last block
		memcpy(last_block, ptr, last_block_len);

		// pad last block with 1 bit followed by zeros
		last_block[last_block_len] = 0x80;
		if (last_block_len + 1 < DES_BLOCK_SIZE) {
			memset(last_block + last_block_len + 1, 0, DES_BLOCK_SIZE - last_block_len - 1);
		}

		ptr = last_block;
	}

	// process last block
	r = tr31_tdes_encrypt_cbc(key, key_len, iv, ptr, DES_BLOCK_SIZE, cmac);
	if (r) {
		// internal error
		return r;
	}

	// cleanup
	tr31_cleanse(k1, sizeof(k1));
	tr31_cleanse(k2, sizeof(k2));
	tr31_cleanse(iv, sizeof(iv));
	tr31_cleanse(last_block, sizeof(last_block));

	return 0;
}

int tr31_tdes_verify_cmac(const void* key, size_t key_len, const void* buf, size_t len, const void* cmac_verify)
{
	int r;
	uint8_t cmac[DES_BLOCK_SIZE];

	r = tr31_tdes_cmac(key, key_len, buf, len, cmac);
	if (r) {
		return r;
	}

	return tr31_memcmp(cmac, cmac_verify, sizeof(cmac));
}

int tr31_tdes_kbpk_variant(const void* kbpk, size_t kbpk_len, void* kbek, void* kbak)
{
	const uint8_t* kbpk_buf = kbpk;
	uint8_t* kbek_buf = kbek;
	uint8_t* kbak_buf = kbak;

	if (!kbpk || !kbek || !kbak) {
		return -1;
	}
	if (kbpk_len != TDES2_KEY_SIZE && kbpk_len != TDES3_KEY_SIZE) {
		return TR31_ERROR_UNSUPPORTED_KBPK_LENGTH;
	}

	for (size_t i = 0; i < kbpk_len; ++i) {
		kbek_buf[i] = kbpk_buf[i] ^ TR31_KBEK_VARIANT_XOR;
		kbak_buf[i] = kbpk_buf[i] ^ TR31_KBAK_VARIANT_XOR;
	}

	return 0;
}

int tr31_tdes_kbpk_derive(const void* kbpk, size_t kbpk_len, void* kbek, void* kbak)
{
	int r;
	uint8_t kbxk_input[8];

	if (!kbpk || !kbek || !kbak) {
		return -1;
	}
	if (kbpk_len != TDES2_KEY_SIZE && kbpk_len != TDES3_KEY_SIZE) {
		return TR31_ERROR_UNSUPPORTED_KBPK_LENGTH;
	}

	// See TR-31:2018, section 5.3.2.1
	// CMAC uses subkey and message input to output derived key material of
	// cipher block length.
	// Message input is as described in TR-31:2018, section 5.3.2.1, table 1

	// populate key block encryption key derivation input
	switch (kbpk_len) {
		case TDES2_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbek_tdes2_input, sizeof(tr31_derive_kbek_tdes2_input));
			break;

		case TDES3_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbek_tdes3_input, sizeof(tr31_derive_kbek_tdes3_input));
			break;

		default:
			return -2;
	}

	// derive key block encryption key
	for (size_t kbek_len = 0; kbek_len < kbpk_len; kbek_len += DES_BLOCK_SIZE) {
		// TDES CMAC creates key material of size DES_BLOCK_SIZE
		r = tr31_tdes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), kbek + kbek_len);
		if (r) {
			// internal error
			return r;
		}

		// increment key derivation input counter
		kbxk_input[0]++;
	}

	// populate key block authentication key derivation input
	switch (kbpk_len) {
		case TDES2_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbak_tdes2_input, sizeof(tr31_derive_kbak_tdes2_input));
			break;

		case TDES3_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbak_tdes3_input, sizeof(tr31_derive_kbak_tdes3_input));
			break;

		default:
			return -3;
	}

	// derive key block authentication key
	for (size_t kbak_len = 0; kbak_len < kbpk_len; kbak_len += DES_BLOCK_SIZE) {
		// TDES CMAC creates key material of size DES_BLOCK_SIZE
		r = tr31_tdes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), kbak + kbak_len);
		if (r) {
			// internal error
			return r;
		}

		// increment key derivation input counter
		kbxk_input[0]++;
	}

	return 0;
}

int tr31_tdes_kcv(const void* key, size_t key_len, void* kcv)
{
	int r;
	uint8_t zero[DES_BLOCK_SIZE];
	uint8_t ciphertext[DES_BLOCK_SIZE];

	if (!key || !kcv) {
		return -1;
	}
	if (key_len != TDES2_KEY_SIZE && key_len != TDES3_KEY_SIZE) {
		return -2;
	}

	// see ANSI X9.24-1:2017, A.2 Legacy Approach

	// zero KCV in case of error
	memset(kcv, 0, TDES_KCV_SIZE);

	// encrypt zero block with input key
	memset(zero, 0, sizeof(zero));
	r = tr31_tdes_encrypt_ecb(key, key_len, zero, ciphertext);
	if (r) {
		// internal error
		return r;
	}

	// KCV is always first 3 bytes of ciphertext
	memcpy(kcv, ciphertext, TDES_KCV_SIZE);

	tr31_cleanse(ciphertext, sizeof(ciphertext));

	return 0;
}

int tr31_aes_encrypt_ecb(const void* key, size_t key_len, const void* plaintext, void* ciphertext)
{
	return tr31_aes_encrypt(key, key_len, NULL, plaintext, AES_BLOCK_SIZE, ciphertext);
}

int tr31_aes_decrypt_ecb(const void* key, size_t key_len, const void* ciphertext, void* plaintext)
{
	return tr31_aes_decrypt(key, key_len, NULL, ciphertext, AES_BLOCK_SIZE, plaintext);
}

int tr31_aes_encrypt_cbc(const void* key, size_t key_len, const void* iv, const void* plaintext, size_t plen, void* ciphertext)
{
	return tr31_aes_encrypt(key, key_len, iv, plaintext, plen, ciphertext);
}

int tr31_aes_decrypt_cbc(const void* key, size_t key_len, const void* iv, const void* ciphertext, size_t clen, void* plaintext)
{
	return tr31_aes_decrypt(key, key_len, iv, ciphertext, clen, plaintext);
}

static int tr31_aes_derive_subkeys(const void* key, size_t key_len, void* k1, void* k2)
{
	int r;
	uint8_t zero[AES_BLOCK_SIZE];
	uint8_t l_buf[AES_BLOCK_SIZE];

	// see NIST SP 800-38B, section 6.1

	// encrypt zero block with input key
	memset(zero, 0, sizeof(zero));
	r = tr31_aes_encrypt_ecb(key, key_len, zero, l_buf);
	if (r) {
		// internal error
		return r;
	}

	// generate K1 subkey
	memcpy(k1, l_buf, AES_BLOCK_SIZE);
	r = tr31_lshift(k1, AES_BLOCK_SIZE);
	// if carry bit is set, XOR with R128
	if (r) {
		tr31_xor(k1, tr31_subkey_r128, sizeof(tr31_subkey_r128));
	}

	// generate K2 subkey
	memcpy(k2, k1, AES_BLOCK_SIZE);
	r = tr31_lshift(k2, AES_BLOCK_SIZE);
	// if carry bit is set, XOR with R128
	if (r) {
		tr31_xor(k2, tr31_subkey_r128, sizeof(tr31_subkey_r128));
	}

	// cleanup
	tr31_cleanse(l_buf, sizeof(l_buf));

	return 0;
}

int tr31_aes_cmac(const void* key, size_t key_len, const void* buf, size_t len, void* cmac)
{
	int r;
	uint8_t k1[AES_BLOCK_SIZE];
	uint8_t k2[AES_BLOCK_SIZE];
	uint8_t iv[AES_BLOCK_SIZE];
	const void* ptr = buf;

	size_t last_block_len;
	uint8_t last_block[AES_BLOCK_SIZE];

	if (!key || !buf || !cmac) {
		return -1;
	}
	if (key_len != AES128_KEY_SIZE &&
		key_len != AES192_KEY_SIZE &&
		key_len != AES256_KEY_SIZE
	) {
		return -2;
	}

	// See NIST SP 800-38B, section 6.2
	// See ISO 9797-1:2011 MAC algorithm 5
	// If CMAC message input (M) is a multiple of the cipher block size, then
	// the last message input block is XOR'd with subkey K1.
	// If CMAC message input (M) is not a multiple of the cipher block size,
	// then the last message input block is padded and XOR'd with subkey K2.
	// The cipher is applied in CBC mode to all message input blocks,
	// including the modified last block.

	// derive CMAC subkeys
	r = tr31_aes_derive_subkeys(key, key_len, k1, k2);
	if (r) {
		// internal error
		return r;
	}

	// compute CMAC
	// see NIST SP 800-38B, section 6.2
	// see ISO 9797-1:2011 MAC algorithm 5
	memset(iv, 0, sizeof(iv)); // start with zero IV
	if (len > AES_BLOCK_SIZE) {
		// for all blocks except the last block
		for (size_t i = 0; i < len - AES_BLOCK_SIZE; i += AES_BLOCK_SIZE) {
			r = tr31_aes_encrypt_cbc(key, key_len, iv, ptr, AES_BLOCK_SIZE, iv);
			if (r) {
				// internal error
				return r;
			}

			ptr += AES_BLOCK_SIZE;
		}
	}

	// prepare last block
	last_block_len = len - (ptr - buf);
	if (last_block_len == AES_BLOCK_SIZE) {
		// if message input is a multple of cipher block size,
		// use subkey K1
		tr31_xor(iv, k1, sizeof(iv));
	} else {
		// if message input is a multple of cipher block size,
		// use subkey K2
		tr31_xor(iv, k2, sizeof(iv));

		// build new last block
		memcpy(last_block, ptr, last_block_len);

		// pad last block with 1 bit followed by zeros
		last_block[last_block_len] = 0x80;
		if (last_block_len + 1 < AES_BLOCK_SIZE) {
			memset(last_block + last_block_len + 1, 0, AES_BLOCK_SIZE - last_block_len - 1);
		}

		ptr = last_block;
	}

	// process last block
	r = tr31_aes_encrypt_cbc(key, key_len, iv, ptr, AES_BLOCK_SIZE, cmac);
	if (r) {
		// internal error
		return r;
	}

	return 0;
}

int tr31_aes_verify_cmac(const void* key, size_t key_len, const void* buf, size_t len, const void* cmac_verify)
{
	int r;
	uint8_t cmac[AES_BLOCK_SIZE];

	r = tr31_aes_cmac(key, key_len, buf, len, cmac);
	if (r) {
		return r;
	}

	return tr31_memcmp(cmac, cmac_verify, sizeof(cmac));
}

int tr31_aes_kbpk_derive(const void* kbpk, size_t kbpk_len, void* kbek, void* kbak)
{
	int r;
	uint8_t kbxk_input[8];

	if (!kbpk || !kbek || !kbak) {
		return -1;
	}
	if (kbpk_len != AES128_KEY_SIZE &&
		kbpk_len != AES192_KEY_SIZE &&
		kbpk_len != AES256_KEY_SIZE
	) {
		return TR31_ERROR_UNSUPPORTED_KBPK_LENGTH;
	}

	// See TR-31:2018, section 5.3.2.3
	// CMAC uses subkey and message input to output derived key material of
	// cipher block length.
	// Message input is as described in TR-31:2018, section 5.3.2.3, table 2

	// populate key block encryption key derivation input
	memset(kbxk_input, 0, sizeof(kbxk_input));
	switch (kbpk_len) {
		case AES128_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbek_aes128_input, sizeof(tr31_derive_kbek_aes128_input));
			break;

		case AES192_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbek_aes192_input, sizeof(tr31_derive_kbek_aes192_input));
			break;

		case AES256_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbek_aes256_input, sizeof(tr31_derive_kbek_aes256_input));
			break;

		default:
			return -2;
	}

	// derive key block encryption key
	for (size_t kbek_len = 0; kbek_len < kbpk_len; kbek_len += AES_BLOCK_SIZE) {
		// AES CMAC creates key material of size AES_BLOCK_SIZE

		// see TR-31:2018, section 5.3.2.3
		// for AES-192 key derivation, use the leftmost 8 bytes of the
		// second CMAC block
		if (kbpk_len - kbek_len < AES_BLOCK_SIZE) {
			uint8_t cmac[AES_BLOCK_SIZE];

			r = tr31_aes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), cmac);
			if (r) {
				// internal error
				return r;
			}

			memcpy(kbek + kbek_len, cmac, kbpk_len - kbek_len);
			tr31_cleanse(cmac, sizeof(cmac));
		} else {
			r = tr31_aes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), kbek + kbek_len);
			if (r) {
				// internal error
				return r;
			}
		}

		// increment key derivation input counter
		kbxk_input[0]++;
	}

	// populate key block authentication key derivation input
	switch (kbpk_len) {
		case AES128_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbak_aes128_input, sizeof(tr31_derive_kbak_aes128_input));
			break;

		case AES192_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbak_aes192_input, sizeof(tr31_derive_kbak_aes192_input));
			break;

		case AES256_KEY_SIZE:
			memcpy(kbxk_input, tr31_derive_kbak_aes256_input, sizeof(tr31_derive_kbak_aes256_input));
			break;

		default:
			return -3;
	}

	// derive key block authentication key
	for (size_t kbak_len = 0; kbak_len < kbpk_len; kbak_len += AES_BLOCK_SIZE) {
		// AES CMAC creates key material of size AES_BLOCK_SIZE

		// see TR-31:2018, section 5.3.2.3
		// for AES-192 key derivation, use the leftmost 8 bytes of the
		// second CMAC block
		if (kbpk_len - kbak_len < AES_BLOCK_SIZE) {
			uint8_t cmac[AES_BLOCK_SIZE];

			r = tr31_aes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), cmac);
			if (r) {
				// internal error
				return r;
			}

			memcpy(kbak + kbak_len, cmac, kbpk_len - kbak_len);
			tr31_cleanse(cmac, sizeof(cmac));
		} else {
			r = tr31_aes_cmac(kbpk, kbpk_len, kbxk_input, sizeof(kbxk_input), kbak + kbak_len);
			if (r) {
				// internal error
				return r;
			}
		}

		// increment key derivation input counter
		kbxk_input[0]++;
	}

	return 0;
}

int tr31_aes_kcv(const void* key, size_t key_len, void* kcv)
{
	int r;
	uint8_t input[AES_BLOCK_SIZE];
	uint8_t ciphertext[AES_BLOCK_SIZE];

	if (!key || !kcv) {
		return -1;
	}
	if (key_len != AES128_KEY_SIZE &&
		key_len != AES192_KEY_SIZE &&
		key_len != AES256_KEY_SIZE
	) {
		return -2;
	}

	// see ANSI X9.24-1:2017, A.3 CMAC-based Check values

	// zero KCV in case of error
	memset(kcv, 0, AES_KCV_SIZE);

	// use input block populated with 0x00
	memset(input, 0x00, sizeof(input));

	// Compute CMAC of input block using input key
	r = tr31_aes_cmac(key, key_len, input, sizeof(input), ciphertext);
	if (r) {
		// internal error
		return r;
	}

	// KCV is always first 5 bytes of ciphertext
	memcpy(kcv, ciphertext, AES_KCV_SIZE);

	tr31_cleanse(ciphertext, sizeof(ciphertext));

	return 0;
}

void tr31_cleanse(void* buf, size_t len)
{
	memset(buf, 0, len);

	// From GCC documentation:
	// If the function does not have side effects, there are optimizations
	// other than inlining that cause function calls to be optimized away,
	// although the function call is live. To keep such calls from being
	// optimized away, put...
	__asm__ ("");
}

void tr31_rand(void* buf, size_t len)
{
	tr31_rand_impl(buf, len);
}
