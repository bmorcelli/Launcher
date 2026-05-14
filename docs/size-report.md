# Relatorio de tamanho e dependencias

## Milestone 0 - baseline

Data: 2026-05-14

Ambiente validado: `m5stack-cardputer`

Comandos executados:

```powershell
pio run -e m5stack-cardputer -t clean
pio run -e m5stack-cardputer
```

Observacao: o build terminou com sucesso, mas o PowerShell retornou exit code `1` porque o warning abaixo saiu em stderr depois do sucesso:

```text
lto-wrapper.exe: warning: using serial compilation of 7 LTRANS jobs
```

Log completo salvo em `docs/milestone0-m5stack-cardputer-build.log`.

### Tamanhos

| Artefato | Tamanho |
| --- | ---: |
| `.pio/build/m5stack-cardputer/firmware.bin` | 1.276.656 bytes |
| `.pio/build/m5stack-cardputer/firmware.elf` | 22.042.072 bytes |
| `Launcher-m5stack-cardputer.bin` | 1.342.192 bytes |

### Uso de memoria reportado pelo linker

```text
iram0_0_seg:         74 KB     358144 B     21.16%
iram0_2_seg:      987392 B    8388576 B     11.77%
dram0_0_seg:      130032 B     341760 B     38.05%
drom0_0_seg:     1263835 B   33554400 B      3.77%
extern_ram_seg:  1245152 B   33554400 B      3.71%
```

Resumo PlatformIO:

```text
RAM:   [==        ]  21.6% (used 70636 bytes from 327680 bytes)
Flash: [==        ]  24.7% (used 1276247 bytes from 5177344 bytes)
Launcher: [=================   ] 88.5% (used 0x137AF0 bytes of 0x160000 of test partition)
```

### Dependencias ativas no baseline

Dependencias relevantes encontradas no grafo do build:

| Dependencia | Versao reportada | Motivo atual |
| --- | --- | --- |
| `HTTPClient` | 3.3.2 | OTA online e downloads HTTP/HTTPS em `src/onlineLauncher.*` |
| `WiFi` | 3.3.2 | conexao STA/AP, scan e IP em `src/onlineLauncher.*`, `src/webInterface.*`, `src/main.cpp` |
| `M5-HTTPUpdate` | 2.0.0 | instalacao OTA online via `httpUpdate` |
| `CustomUpdate` | 2.0.0 | `Update`/constantes de update usadas por SD, WebUI e partitioner |
| `AsyncTCP` | 3.4.10 | base TCP do ESPAsyncWebServer |
| `ESPAsyncWebServer` | 3.6.0 | WebUI atual |
| `ESPmDNS` | 3.3.2 | `launcher.local` |
| `NetworkClientSecure` | 3.3.2 | HTTPS via `WiFiClientSecure` |
| `Arduino_GFX` | 1.6.5 | display; fora do escopo de remocao neste momento |
| `Wire` | 3.3.2 | display/perifericos; fora do escopo de remocao neste momento |
| `SPI` | 3.3.2 | display/SD; fora do escopo de remocao neste momento |

Bibliotecas confirmadas como compiladas no baseline:

```text
HTTPClient
Custom_Update
M5Stack-HTTPUpdate
AsyncTCP
ESPAsyncWebServer
ESPmDNS
```

### Arquivos impactados pelo mapa de dependencias

Principais arquivos do app:

- `src/webInterface.h`: inclui `AsyncTCP.h`, `ESPAsyncWebServer.h`, `WiFi.h`.
- `src/webInterface.cpp`: usa `AsyncWebServer`, `AsyncWebServerRequest`, `AsyncWebServerResponse`, `MDNS`, `WiFi`, `Update`.
- `src/onlineLauncher.h`: inclui `HTTPClient.h`, `M5-HTTPUpdate.h`, `WiFi.h`, `WiFiClientSecure.h`.
- `src/onlineLauncher.cpp`: usa `WiFi`, `WiFiClientSecure`, `HTTPClient`, `httpUpdate`.
- `src/sd_functions.h`: inclui `CustomUpdate.h`.
- `src/sd_functions.cpp`: usa `Update.begin`, `Update.write`, `Update.end`, `Update.getError`.
- `src/partitioner.cpp`: usa `Update.begin` e `Update.write`.
- `src/main.cpp`: inclui `HTTPClient.h`, `WiFi.h`; usa `WiFi` no fluxo headless.
- `include/globals.h`, `include/interface.h`, `include/VectorDisplay.h`, `src/mykeyboard.h`, `src/tft.h`: incluem `Arduino.h`.

Arquivos de board com uso direto relevante:

- `boards/m5stack-tab5/interface.cpp`: `WiFi.h`, `WiFi.setPins`, `WiFi.scanNetworks`.
- `boards/m5stack-paper-s3/interface.cpp`: `WiFi.h`.
- `boards/lilygo-t-hmi/interface.cpp`, `boards/phantom/interface.cpp`, `boards/waveshare-esp32-s3-lcd-147/interface.cpp`: `Arduino.h`.

Configuracao de build relevante:

- `platformio.ini`: `framework = arduino`, `lib_extra_dirs = lib_modules`.
- `platformio.ini`: flags `CONFIG_ASYNC_TCP_RUNNING_CORE` e `CONFIG_ASYNC_TCP_USE_WDT` ainda existem e devem sair apos remover AsyncTCP.
- `boards/m5stack-cardputer/platformio.ini`: usa `${env.lib_deps}` e `lib_ignore` especifico do ambiente.
- `boards/m5stack-tab5/platformio.ini` e `boards/m5stack-paper-s3/platformio.ini`: tambem fixam `framework = arduino`.

### Conclusao do Milestone 0

Baseline registrado para `m5stack-cardputer`.

Proximo milestone recomendado: criar `src/idf/idf_update.*` e substituir `CustomUpdate`/`Update` nos fluxos SD, WebUI e partitioner antes de mexer em HTTP/WebUI.

## Milestone 1 - camada nativa de update/flash

Data: 2026-05-14

Ambiente validado: `m5stack-cardputer`

Comandos executados:

```powershell
pio run -e m5stack-cardputer -t clean
pio run -e m5stack-cardputer
```

Log completo salvo em `docs/milestone1-m5stack-cardputer-build.log`.

### Alteracoes

- Criada a camada `src/idf/idf_update.h` e `src/idf/idf_update.cpp`.
- Removido o include direto de `CustomUpdate.h` em `src/sd_functions.h`.
- `src/sd_functions.cpp` agora usa `launcherUpdateBegin`, `launcherUpdateWrite` e `launcherUpdateEnd` para app/SPIFFS vindos do SD.
- `src/webInterface.cpp` agora usa a camada IDF para OTA via upload WebUI.
- `src/partitioner.cpp` agora usa a camada IDF para restore de SPIFFS.
- FAT continua usando a rotina direta existente com `esp_flash_erase_region`/`esp_flash_write`.

### Tamanhos

| Artefato | Baseline | Milestone 1 | Delta |
| --- | ---: | ---: | ---: |
| `.pio/build/m5stack-cardputer/firmware.bin` | 1.276.656 bytes | 1.277.008 bytes | +352 bytes |
| `Launcher-m5stack-cardputer.bin` | 1.342.192 bytes | 1.342.544 bytes | +352 bytes |

Resumo PlatformIO:

```text
RAM:   [==        ]  21.6% (used 70684 bytes from 327680 bytes)
Flash: [==        ]  24.7% (used 1276603 bytes from 5177344 bytes)
```

### Dependencias

Uso direto no app removido:

```text
rg -n "CustomUpdate|Custom_Update|Update\.|U_FLASH|U_SPIFFS|UPDATE_ERROR_NO_PARTITION" src include
```

Resultado relevante: nao ha mais uso direto de `CustomUpdate.h`, `Update.begin`, `Update.write`, `Update.end`, `U_FLASH` ou `U_SPIFFS` nos fluxos SD/WebUI/partitioner. As ocorrencias restantes de `UPDATE_ERROR_NO_PARTITION` sao os novos codigos locais `LAUNCHER_UPDATE_ERROR_NO_PARTITION` em `src/idf/idf_update.*`.

Ainda pendente:

- `M5-HTTPUpdate` continua no grafo porque `src/onlineLauncher.h` ainda inclui `M5-HTTPUpdate.h`.
- `httpUpdate.*` continua em `src/onlineLauncher.cpp`; sera removido no Milestone 2.
- `Custom_Update` ainda pode aparecer como objeto recuperado do cache ou dependencia transitiva enquanto `M5-HTTPUpdate` existir. A remocao completa do par `M5-HTTPUpdate`/`Custom_Update` depende do Milestone 2.

### Observacoes

- A camada IDF preserva o comportamento importante do `CustomUpdate`: para app update, o magic byte/header inicial e gravado somente no final para evitar que uma particao parcialmente escrita pareca bootavel.
- Com as tabelas de particao atualizadas para incluir `otadata`, o app update agora chama `esp_ota_set_boot_partition` no final para ativar a particao gravada.
- O pequeno aumento de tamanho e esperado nesta etapa porque `M5-HTTPUpdate` ainda permanece. A reducao deve aparecer depois que `M5-HTTPUpdate`, `HTTPClient` e `NetworkClientSecure` forem removidos.

## Milestone 2 - cliente HTTP/HTTPS nativo para OTA online

Data: 2026-05-14

Ambiente validado: `m5stack-cardputer`

Comandos executados:

```powershell
pio run -e m5stack-cardputer -t clean
pio run -e m5stack-cardputer
```

Log completo salvo em `docs/milestone2-m5stack-cardputer-build.log`.

Observacao: o build terminou com sucesso, mas o PowerShell retornou exit code `1` porque o warning abaixo saiu em stderr depois do sucesso:

```text
lto-wrapper.exe: warning: using serial compilation of 6 LTRANS jobs
```

### Alteracoes

- Criada a camada `src/idf/idf_http_client.h` e `src/idf/idf_http_client.cpp`.
- Removidos os includes de `HTTPClient.h`, `WiFiClientSecure.h` e `M5-HTTPUpdate.h` de `src/onlineLauncher.h`.
- Removido o include de `HTTPClient.h` de `src/main.cpp`.
- `getInfo()` agora usa `esp_http_client` via `launcherHttpGetToString`.
- `downloadFirmware()` agora faz streaming HTTP direto para `File`, mantendo o header `HWID`.
- `installExtFirmware()` agora usa `Range` nativo para ler a tabela de particao remota.
- `installFirmware()` agora instala app/SPIFFS via streaming HTTP direto para `launcherUpdate*`.
- `installFAT_OTA()` agora usa `Range` nativo e a API local `launcherUpdate*` para `vfs`/`sys`.

### Tamanhos

| Artefato | Baseline | Milestone 1 | Milestone 2 | Delta vs M1 |
| --- | ---: | ---: | ---: | ---: |
| `.pio/build/m5stack-cardputer/firmware.bin` | 1.276.656 bytes | 1.277.008 bytes | 1.323.920 bytes | +46.912 bytes |
| `Launcher-m5stack-cardputer.bin` | 1.342.192 bytes | 1.342.544 bytes | 1.389.456 bytes | +46.912 bytes |

Resumo PlatformIO:

```text
RAM:   [==        ]  21.6% (used 70732 bytes from 327680 bytes)
Flash: [===       ]  25.6% (used 1323511 bytes from 5177344 bytes)
Launcher: [==================  ] 91.8% (used 0x143390 bytes of 0x160000 of test partition)
```

### Dependencias

Uso direto no app removido:

```text
rg -n "HTTPClient|WiFiClientSecure|M5-HTTPUpdate|httpUpdate|HTTPC_FORCE_FOLLOW_REDIRECTS|NetworkClientSecure|CustomUpdate|Custom_Update|Update\\." src include
```

Resultado relevante: nao ha mais uso ativo de `HTTPClient`, `WiFiClientSecure`, `M5-HTTPUpdate`, `httpUpdate`, `HTTPC_FORCE_FOLLOW_REDIRECTS`, `NetworkClientSecure`, `CustomUpdate`, `Custom_Update` ou chamadas `Update.*`. A ocorrencia restante de `appUpdate.expected` e apenas o nome de uma variavel local.

Grafo de dependencias apos build limpo nao lista:

```text
HTTPClient
NetworkClientSecure
M5-HTTPUpdate
Custom_Update
```

Ainda pendente:

- `AsyncTCP` e `ESPAsyncWebServer` continuam no grafo por causa do WebUI atual; remocao prevista no Milestone 3.
- `Wire` e `SPI` continuam por decisao explicita de escopo, pois ainda sao necessarios para display/Arduino GFX e cartao SD.

### Observacoes

- O Milestone 2 removeu as dependencias alvo, mas aumentou o binario em `46.912 bytes` contra o Milestone 1. A causa provavel e a entrada direta do stack `esp_http_client`/TLS do ESP-IDF no link final.
- A equivalencia funcional com `WiFiClientSecure::setInsecure()` foi mantida com verificacao de certificado desabilitada no cliente IDF. Isso preserva o comportamento atual, mas continua sendo um risco de seguranca conhecido.
- Fluxos que precisam validacao em hardware: download do LauncherHub, OTA app completo, OTA app com offset, SPIFFS remoto e FAT remoto.

## Milestone 3 - WebUI com esp_http_server

Data: 2026-05-14

Ambiente validado: `m5stack-cardputer`

Comandos executados:

```powershell
pio run -e m5stack-cardputer -t clean
pio run -e m5stack-cardputer
```

Log completo salvo em `docs/milestone3-m5stack-cardputer-build.log`.

Observacao: o build terminou com sucesso, mas o PowerShell retornou exit code `1` porque o warning abaixo saiu em stderr depois do sucesso:

```text
lto-wrapper.exe: warning: using serial compilation of 6 LTRANS jobs
```

### Alteracoes

- Criada a camada `src/idf/idf_web_server.h` e `src/idf/idf_web_server.cpp`.
- `src/webInterface.h` nao inclui mais `AsyncTCP.h` nem `ESPAsyncWebServer.h`.
- `src/webInterface.cpp` foi migrado para handlers `esp_http_server`.
- Removidas as flags `CONFIG_ASYNC_TCP_RUNNING_CORE` e `CONFIG_ASYNC_TCP_USE_WDT` de `platformio.ini`.
- Mantido `ESPmDNS` para preservar acesso por `launcher.local`.
- Mantidos `Wire` e `SPI` no build, conforme escopo definido.

### Rotas preservadas

```text
GET  /ping
POST /login
GET  /logout
GET  /logged-out
POST /UPDATE
POST /rename
POST /OTA
POST /OTAFILE
GET  /scripts.js
GET  /style.css
GET  /
POST /
GET  /systeminfo
GET  /reboot
GET  /listfiles
GET  /file
GET  /sdpins
GET  /wifi
```

### Tamanhos

| Artefato | Baseline | Milestone 2 | Milestone 3 | Delta vs M2 | Delta vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| `.pio/build/m5stack-cardputer/firmware.bin` | 1.276.656 bytes | 1.323.920 bytes | 1.275.328 bytes | -48.592 bytes | -1.328 bytes |
| `Launcher-m5stack-cardputer.bin` | 1.342.192 bytes | 1.389.456 bytes | 1.340.864 bytes | -48.592 bytes | -1.328 bytes |

Resumo PlatformIO:

```text
RAM:   [==        ]  21.5% (used 70372 bytes from 327680 bytes)
Flash: [==        ]  24.6% (used 1274931 bytes from 5177344 bytes)
Launcher: [=================   ] 88.5% (used 0x1375C0 bytes of 0x160000 of test partition)
```

### Dependencias

Verificacao executada:

```text
rg -n "AsyncWebServer|ESPAsyncWebServer|AsyncTCP|DefaultHeaders|AsyncWeb|CONFIG_ASYNC_TCP" src include platformio.ini boards/m5stack-cardputer/platformio.ini
```

Resultado: sem ocorrencias.

Grafo de dependencias apos build limpo nao lista:

```text
AsyncTCP
ESPAsyncWebServer
```

### Observacoes

- O servidor nativo foi configurado para porta 80, `max_open_sockets = 2`, `lru_purge_enable = true`, timeouts de 10 segundos e `httpd_uri_match_wildcard`.
- Upload de arquivo e OTA via WebUI usam parser multipart local com streaming, sem carregar o arquivo inteiro em RAM.
- Fluxos que precisam validacao em hardware: login/logout, listagem SD, upload de arquivo, download de arquivo, delete, rename, criar pasta, reboot e OTA via upload.
