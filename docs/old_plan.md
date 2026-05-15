# Plano de reducao do binario

Objetivo: reduzir o tamanho final do firmware removendo dependencias pesadas de Arduino, ESPAsyncWebServer/AsyncTCP, Custom_Update e M5Stack-HTTPUpdate, mantendo as funcoes atuais de SD, WebUI, OTA online, OTA via upload e `launcher.local`.

Este arquivo e um roteiro para agentes de IA. Cada milestone deve ser executado em PR/commit separado, com medicao antes/depois e sem misturar refactors nao relacionados.

## Regras para agentes

- Trabalhe em milestones na ordem abaixo. Nao pule para a remocao total do framework Arduino antes de isolar WebUI e OTA.
- Antes de editar, rode `git status --short` e preserve mudancas existentes do usuario.
- Use somente o ambiente `m5stack-cardputer` para teste de compilacao nesta fase.
- Sempre registre no resumo do milestone:
  - comando de build usado;
  - tamanho do firmware antes/depois;
  - dependencias removidas ou ainda pendentes;
  - fluxos manuais que precisam teste em hardware.
- Nao aumente a superficie funcional. O WebUI deve continuar simples: 1 cliente normal, 2 clientes no maximo.
- Prefira APIs ESP-IDF nativas. Use wrappers pequenos no projeto quando o codigo ainda precisar conviver com tipos Arduino durante a transicao.
- Novos arquivos criados para camadas nativas devem ficar em `src/idf/`.
- Nao remova nem substitua `Wire.h` e `SPI.h` neste plano. Eles continuam necessarios para Arduino GFX/display e cartao SD.
- Nao remova `lib_modules` ou `lib` inteiros ate que `rg` prove que nao ha includes/links ativos.

## Estado atual relevante

- Build global em [platformio.ini](../platformio.ini) usa `framework = arduino`.
- `src/webInterface.h` inclui `AsyncTCP.h`, `ESPAsyncWebServer.h`, `ESPmDNS.h`, `SPI.h`, `WiFi.h` e `webFiles.h`.
- `src/webInterface.cpp` usa `AsyncWebServer`, `AsyncWebServerRequest`, `AsyncWebServerResponse`, `DefaultHeaders`, `MDNS`, `WiFi`, `Update`, upload multipart e handlers HTTP.
- `src/onlineLauncher.h` inclui `HTTPClient.h`, `M5-HTTPUpdate.h`, `SPIFFS.h`, `WiFi.h`, `WiFiClientSecure.h`.
- `src/onlineLauncher.cpp` usa `HTTPClient`, `WiFiClientSecure`, `httpUpdate`, redirects, Range requests, download para SD e OTA online.
- `src/sd_functions.h` inclui `CustomUpdate.h`.
- `src/sd_functions.cpp`, `src/webInterface.cpp` e `src/partitioner.cpp` usam `Update.begin/write/end` e constantes `U_FLASH`, `U_SPIFFS`, `UPDATE_ERROR_NO_PARTITION`.
- `include/globals.h` inclui `Arduino.h`, `pins_arduino.h`, `ArduinoJson.h`, `LittleFS.h`, `functional`, `vector`.

## Milestone 0 - baseline e mapa de dependencias

Meta: criar uma base quantitativa para comparar todo o trabalho.

Tarefas:

- Rodar builds limpos:
  - `pio run -e m5stack-cardputer`
- Salvar tamanhos em `docs/size-report.md` com:
  - tamanho de `.pio/build/<env>/firmware.bin`;
  - linhas `RAM`/`Flash` impressas por `--print-memory-usage`;
  - tamanho de `Launcher-*.bin` se o script de merge gerar artefato final.
- Gerar mapa de includes e usos:
  - `rg -n "AsyncWebServer|AsyncTCP|HTTPClient|WiFiClientSecure|M5-HTTPUpdate|CustomUpdate|Update\\.|Arduino\\.h|WiFi\\.|MDNS" src include boards lib platformio.ini`
- Identificar todos os ambientes que explicitamente alteram `framework`, `lib_deps` ou `lib_ignore`.

Aceite:

- `docs/size-report.md` existe e tem baseline para pelo menos `m5stack-cardputer`.
- Lista de arquivos impactados esta no relatorio.
- Nenhuma alteracao funcional alem de documentacao.

## Milestone 1 - camada nativa de update/flash

Meta: substituir `Custom_Update` e o objeto Arduino `Update` por uma API local baseada em ESP-IDF.

Arquivos alvo:

- Criar `src/idf/idf_update.h`
- Criar `src/idf/idf_update.cpp`
- Alterar `src/sd_functions.h`
- Alterar `src/sd_functions.cpp`
- Alterar `src/webInterface.cpp`
- Alterar `src/partitioner.cpp`

API sugerida:

```cpp
enum LauncherUpdateTarget {
    LAUNCHER_UPDATE_APP,
    LAUNCHER_UPDATE_SPIFFS,
    LAUNCHER_UPDATE_FAT_VFS,
    LAUNCHER_UPDATE_FAT_SYS,
};

using LauncherUpdateProgress = void (*)(size_t written, size_t total);

bool launcherUpdateBegin(LauncherUpdateTarget target, size_t size);
bool launcherUpdateWrite(const uint8_t *data, size_t len);
bool launcherUpdateEnd();
int launcherUpdateLastError();
const char *launcherUpdateLastErrorName();
bool launcherUpdateStream(Stream &source, size_t size, LauncherUpdateTarget target, LauncherUpdateProgress cb);
```

Implementacao:

- Para app: usar `esp_ota_get_next_update_partition`, `esp_ota_begin`, `esp_ota_write`, `esp_ota_end`, `esp_ota_set_boot_partition`.
- Para SPIFFS/FAT: usar `esp_partition_find_first`, `esp_partition_erase_range`, `esp_partition_write`.
- Manter compatibilidade temporaria com `Stream` porque `File` e clientes de rede ainda sao Arduino nesta etapa.
- Definir constantes locais equivalentes a `U_FLASH`, `U_SPIFFS`, `U_FAT_vfs`, `U_FAT_sys` somente se ainda forem necessarias para reduzir diff.
- Remover `#include <CustomUpdate.h>` de `src/sd_functions.h`.

Aceite:

- `rg -n "CustomUpdate|Custom_Update|Update\\.|U_SPIFFS|UPDATE_ERROR_NO_PARTITION" src include` nao deve encontrar uso ativo, exceto comentarios temporarios justificados.
- OTA por arquivo no SD continua chamando `progressHandler`.
- Upload OTA pelo WebUI continua instalando app.
- Build passa com `pio run -e m5stack-cardputer`.

## Milestone 2 - cliente HTTP/HTTPS nativo para OTA online

Meta: remover `M5Stack-HTTPUpdate`, `HTTPClient` e `WiFiClientSecure` dos fluxos OTA online, usando ESP-IDF.

Arquivos alvo:

- Criar `src/idf/idf_http_client.h`
- Criar `src/idf/idf_http_client.cpp`
- Alterar `src/onlineLauncher.h`
- Alterar `src/onlineLauncher.cpp`

API sugerida:

```cpp
struct LauncherHttpResponse {
    int status;
    int64_t content_length;
    char content_range[96];
};

using LauncherHttpChunkCb = bool (*)(const uint8_t *data, size_t len, void *ctx);

bool launcherHttpGetToBuffer(const char *url, std::string &out, size_t max_size);
bool launcherHttpGetRange(const char *url, uint32_t offset, uint32_t size, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *resp);
bool launcherHttpGetStream(const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *resp);
```

Implementacao:

- Usar `esp_http_client`.
- Habilitar redirects via config/event handling ou repetir manualmente em `Location`.
- Para HTTPS, comecar equivalente ao comportamento atual `setInsecure()`: `cert_pem = NULL` e config apropriada para skip CN/cert se o IDF permitir no framework usado. Documentar risco.
- Implementar headers:
  - `Range: bytes=<start>-<end>` para tabela de particao, SPIFFS e FAT.
  - `HWID: <mac>` no download quando o codigo atual envia esse header.
- Substituir:
  - `getInfo()` por GET para string e `deserializeJson`.
  - `downloadFirmware()` por streaming para `File`.
  - `installExtFirmware()` por Range nativo.
  - `installFirmware()` por streaming direto para `launcherUpdate*`.
  - `installFAT_OTA()` por Range nativo + `performFATUpdate` ou nova API de particao.

Aceite:

- `rg -n "M5-HTTPUpdate|httpUpdate|HTTPClient|WiFiClientSecure" src include` nao encontra uso ativo.
- `lib/M5Stack-HTTPUpdate` nao e mais linkada.
- OTA online ainda suporta:
  - obter JSON do LauncherHub;
  - Range request para tabela de particao em offset `0x8000`;
  - instalar app completo;
  - instalar app com offset;
  - instalar SPIFFS/FAT quando presentes e habilitados.
- Build passa com `pio run -e m5stack-cardputer`.

## Milestone 3 - WebUI com esp_http_server

Meta: substituir `ESPAsyncWebServer` e `AsyncTCP` por `esp_http_server`, reduzindo dependencias e mantendo os endpoints atuais.

Arquivos alvo:

- Criar `src/idf/idf_web_server.h`
- Criar `src/idf/idf_web_server.cpp`
- Reescrever `src/webInterface.h`
- Reescrever internamente `src/webInterface.cpp`
- Ajustar `platformio.ini`/`boards/*/platformio.ini` apenas depois que nao houver includes async.

Config sugerida:

```cpp
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = 80;
config.max_open_sockets = 2;
config.lru_purge_enable = true;
config.recv_wait_timeout = 10;
config.send_wait_timeout = 10;
config.uri_match_fn = httpd_uri_match_wildcard;
```

Rotas que devem permanecer:

- `GET /ping`
- `POST /login`
- `GET /logout`
- `GET /logged-out`
- `POST /UPDATE`
- `POST /rename`
- `POST /OTA`
- `POST /OTAFILE`
- `GET /scripts.js`
- `GET /style.css`
- `GET /`
- `GET /systeminfo`
- `GET /reboot`
- `GET /listfiles`
- `GET /file`
- `GET /sdpins`
- `GET /wifi`
- fallback redirect para `/`

Detalhes de implementacao:

- Servir assets gzipados de `webFiles.h` com headers:
  - `Content-Type`
  - `Content-Encoding: gzip`
  - `Access-Control-Allow-Origin: *`
- Autenticacao:
  - Ler header `Cookie`.
  - Manter token `ESP32SESSION=<token>`.
  - Em falha com pagina: retornar `login.html`.
  - Em falha API: retornar `401 Unauthorized`.
- Query/body params:
  - Implementar helpers pequenos para URL decode, query string e form-urlencoded.
  - Evitar parser generico grande.
- Upload multipart:
  - Implementar somente o necessario para o WebUI atual.
  - Suportar campo de arquivo com `filename=`.
  - Escrever chunks direto no SD ou no `launcherUpdateWrite`.
  - Limitar memoria: buffer fixo <= 2 KiB.
- Download de arquivo:
  - Usar `httpd_resp_send_chunk` lendo `File` em blocos.
- Ciclo de vida:
  - `configureWebServer()` registra rotas.
  - `startWebUi()` cria `httpd_handle_t`, chama `httpd_start`, encerra com `httpd_stop`.

Aceite:

- `rg -n "AsyncWebServer|AsyncTCP|ESPAsyncWebServer|DefaultHeaders|AsyncWeb" src include` nao encontra uso ativo.
- `launcher.local` funciona via mDNS.
- Browser acessa `http://launcher.local`.
- Login, logout, listagem SD, upload arquivo, download arquivo, delete, rename, criar pasta, reboot e OTA via upload funcionam em hardware.
- Build passa com `pio run -e m5stack-cardputer` e o tamanho do binario diminui em relacao ao baseline.

## Milestone 4 - WiFi e mDNS por ESP-IDF

Meta: remover o uso de `WiFi.h` e `ESPmDNS.h` dos modulos de rede principais.

Arquivos alvo:

- Criar `src/idf/idf_wifi.h`
- Criar `src/idf/idf_wifi.cpp`
- Alterar `src/onlineLauncher.cpp`
- Alterar `src/webInterface.cpp`
- Alterar ambientes que dependem de WiFi Arduino, com atencao especial para `boards/m5stack-tab5/interface.cpp`.

API sugerida:

```cpp
bool launcherWifiStartSta();
bool launcherWifiConnect(const char *ssid, const char *password, uint32_t timeout_ms);
int launcherWifiScan(std::vector<LauncherWifiAp> &out);
bool launcherWifiStartAp(const char *ssid, const char *password, uint8_t channel, uint8_t max_clients);
bool launcherWifiStop();
bool launcherWifiIsConnected();
std::string launcherWifiLocalIp();
std::string launcherWifiApIp();
std::string launcherWifiMac();
bool launcherMdnsStart(const char *host);
void launcherMdnsStop();
```

Implementacao:

- Usar `esp_wifi`, `esp_event`, `esp_netif`.
- Inicializar WiFi uma unica vez; evitar init/deinit repetido se houver targets ESP32-P4/hosted WiFi.
- Para STA:
  - `esp_wifi_set_mode(WIFI_MODE_STA)`
  - `esp_wifi_set_config`
  - aguardar conectado via EventGroup.
- Para AP:
  - IP `172.0.0.1` como hoje.
  - max clients 2 ou 4 somente se houver motivo para manter o comportamento atual.
- Para scan:
  - retornar SSID e `wifi_auth_mode_t`.
- Para mDNS:
  - usar `mdns_init`, `mdns_hostname_set("launcher")`, `mdns_service_add("http", "_http", "_tcp", 80, ...)`.

Aceite:

- `src/webInterface.*` e `src/onlineLauncher.*` nao incluem `WiFi.h` nem `ESPmDNS.h`.
- `launcher.local` continua funcionando em STA e AP quando suportado.
- Scan, conexao por rede salva, SSID oculto e AP mode funcionam.
- Build passa com `pio run -e m5stack-cardputer`.

## Milestone 5 - reduzir dependencia de Arduino.h no codigo do app

Meta: remover `#include <Arduino.h>` dos headers centrais e diminuir dependencias implicitas de `String`, `Stream`, `File`, `Serial`, `millis`, `delay`, `random`, `pinMode`, `digitalWrite`.

Arquivos alvo:

- `include/globals.h`
- `src/*.h`
- `src/*.cpp`
- `include/VectorDisplay.h`
- `boards/*/interface.cpp` somente quando necessario.

Ordem recomendada:

1. Criar `src/idf/launcher_platform.h` com wrappers pequenos:
   - tempo: `launcherMillis()`, `launcherDelayMs()`;
   - log: wrappers explicitos para debug e mensagens funcionais;
   - random: `esp_random`;
   - GPIO: wrappers sobre `gpio_set_direction`, `gpio_set_level`, quando viavel.
2. Mover includes Arduino para `.cpp`, nunca para headers compartilhados, quando o tipo nao aparece na assinatura publica.
3. Trocar `String` por `std::string` ou buffers fixos nos novos modulos IDF.
4. Manter `String` temporariamente em UI/menu/config ate milestone especifico posterior; nao reescrever tudo de uma vez.
5. Repensar `Serial.*` por categoria, sem troca direta para `ESP_LOGx`:
   - `ESP_LOGx` nao substitui mensagens funcionais enquanto o build usar `-DCORE_DEBUG_LEVEL=0`, porque esses logs ficam desabilitados.
   - Debug/diagnostico opcional pode virar macro compilavel fora, por exemplo `LAUNCHER_LOGD(...)`.
   - Mensagens funcionais que o usuario precisa ver em headless/WebUI devem usar wrapper proprio que continue emitindo mesmo com `CORE_DEBUG_LEVEL=0`, por exemplo `launcherConsolePrintf(...)` baseado em `printf`, `uart_write_bytes` ou `Serial` enquanto Arduino ainda existir.
   - Mensagens ruidosas ou puramente temporarias devem ser removidas, nao migradas.
   - Interfaces de board podem continuar usando `Serial.*` ate uma fase posterior.
6. Manter `Wire.h` e `SPI.h` onde forem necessarios para display e SD; nao tratar esses includes como falha neste milestone.

Aceite:

- `rg -n "#include <Arduino\\.h>|#include \"Arduino\\.h\"" src include` mostra somente excecoes documentadas.
- Headers centrais nao forcam `Arduino.h` por transitividade.
- Build passa com `pio run -e m5stack-cardputer`.
- Tamanho nao aumenta; se aumentar, justificar com dados.

## Milestone 6 - limpeza de dependencias e configuracao de build

Meta: impedir que bibliotecas removidas sejam compiladas/linkadas.

Arquivos alvo:

- `platformio.ini`
- `boards/*/platformio.ini`
- `.gitmodules` se submodulos forem realmente removidos em milestone separado.

Tarefas:

- Remover flags exclusivas de AsyncTCP:
  - `CONFIG_ASYNC_TCP_RUNNING_CORE`
  - `CONFIG_ASYNC_TCP_USE_WDT`
- Adicionar `lib_ignore` global ou por ambiente para:
  - `ESPAsyncWebServer`
  - `AsyncTCP`
  - `M5Stack-HTTPUpdate`
  - `Custom_Update`
- Verificar se `lib_extra_dirs = lib_modules` ainda e necessario para outras libs.
- Nao apagar diretorios de biblioteca nesta etapa se o projeto usa submodulos; primeiro confirmar politica de repo.

Aceite:

- Build passa com `pio run -e m5stack-cardputer` sem compilar as libs removidas.
- `pio run -e m5stack-cardputer -v` nao mostra includes dessas libs em comandos de compilacao.
- `docs/size-report.md` atualizado com ganho acumulado.

## Milestone 7 - avaliar migracao parcial ou total para framework ESP-IDF

Meta: decidir com dados se o projeto pode sair de `framework = arduino` ou se deve manter Arduino como camada temporaria por causa de display/SD/libs de board.

Tarefas:

- Criar branch/prototipo separado, sem misturar no fluxo principal.
- Testar `framework = espidf` ou `framework = arduino, espidf` conforme suporte da plataforma.
- Levantar blockers:
  - M5GFX/TFT/Arduino_GFX;
  - SD/SD_MMC/FS;
  - ArduinoJson;
  - interfaces de board que usam `String`, `Serial`, `Wire`, `SPI`, `millis`.
- Se a migracao total for grande demais, manter uma estrategia hibrida:
  - WebUI, OTA, WiFi e HTTP nativos;
  - display/input/SD ainda em Arduino ate milestones especificos.

Aceite:

- Documento curto em `docs/size-report.md` ou `docs/espidf-migration-notes.md` com:
  - tamanho do prototipo;
  - erros principais;
  - lista de dependencias que bloqueiam `framework = espidf`;
  - recomendacao objetiva: migrar agora, migrar parcialmente ou adiar.

## Milestone 8 - validacao em hardware e regressao funcional

Meta: garantir que a reducao nao quebrou os fluxos usados por usuarios.

Checklist manual minimo:

- Boot normal no Launcher.
- Boot para app instalado.
- SD list/read/write/delete/rename/create folder.
- WebUI STA em `http://launcher.local`.
- WebUI AP em `http://launcher.local` e IP exibido.
- Login/logout e cookie persistido.
- Upload de arquivo comum para SD.
- Download de arquivo do SD via browser.
- OTA via upload WebUI.
- OTA a partir de arquivo no SD.
- OTA online LauncherHub:
  - listar firmwares;
  - instalar firmware sem bootloader/partition table;
  - instalar firmware com bootloader/partition table;
  - instalar SPIFFS/FAT quando aplicavel.
- Headless inicia WebUI em AP quando nao ha credencial.

Aceite:

- Todos os itens acima foram testados ou marcados como "nao testado" com motivo.
- `docs/size-report.md` contem tabela final de tamanho por milestone.
- PR final remove ou ignora dependencias obsoletas.

## Riscos principais

- `esp_http_server` nao fornece parser multipart pronto; implementar somente o subset usado pelo WebUI.
- HTTPS sem certificado replica o comportamento atual, mas deve ser documentado como risco de seguranca.
- Remover `Arduino.h` de uma vez tende a explodir escopo por causa de `String`, `File`, `Stream`, `Serial`, `Wire`, `SPI` e bibliotecas de display.
- ESP32-P4/hosted WiFi pode exigir tratamento especial; preservar os `#if CONFIG_ESP_HOSTED_ENABLED` existentes ate validar hardware.
- O ganho real so aparece quando a biblioteca deixa de compilar/linkar. Trocar includes sem `lib_ignore` pode nao reduzir binario.

## Comandos uteis

```powershell
git status --short
pio run -e m5stack-cardputer
Get-Item .pio\build\m5stack-cardputer\firmware.bin | Select-Object Length
rg -n "AsyncWebServer|AsyncTCP|ESPAsyncWebServer|HTTPClient|WiFiClientSecure|M5-HTTPUpdate|CustomUpdate|Update\.|Arduino\.h" src include lib platformio.ini boards
rg -n "framework\s*=|lib_deps|lib_ignore|lib_extra_dirs" platformio.ini boards
```
