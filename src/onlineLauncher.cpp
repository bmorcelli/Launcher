#include "onlineLauncher.h"
#include "sd_functions.h"
#include <globals.h>
#include "display.h"
#include "mykeyboard.h"
#include "settings.h"
#include "powerSave.h"

#if defined(M5STACK)
#define SERVER_PATH "https://m5burner-cdn.m5stack.com/firmware/"
#else
#define SERVER_PATH ""
#endif

#ifndef JSON_SOURCE_PATH
#define JSON_SOURCE_PATH "https://raw.githubusercontent.com/bmorcelli/M5Stack-json-fw/main/v2/third_party.json"
#endif
/***************************************************************************************
** Function name: wifiConnect
** Description:   Connects to wifiNetwork
***************************************************************************************/
void wifiConnect(String ssid, int encryptation, bool isAP)
{
  if (!isAP)
  {
    bool found = false;
    bool wrongPass = false;
    getConfigs();
    JsonObject setting = settings[0];
    JsonArray WifiList = setting["wifi"].as<JsonArray>();
    EEPROM.begin(EEPROMSIZE);
    pwd = EEPROM.readString(20);
    EEPROM.end();
    log_i("sdcardMounted: %d", sdcardMounted);

    if (sdcardMounted)
    {
      for (JsonObject wifiEntry : WifiList)
      {
        String name = wifiEntry["ssid"].as<String>();
        String pass = wifiEntry["pwd"].as<String>();
        log_i("SSID: %s, Pass: %s", name, pass);
        if (name == ssid)
        {
          pwd = pass;
          found = true;
          log_i("Found SSID: %s", name);
          break;
        }
      }
    }

  Retry:
    if (!found || wrongPass)
    {
      if (encryptation > 0)
        pwd = keyboard(pwd, 63, "Network Password:");

      EEPROM.begin(EEPROMSIZE);
      if (pwd != EEPROM.readString(20))
      {
        EEPROM.writeString(20, pwd);
        EEPROM.commit(); // Store data to EEPROM
      }
      EEPROM.end(); // Free EEPROM memory
      if (sdcardMounted && !found)
      {
        // Cria um novo objeto JSON para adicionar ao array "wifi"
        JsonObject newWifi = WifiList.add<JsonObject>();
        newWifi["ssid"] = ssid;
        newWifi["pwd"] = pwd;
        found = true;
        saveConfigs();
      }
      else if (sdcardMounted && found && wrongPass)
      {
        for (JsonObject wifiEntry : WifiList)
        {
          if (wifiEntry["ssid"].as<String>() == ssid)
          {
            wifiEntry["pwd"] = pwd;
            log_i("Mudou pwd de SSID: %s", ssid);
            break;
          }
        }
        saveConfigs();
      }
    }

    WiFi.begin(ssid.c_str(), pwd.c_str());

    resetTftDisplay(10, 10, FGCOLOR, FP);
    tft->fillScreen(BGCOLOR);
    tftprint("Connecting to: " + ssid + ".", 10);
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    // Simulação da função de desenho no display TFT
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      tftprint(".", 10);
      count++;
      if (count > 20)
      {
        wrongPass = true;
        options = {
            {"Retry", [&]()
             { yield(); }},
            {"Main Menu", [&]()
             { returnToMenu = true; }},
        };
        loopOptions(options);
        if (!returnToMenu)
          goto Retry;
        else
          goto END;
      }
    }
  }
  else
  { // Running in Access point mode
    IPAddress AP_GATEWAY(172, 0, 0, 1);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Launcher", "", 6, 0, 1, false);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
  }
END:
  delay(0);
}

/***************************************************************************************
** Function name: ota_function
** Description:   Start OTA function
***************************************************************************************/
void ota_function()
{
#ifndef DISABLE_OTA
  if (!stopOta)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      int nets;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_MODE_STA);
      displayRedStripe("Scanning...");
      nets = WiFi.scanNetworks();
      options = {};
      for (int i = 0; i < nets; i++)
      {
        options.push_back({WiFi.SSID(i).c_str(), [=]()
                           { wifiConnect(WiFi.SSID(i).c_str(), int(WiFi.encryptionType(i))); }});
      }
      options.push_back({"Hidden SSID", [=]()
                         { String __ssid=keyboard("", 32, "Your SSID"); wifiConnect(__ssid.c_str(),8); }});
      options.push_back({"Main Menu", [=]()
                         { returnToMenu = true; }});
      loopOptions(options);
      if (WiFi.status() == WL_CONNECTED)
      {
        if (GetJsonFromM5())
          loopFirmware();
      }
    }
    else
    {
      // If it is already connected, download the JSON again... it loses the information once you step out of loopFirmware(), dkw
      closeSdCard();
      if (GetJsonFromM5())
        loopFirmware();
    }
    tft->fillScreen(BGCOLOR);
  }
  else
  {
    displayRedStripe("Restart to open OTA");
    delay(3000);
  }
#else
  displayRedStripe("Not M5 Device");
  delay(3000);
#endif
}

#ifndef DISABLE_OTA

/***************************************************************************************
** Function name: replaceChars
** Description:   Replace some characters for _
***************************************************************************************/
String replaceChars(String input)
{
  // Define os caracteres que devem ser substituídos
  const char charsToReplace[] = {'/', '\\', '\"', '\'', '`'};
  // Define o caractere de substituição (neste exemplo, usamos um espaço)
  const char replacementChar = '_';

  // Percorre a string e substitui os caracteres especificados
  for (size_t i = 0; i < sizeof(charsToReplace); i++)
  {
    input.replace(String(charsToReplace[i]), String(replacementChar));
  }
  return input;
}
/***************************************************************************************
** Function name: GetJsonFromM5
** Description:   Gets JSON from github server
***************************************************************************************/
bool GetJsonFromM5()
{
  const char *serverUrl = JSON_SOURCE_PATH;

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    int httpResponseCode = -1;
    resetTftDisplay(tftWidth / 2 - 6 * String("Getting info from").length(), 32);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    tft->drawCentreString("Getting info from", tftWidth / 2, tftHeight / 3, 1);
    tft->drawCentreString("repository", tftWidth / 2, tftHeight / 3 + FM * 9, 1);

    tft->setCursor(18, tftHeight / 3 + FM * 9 * 2);
    while (httpResponseCode < 0)
    {
      http.begin(serverUrl);
      http.useHTTP10(true);
      httpResponseCode = http.GET();
      if (httpResponseCode > 0)
      {
        DeserializationError error;
        deserializeJson(doc, http.getStream());
        delay(100);
        return true;
        break;
      }
      else
      {
        tftprint(".", 10);
        http.end();
        delay(1000);
      }
    }
    http.end();
  }
  return false;
}

/***************************************************************************************
** Function name: downloadFirmware
** Description:   Downloads the firmware and save into the SDCard
***************************************************************************************/
void downloadFirmware(String file_str, String fileName, String folder)
{
  String fileAddr = SERVER_PATH + file_str;
  int tries = 0;
  fileName = replaceChars(fileName);
  prog_handler = 2;
  if (!setupSdCard())
  {
    displayRedStripe("SDCard Not Found");
    delay(2500);
    return;
  }

  tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
  displayRedStripe("Connecting FW");
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
retry:
  if (client)
  {
    HTTPClient http;
    int httpResponseCode = -1;

    while (httpResponseCode < 0)
    {
      http.begin(*client, fileAddr);
      http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Github links need it
      http.useHTTP10(true);
      httpResponseCode = http.GET();
      if (httpResponseCode > 0)
      {
        setupSdCard();
        if (!SDM.exists("/downloads"))
          SDM.mkdir("/downloads");

        File file = SDM.open(folder + fileName + ".bin", FILE_WRITE);
        size_t size = http.getSize();
        displayRedStripe("Downloading FW");
        if (file)
        {

          int downloaded = 0;
          WiFiClient *stream = http.getStreamPtr();
          int len = size;

          prog_handler = 2; // Download handler

          // Ler dados enquanto disponível
          progressHandler(downloaded, size);
          while (http.connected() && (len > 0 || len == -1))
          {
            // Ler dados em partes
            int size_av = stream->available();
            if (size_av)
            {
              int c = stream->readBytes(buff, std::min(size_av, bufSize));
              file.write(buff, c);

              if (len > 0)
              {
                len -= c;
              }

              downloaded += c;
              progressHandler(downloaded, size); // Chama a função de progresso
              progressHandler(downloaded, size); // Chama a função de progresso
            }
          }
          file.close();
        }
        else
        {
          log_i("Download> Couldn't create file %s", String(folder + fileName + ".bin"));
          displayRedStripe("Fail creating file.");
        }
        // Checks if the file was preatically not downloaded and try one more time (size <= bufSize)
        file = SDM.open(folder + fileName + ".bin");
        if (file.size() <= bufSize & tries < 1)
        {
          tries++;
          http.end();
          goto retry;
        }
        // Checks if the file was completely downloaded
        log_i("File size in get() = %d\nFile size in SD    = %d", size, file.size());
        if (file.size() != size)
        {
          SDM.remove(file.path());
          displayRedStripe("Download FAILED");
          while (!check(SelPress))
            yield();
        }
        else
        {
          log_i("File successfully downloaded.");
          displayRedStripe(" Downloaded ");
          while (!check(SelPress))
            yield();
        }
        file.close();
        break;
      }
      else
      {
        http.end();
        delay(500);
      }
    }
    http.end();
  }
  else
  {
    displayRedStripe("Couldn't Connect");
  }
  wakeUpScreen();
}

/***************************************************************************************
** Function name: installFirmware
** Description:   installs Firmware using OTA
***************************************************************************************/
void installFirmware(String file, uint32_t app_size, bool spiffs, uint32_t spiffs_offset, uint32_t spiffs_size, bool nb, bool fat, uint32_t fat_offset[2], uint32_t fat_size[2])
{
  uint32_t app_offset = 0x10000;

  // Release RAM Memory from Json Objects
  if (spiffs && askSpiffs)
  {
    options = {
        {"SPIFFS No", [&]()
         { spiffs = false; }},
        {"SPIFFS Yes", [&]()
         { spiffs = true; }},
    };
    loopOptions(options);
  }

  if (spiffs && spiffs_size > MAX_SPIFFS)
    spiffs_size = MAX_SPIFFS;
  if (app_size > MAX_APP)
    app_size = MAX_APP;
  if (app_size > MAX_APP)
    app_size = MAX_APP;

  // precisa revisar!!!
  if (fat && fat_size[0] > MAX_FAT_vfs && fat_size[1] == 0)
    fat_size[0] = MAX_FAT_vfs;
  else if (fat && fat_size[0] > MAX_FAT_sys)
    fat_size[0] = MAX_FAT_sys;
  if (fat && fat_size[1] > MAX_FAT_vfs)
    fat_size[1] = MAX_FAT_vfs;

  String fileAddr;

  tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
  displayRedStripe("Connecting FW");

  WiFiClientSecure *client = new WiFiClientSecure;
  fileAddr = SERVER_PATH + file;
  client->setInsecure();
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Github links need it
  /* Install App */
  prog_handler = 0;
  tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
  progressHandler(0, 500);
  httpUpdate.onProgress(progressHandler);
  httpUpdate.setLedPin(LED, LED_ON);

  if (nb)
    app_offset = 0;
  if (!httpUpdate.updateFromOffset(*client, fileAddr, app_offset, app_size))
  {
    displayRedStripe("Instalation Failed");
    goto SAIR;
  }
  if (!client)
  {
    displayRedStripe("Couldn't Connect *.m5stack.com");
    goto SAIR;
  }

  if (spiffs)
  {
    prog_handler = 1;
    tft->fillRect(5, 60, tftWidth - 10, 16, ALCOLOR);
    setTftDisplay(5, 60, WHITE, FM, ALCOLOR);

    tft->println(" Preparing SPIFFS");
    // Format Spiffs partition
    if (!SPIFFS.begin(true))
    {
      displayRedStripe("Fail to start SPIFFS");
      delay(2500);
    }
    else
    {
      displayRedStripe("Formatting SPIFFS");
      SPIFFS.format();
      SPIFFS.end();
    }
    displayRedStripe("Connecting SPIFFs");

    // Install Spiffs
    progressHandler(0, 500);
    httpUpdate.onProgress(progressHandler);

    if (!httpUpdate.updateSpiffsFromOffset(*client, fileAddr, spiffs_offset, spiffs_size))
    {
      displayRedStripe("SPIFFS Failed");
      delay(2500);
    }
  }

#if !defined(PART_04MB)
  if (fat)
  {
    // eraseFAT();
    int FAT = U_FAT_vfs;
    if (fat_size[1] > 0)
      FAT = U_FAT_sys;
    for (int i = 0; i < 2; i++)
    {
      if (fat_size[i] > 0)
      {
        if ((FAT - i * 100) == 400)
        {
          if (!installFAT_OTA(client, fileAddr, fat_offset[i], fat_size[i], "sys"))
          {
            displayRedStripe("FAT Failed");
            delay(2500);
          }
        }
        else
        {
          if (!installFAT_OTA(client, fileAddr, fat_offset[i], fat_size[i], "vfs"))
          {
            displayRedStripe("FAT Failed");
            delay(2500);
          }
        }
      }
    }
  }
#endif

Sucesso:
  esp_restart();

// Só chega aqui se der errado
SAIR:
  delay(2000);
}

/***************************************************************************************
** Function name: installFAT_OTA
** Description:   install FAT partition OverTheAir
***************************************************************************************/
bool installFAT_OTA(WiFiClientSecure *client, String fileAddr, uint32_t offset, uint32_t size, const char *label)
{
  prog_handler = 1; // review

  tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
  displayRedStripe("Connecting FAT");

  if (client)
  {
    HTTPClient http;
    int httpResponseCode = -1;
    http.begin(*client, fileAddr);
    http.addHeader("Range", "bytes=" + String(offset) + "-" + String(offset + size - 1));
    http.useHTTP10(true);

    while (httpResponseCode < 0)
    {
      httpResponseCode = http.GET();
      delay(500);
    }
    if (httpResponseCode > 0)
    {
      int size = http.getSize();
      displayRedStripe("Installing FAT");
      WiFiClient *stream = http.getStreamPtr();
      prog_handler = 1; // Download handler
      performFATUpdate(*stream, size, label);
    }
    http.end();
    delay(1000);
    return true;
  }
  else
  {
    displayRedStripe("Couldn't Connect");
    delay(2000);
    return false;
  }
}

#endif
