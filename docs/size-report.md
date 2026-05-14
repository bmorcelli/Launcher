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
