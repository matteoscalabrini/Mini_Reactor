#pragma once

// NvsAes — Software AES-128-CBC encryption for NVS credential storage.
// (Adapted verbatim from the barebone firmware; salt domain changed per project.)
//
// Key derivation: SHA-256(eFuse base MAC || domain salt) → 16-byte key + 16-byte IV.
// Storage format: Base64( [4-byte magic] + AES-128-CBC(PKCS7-padded plaintext) ).
//
// Migration: if a stored value lacks the magic header, it is returned as plaintext
// so the caller can re-save (which encrypts it automatically).

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

namespace NvsAes {

static constexpr uint8_t kMagic[] = {0xAE, 0x5C, 0x01, 0x00};
static constexpr size_t kMagicLen = 4;
static constexpr size_t kBlockLen = 16;  // AES-128 block size

// ---------------------------------------------------------------------------
// Key derivation (from eFuse MAC + salt → 32-byte SHA-256 hash).
// First 16 bytes = AES key, next 16 bytes = CBC IV.
// ---------------------------------------------------------------------------
inline bool deriveKeyIv(uint8_t key[kBlockLen], uint8_t iv[kBlockLen]) {
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;

  const uint8_t salt[] = "NvsAes-MiniReactor-v1";
  uint8_t buf[6 + sizeof(salt) - 1];
  memcpy(buf, mac, 6);
  memcpy(buf + 6, salt, sizeof(salt) - 1);

  uint8_t hash[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  if (mbedtls_md(info, buf, sizeof(buf), hash) != 0) return false;

  memcpy(key, hash, kBlockLen);
  memcpy(iv, hash + kBlockLen, kBlockLen);
  return true;
}

// ---------------------------------------------------------------------------
// Encrypt a plaintext string.  Returns base64-encoded ciphertext with magic
// header, or the original string on failure / empty input.
// ---------------------------------------------------------------------------
inline String encrypt(const String& plaintext) {
  if (plaintext.isEmpty()) return plaintext;

  uint8_t key[kBlockLen], iv[kBlockLen], ivWork[kBlockLen];
  if (!deriveKeyIv(key, iv)) return plaintext;
  memcpy(ivWork, iv, kBlockLen);

  // PKCS7 padding
  const size_t ptLen = plaintext.length();
  const size_t padVal = kBlockLen - (ptLen % kBlockLen);
  const size_t ctLen = ptLen + padVal;

  uint8_t* padded = (uint8_t*)malloc(ctLen);
  if (!padded) return plaintext;
  memcpy(padded, plaintext.c_str(), ptLen);
  memset(padded + ptLen, (uint8_t)padVal, padVal);

  // AES-128-CBC encrypt
  uint8_t* ct = (uint8_t*)malloc(ctLen);
  if (!ct) { free(padded); return plaintext; }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, kBlockLen * 8);
  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, ctLen, ivWork,
                                  padded, ct);
  mbedtls_aes_free(&aes);
  free(padded);
  if (ret != 0) { free(ct); return plaintext; }

  // Build raw buffer: magic + ciphertext
  const size_t rawLen = kMagicLen + ctLen;
  uint8_t* raw = (uint8_t*)malloc(rawLen);
  if (!raw) { free(ct); return plaintext; }
  memcpy(raw, kMagic, kMagicLen);
  memcpy(raw + kMagicLen, ct, ctLen);
  free(ct);

  // Base64 encode
  size_t b64Len = 0;
  mbedtls_base64_encode(nullptr, 0, &b64Len, raw, rawLen);
  char* b64 = (char*)malloc(b64Len + 1);
  if (!b64) { free(raw); return plaintext; }
  mbedtls_base64_encode((uint8_t*)b64, b64Len + 1, &b64Len, raw, rawLen);
  b64[b64Len] = '\0';
  free(raw);

  String result(b64);
  free(b64);
  return result;
}

// ---------------------------------------------------------------------------
// Check whether a stored NVS string carries the encrypted magic header.
// ---------------------------------------------------------------------------
inline bool isEncrypted(const String& stored) {
  if (stored.length() < 8) return false;

  // Base64-decode and check magic
  size_t decLen = 0;
  int rc = mbedtls_base64_decode(nullptr, 0, &decLen,
                                 (const uint8_t*)stored.c_str(),
                                 stored.length());
  if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decLen < kMagicLen)
    return false;

  uint8_t* dec = (uint8_t*)malloc(decLen);
  if (!dec) return false;

  size_t actualLen = 0;
  if (mbedtls_base64_decode(dec, decLen, &actualLen,
                            (const uint8_t*)stored.c_str(),
                            stored.length()) != 0) {
    free(dec);
    return false;
  }
  bool match = (actualLen >= kMagicLen && memcmp(dec, kMagic, kMagicLen) == 0);
  free(dec);
  return match;
}

// ---------------------------------------------------------------------------
// Decrypt a base64-encoded ciphertext.  Returns plaintext on success, or the
// original string if the value is not encrypted (plaintext passthrough).
// ---------------------------------------------------------------------------
inline String decrypt(const String& stored) {
  if (stored.isEmpty() || !isEncrypted(stored)) return stored;

  uint8_t key[kBlockLen], iv[kBlockLen], ivWork[kBlockLen];
  if (!deriveKeyIv(key, iv)) return stored;
  memcpy(ivWork, iv, kBlockLen);

  // Base64 decode
  size_t decLen = 0;
  mbedtls_base64_decode(nullptr, 0, &decLen, (const uint8_t*)stored.c_str(),
                        stored.length());
  uint8_t* dec = (uint8_t*)malloc(decLen);
  if (!dec) return stored;

  size_t actualLen = 0;
  if (mbedtls_base64_decode(dec, decLen, &actualLen,
                            (const uint8_t*)stored.c_str(),
                            stored.length()) != 0) {
    free(dec);
    return stored;
  }

  const size_t ctLen = actualLen - kMagicLen;
  if (ctLen == 0 || ctLen % kBlockLen != 0) { free(dec); return stored; }

  // AES-128-CBC decrypt
  uint8_t* pt = (uint8_t*)malloc(ctLen);
  if (!pt) { free(dec); return stored; }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, kBlockLen * 8);
  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ctLen, ivWork,
                                  dec + kMagicLen, pt);
  mbedtls_aes_free(&aes);
  free(dec);
  if (ret != 0) { free(pt); return stored; }

  // Remove PKCS7 padding
  const uint8_t padVal = pt[ctLen - 1];
  if (padVal == 0 || padVal > kBlockLen) { free(pt); return stored; }
  for (size_t i = ctLen - padVal; i < ctLen; i++) {
    if (pt[i] != padVal) { free(pt); return stored; }
  }

  const size_t ptLen = ctLen - padVal;
  String result;
  result.reserve(ptLen);
  for (size_t i = 0; i < ptLen; i++) result += (char)pt[i];
  free(pt);
  return result;
}

// ---------------------------------------------------------------------------
// readCredential — Read from NVS, auto-decrypt if encrypted, return plaintext.
// ---------------------------------------------------------------------------
inline String readCredential(Preferences& prefs, const char* key,
                             const char* defaultVal) {
  String stored = prefs.getString(key, defaultVal);
  if (stored.isEmpty()) return stored;
  return decrypt(stored);
}

// ---------------------------------------------------------------------------
// writeCredential — Encrypt and write to NVS.
// ---------------------------------------------------------------------------
inline void writeCredential(Preferences& prefs, const char* key,
                            const String& plaintext) {
  if (plaintext.isEmpty()) {
    prefs.putString(key, plaintext);
    return;
  }
  prefs.putString(key, encrypt(plaintext));
}

}  // namespace NvsAes
