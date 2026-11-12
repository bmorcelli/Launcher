#pragma once
#include <Arduino.h>

// Encrypt a WiFi password using AES-128-CBC + base64.
// Returns the plaintext unchanged on allocation failure.
String wifiPwdEncrypt(const String &plain);

// Decrypt a base64+AES-128-CBC password produced by wifiPwdEncrypt.
// Returns the ciphertext unchanged if decoding/decryption fails.
String wifiPwdDecrypt(const String &cipher);

// Decrypt a stored password and report whether it was actually readable with
// this build's key. Returns false when `cipher` is a ciphertext from another
// build (config.conf shared across devices, or NVS written with a different
// WIFI_ENC_KEY); in that case the caller must not re-encrypt it as if it were
// the plaintext password. Also unwraps values that older builds encrypted more
// than once after a failed decrypt.
bool wifiPwdDecryptStored(const String &cipher, String &plain);
