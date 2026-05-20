#include "wifi_crypto.h"
#include "mbedtls/aes.h"
#include <stdlib.h>
#include <string.h>

// Default key — replaced at build time via -DWIFI_ENC_KEY in platformio.ini.
// In CI, the sed step substitutes this value with the WIFI_ENC_KEY GitHub secret.
#ifndef WIFI_ENC_KEY
#define WIFI_ENC_KEY "LauncherWifi2025"
#endif

// Fixed IV — distinct from key so CBC mode has a proper non-zero IV.
static const uint8_t AES_IV[16] = {
    0x4C, 0x61, 0x75, 0x6E, 0x63, 0x68, 0x65, 0x72,  // "Launcher"
    0x57, 0x69, 0x66, 0x69, 0x4B, 0x65, 0x79, 0x21    // "WifiKey!"
};

// Build a 16-byte AES key from WIFI_ENC_KEY (cyclic pad/truncate).
static void buildKey(uint8_t key[16]) {
    const char *raw = WIFI_ENC_KEY;
    size_t len = strlen(raw);
    if (len == 0) len = 1;
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)raw[i % len];
}

// ── Base64 ────────────────────────────────────────────────────────────────────

static const char B64C[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String b64Encode(const uint8_t *data, size_t len) {
    String out;
    out.reserve(((len + 2) / 3) * 4 + 1);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)data[i] << 16) |
                     (i + 1 < len ? (uint32_t)data[i + 1] << 8 : 0) |
                     (i + 2 < len ? (uint32_t)data[i + 2] : 0);
        out += B64C[(n >> 18) & 0x3F];
        out += B64C[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? B64C[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? B64C[n & 0x3F] : '=';
    }
    return out;
}

static int b64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64Decode(const String &str, uint8_t *out, size_t maxLen) {
    size_t outLen = 0;
    size_t slen = str.length();
    for (size_t i = 0; i + 3 < slen && outLen < maxLen; i += 4) {
        int v0 = b64Val(str[i]);
        int v1 = b64Val(str[i + 1]);
        int v2 = str[i + 2] == '=' ? 0 : b64Val(str[i + 2]);
        int v3 = str[i + 3] == '=' ? 0 : b64Val(str[i + 3]);
        if (v0 < 0 || v1 < 0) break;
        if (outLen < maxLen) out[outLen++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (str[i + 2] != '=' && outLen < maxLen) out[outLen++] = (uint8_t)((v1 << 4) | (v2 >> 2));
        if (str[i + 3] != '=' && outLen < maxLen) out[outLen++] = (uint8_t)((v2 << 6) | v3);
    }
    return outLen;
}

// ── Public API ────────────────────────────────────────────────────────────────

String wifiPwdEncrypt(const String &plain) {
    if (plain.isEmpty()) return plain;

    uint8_t key[16];
    buildKey(key);

    // PKCS7 pad to a multiple of 16
    size_t plen = plain.length();
    uint8_t pad = 16 - (uint8_t)(plen % 16);
    size_t total = plen + pad;

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return plain;

    memcpy(buf, plain.c_str(), plen);
    memset(buf + plen, pad, pad);

    uint8_t iv[16];
    memcpy(iv, AES_IV, 16);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, total, iv, buf, buf);
    mbedtls_aes_free(&ctx);

    String result = b64Encode(buf, total);
    free(buf);
    return result;
}

String wifiPwdDecrypt(const String &cipher) {
    if (cipher.isEmpty()) return cipher;

    size_t maxBin = (cipher.length() / 4) * 3 + 4;
    uint8_t *buf = (uint8_t *)malloc(maxBin);
    if (!buf) return cipher;

    size_t binLen = b64Decode(cipher, buf, maxBin);
    if (binLen == 0 || binLen % 16 != 0) {
        free(buf);
        return cipher;
    }

    uint8_t key[16];
    buildKey(key);

    uint8_t iv[16];
    memcpy(iv, AES_IV, 16);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, binLen, iv, buf, buf);
    mbedtls_aes_free(&ctx);

    // Validate and strip PKCS7 padding
    uint8_t padByte = buf[binLen - 1];
    if (padByte == 0 || padByte > 16 || padByte > binLen) {
        free(buf);
        return cipher;
    }
    String result((char *)buf, binLen - padByte);
    free(buf);
    return result;
}
