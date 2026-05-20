#pragma once
#include <Arduino.h>

// Encrypt a WiFi password using AES-128-CBC + base64.
// Returns the plaintext unchanged on allocation failure.
String wifiPwdEncrypt(const String &plain);

// Decrypt a base64+AES-128-CBC password produced by wifiPwdEncrypt.
// Returns the ciphertext unchanged if decoding/decryption fails.
String wifiPwdDecrypt(const String &cipher);
