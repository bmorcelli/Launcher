# Launcher WebUI — Dev Server

Backend Node.js que replica o comportamento do `src/webInterface.cpp` para testar a interface web sem precisar de hardware ESP32.

## Requisitos

- Node.js 16+
- Sem dependências externas

## Uso

```bash
node server.js <pasta-raiz>
```

A `<pasta-raiz>` é tratada como o cartão SD. Todos os arquivos listados, baixados, enviados e deletados operam dentro dessa pasta.

**Exemplos:**

```bash
node server.js C:\Users\bmorc\Downloads
node server.js D:\testes\sdcard
```

Via npm (de dentro da pasta `backend/`):

```bash
npm start -- C:\Users\bmorc\Downloads
```

Porta e credenciais via variáveis de ambiente:

```bash
PORT=3000 WUI_USR=admin WUI_PWD=minhasenha node server.js C:\pasta
```

Após iniciar, abra: **http://localhost:8080**

Login padrão: `admin` / `admin`

## Endpoints implementados

| Endpoint | Método | Comportamento |
|---|---|---|
| `/ping` | GET | Retorna `launcher-pong` |
| `/login` | POST | Autentica e cria cookie de sessão |
| `/logout` | GET | Encerra sessão, redireciona |
| `/systeminfo` | GET | Versão mock + estatísticas do SD |
| `/listfiles?folder=` | GET | Lista real do diretório |
| `/file?name=&action=` | GET | Download, delete ou create (pasta) |
| `/editfile?name=` | GET / POST | Lê e salva arquivos de texto |
| `/` | POST | Upload de arquivos (multipart) |
| `/rename` | POST | Renomeia arquivo ou pasta |
| `/nvs` | GET / POST | Dados NVS persistidos em `nvs_mock.json` |
| `/wifi` | GET | Simulado (loga no console) |
| `/sdpins` | GET | Simulado |
| `/reboot` | GET | Simulado |
| `/OTA` | POST | Simulado |
| `/OTAFILE` | POST | Simulado |
| `/UPDATE` | POST | Simulado |

## NVS

Os dados NVS ficam em `nvs_mock.json` na mesma pasta do servidor e persistem entre reinicializações. Na primeira execução, um conjunto de chaves de exemplo é criado automaticamente.

A chave `launcher/token` nunca é exposta nem editável, assim como no firmware.

## Segurança

Todas as operações de arquivo ficam restritas à pasta raiz informada na inicialização — tentativas de path traversal são bloqueadas.
