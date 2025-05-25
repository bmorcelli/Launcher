#include "display.h"
#include <globals.h>
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "sd_functions.h"
#include "settings.h"

#if defined(HEADLESS)
SerialDisplayClass *tft = new SerialDisplayClass();
#elif defined(E_PAPER_DISPLAY) || defined(USE_TFT_ESPI) || defined(USE_LOVYANGFX) || defined(GxEPD2_DISPLAY)
Ard_eSPI *tft = new Ard_eSPI();
#else
#ifdef TFT_PARALLEL_8_BIT
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(TFT_DC, TFT_CS, TFT_WR, TFT_RD, TFT_D0, TFT_D1, TFT_D2, TFT_D3, TFT_D4, TFT_D5, TFT_D6, TFT_D7);
#elif RGB_PANEL // 16-par connections
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
#if defined(DISPLAY_ST7262_PAR)
    ST7262_PANEL_CONFIG_DE_GPIO_NUM /* DE */,
    ST7262_PANEL_CONFIG_VSYNC_GPIO_NUM /* VSYNC */,
    ST7262_PANEL_CONFIG_HSYNC_GPIO_NUM /* HSYNC */,
    ST7262_PANEL_CONFIG_PCLK_GPIO_NUM /* PCLK */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B0 /* R0 */, // for ST7262 panels (SUNTON boards) R and B are changed
    ST7262_PANEL_CONFIG_DATA_GPIO_B1 /* R1 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B2 /* R2 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B3 /* R3 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B4 /* R4 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G0 /* G0 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G1 /* G1 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G2 /* G2 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G3 /* G3 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G4 /* G4 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G5 /* G5 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R0 /* B0 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R1 /* B1 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R2 /* B2 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R3 /* B3 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R4 /* B4 */,
    0 /* hsync_polarity */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH /* hsync_front_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH /* hsync_pulse_width */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH /* hsync_back_porch */,
    0 /* vsync_polarity */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_FRONT_PORCH /* vsync_front_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_PULSE_WIDTH /* vsync_pulse_width */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_BACK_PORCH /* vsync_back_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG /* pclk_active_neg */,
    16000000 /* prefer_speed */
#elif defined(DISPLAY_ST7701_PAR)
    ST7701_PANEL_CONFIG_DE_GPIO_NUM /* DE */,
    ST7701_PANEL_CONFIG_VSYNC_GPIO_NUM /* VSYNC */,
    ST7701_PANEL_CONFIG_HSYNC_GPIO_NUM /* HSYNC */,
    ST7701_PANEL_CONFIG_PCLK_GPIO_NUM /* PCLK */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R0 /* R0 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R1 /* R1 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R2 /* R2 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R3 /* R3 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R4 /* R4 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G0 /* G0 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G1 /* G1 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G2 /* G2 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G3 /* G3 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G4 /* G4 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G5 /* G5 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B0 /* B0 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B1 /* B1 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B2 /* B2 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B3 /* B3 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B4 /* B4 */,
    1 /* hsync_polarity */,
    ST7701_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH /* hsync_front_porch */,
    ST7701_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH /* hsync_pulse_width */,
    ST7701_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH /* hsync_back_porch */,
    1 /* vsync_polarity */,
    ST7701_PANEL_CONFIG_TIMINGS_VSYNC_FRONT_PORCH /* vsync_front_porch */,
    ST7701_PANEL_CONFIG_TIMINGS_VSYNC_PULSE_WIDTH /* vsync_pulse_width */,
    ST7701_PANEL_CONFIG_TIMINGS_VSYNC_BACK_PORCH /* vsync_back_porch */,
    ST7701_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG /* pclk_active_neg */
#endif
);
#elif defined(AXS15231B_QSPI) || defined(DRIVER_RM67162_QSPI)
Arduino_DataBus *bus = new Arduino_ESP32QSPI(TFT_CS, TFT_SCLK, TFT_D0, TFT_D1, TFT_D2, TFT_D3);
#else // SPI Data Bus shared with SDCard and other SPIClass devices
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, &SPI);
#endif

Ard_eSPI *tft = new Ard_eSPI(bus, TFT_RST, ROTATION, TFT_IPS, TFT_WIDTH, TFT_HEIGHT, TFT_COL_OFS1, TFT_ROW_OFS1, TFT_COL_OFS2, TFT_ROW_OFS2);
#endif

/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord)
{
  int len = text.length();
  String displayText = text + "        "; // Add spaces for smooth looping
  int scrollLen = len + 8;                // Full text plus space buffer
  static int i = 0;
  static long _lastmillis = 0;
  tft->setTextColor(coord.fgcolor, coord.bgcolor);
  if (len < coord.size)
  {
    // Text fits within limit, no scrolling needed
    return;
  }
  else if (millis() > _lastmillis + 200)
  {
    String scrollingPart = displayText.substring(i, i + (coord.size - 1));                                         // Display charLimit characters at a time
    tft->fillRect(coord.x, coord.y, (coord.size - 1) * LW * tft->getTextsize(), LH * tft->getTextsize(), BGCOLOR); // Clear display area
    tft->setCursor(coord.x, coord.y);
    tft->setCursor(coord.x, coord.y);
    tft->print(scrollingPart);
    if (i >= scrollLen - coord.size)
      i = -1; // Loop back
    _lastmillis = millis();
    i++;
    if (i == 1)
      _lastmillis = millis() + 1000;
  }
}

/***************************************************************************************
** Function name: resetTftDisplay
** Description:   set cursor to 0,0, screen and text to default color
***************************************************************************************/
void resetTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg, uint16_t screen)
{
  tft->setCursor(x, y);
  tft->fillScreen(screen);
  tft->setTextSize(size);
  tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: setTftDisplay
** Description:   set cursor, font color, size and bg font color
***************************************************************************************/
void setTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg)
{
  if (x >= 0 && y < 0)
    tft->setCursor(x, tft->getCursorY()); // if -1 on x, sets only y
  else if (x < 0 && y >= 0)
    tft->setCursor(tft->getCursorX(), y); // if -1 on y, sets only x
  else if (x >= 0 && y >= 0)
    tft->setCursor(x, y); // if x and y > 0, sets both
  tft->setTextSize(size);
  tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color)
{
  tft->drawRoundRect(5, tftHeight + 2, tftWidth - 10, 43, 5, color);
  tft->setTextColor(color);
  tft->setTextSize(FM);
  tft->drawCentreString("PREV", tftWidth / 6, tftHeight + 4, 1);
  tft->drawCentreString("SEL", tftWidth / 2, tftHeight + 4, 1);
  tft->drawCentreString("NEXT", 5 * tftWidth / 6, tftHeight + 4, 1);
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter2(uint16_t color)
{
  tft->drawRoundRect(5, tftHeight + 2, tftWidth - 10, 43, 5, color);
  tft->setTextColor(color);
  tft->setTextSize(FM);
  tft->drawCentreString("Skip", tftWidth / 6, tftHeight + 4, 1);
  tft->drawCentreString("LAUNCHER", tftWidth / 2, tftHeight + 4, 1);
  tft->drawCentreString("Skip", 5 * tftWidth / 6, tftHeight + 4, 1);
}

/***************************************************************************************
** Function name: BootScreen
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplay(bool doAll)
{
#ifndef HEADLESS
  static uint8_t _name = random(0, 3);
  String name = "@Pirata";
  String txt;
  int cor, _x, _y, show;

#ifdef E_PAPER_DISPLAY // epaper display draws only once
  static bool runOnce = false;
  if (runOnce)
    goto END;
  else
    runOnce = true;
  tft->stopCallback();
#endif

  if (_name == 1)
    name = "u/bmorcelli";
  else if (_name == 2)
    name = "gh/bmorcelli";
  tft->drawRoundRect(3, 3, tftWidth - 6, tftHeight - 6, 5, FGCOLOR);
  tft->setTextSize(FP);
  tft->setCursor(10, 10);
  cor = 0;
  show = random(0, 40);
  _x = tft->getCursorX();
  _y = tft->getCursorY();

  while (tft->getCursorY() < (tftHeight - (LH + 4)))
  {
    cor = random(0, 11);
    tft->setTextSize(FP);
    show = random(0, 40);
    if (show == 0 || doAll)
    {
      if (cor == 10)
      {
        txt = " ";
      }
      else if (cor & 1)
      {
        tft->setTextColor(odd_color, BGCOLOR);
        txt = String(cor);
      }
      else
      {
        tft->setTextColor(even_color, BGCOLOR);
        txt = String(cor);
      }

      if (_x >= (tftWidth - (LW + 4)))
      {
        _x = 10;
        _y += LH;
      }
      else if (_x < 10)
      {
        _x = 10;
      }
      if (_y >= (tftHeight - (LH + LH / 2)))
        break;
      tft->setCursor(_x, _y);
      if (_y > (tftHeight - (LH * FM + LH / 2)) && _x >= (tftWidth - ((LW + 4) + LW * name.length())))
      {
        tft->setTextColor(FGCOLOR);
        tft->print(name);
        _x += LW * name.length();
      }
      else
      {
        tft->print(txt);
        _x += LW;
      }
    }
    else
    {
      if (_y > (tftHeight - (LH * FM + LH / 2)) && _x >= (tftWidth - ((LW + 4) + LW * name.length())))
        _x += LW * name.length();
      else
        _x += LW;

      if (_x >= (tftWidth - (LW + 4)))
      {
        _x = 10;
        _y += LH;
      }
    }
    tft->setCursor(_x, _y);
  }
  tft->setTextSize(FG);
  tft->setTextColor(FGCOLOR);
#if TFT_HEIGHT > 200
  tft->drawCentreString("Launcher", tftWidth / 2, tftHeight / 2 - 10, 1);
#else
  tft->drawCentreString("Launcher", tftWidth / 2, tftHeight / 2 - 10, 1);
#endif
  tft->setTextSize(FG);
  tft->setTextColor(FGCOLOR);

#ifdef E_PAPER_DISPLAY // epaper display draws only once
  tft->display(false);
  tft->startCallback();
#endif

END:
  delay(50);
#endif
}
/***************************************************************************************
** Function name: initDisplayLoop
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplayLoop()
{
  tft->fillScreen(BGCOLOR);
  initDisplay(true);
  delay(250);
  while (!check(AnyKeyPress))
  {
    initDisplay();
    delay(50);
  }
  returnToMenu = true;
}

/***************************************************************************************
** Function name: displayCurrentItem
** Description:   Display Item on Screen before instalation
***************************************************************************************/
void displayCurrentItem(JsonDocument doc, int currentIndex)
{
#ifdef E_PAPER_DISPLAY
  tft->stopCallback();
#endif
  JsonObject item = doc[currentIndex];

  const char *name = item["name"];
  const char *author = item["author"];

  // tft->fillScreen(BGCOLOR);
  tft->fillRect(0, tftHeight - 5, tftWidth, 5, BGCOLOR);
  tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
  tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);

  setTftDisplay(10, 10, FGCOLOR, FP);
  tft->print("Firmware: ");

  setTftDisplay(10, 22, ~BGCOLOR, FM, BGCOLOR);
  String name2 = String(name);
  tftprintln(name2, 10, 3);

  setTftDisplay(10, 22 + 4 * FM * 8, FGCOLOR);
  tft->print("by: ");
  tft->setTextColor(~BGCOLOR);
  String author2 = String(author).substring(0, 14);
  tftprintln(author2, 10);

  tft->setTextColor(FGCOLOR);
  tft->setTextSize(FM);
  tft->drawChar2(10, tftHeight - (10 + FM * 9), '<', FGCOLOR, BGCOLOR);
  tft->drawChar2(tftWidth - (10 + FM * 6), tftHeight - (10 + FM * 9), '>', FGCOLOR, BGCOLOR);
  tft->setTextSize(FP);
#if TFT_HEIGHT > 200
  String texto = "More information";

  tft->setTextColor(FGCOLOR);
  tft->drawCentreString(texto, tftWidth / 2, tftHeight - (10 + FM * 9), 1);

  texto = String(currentIndex + 1) + " of " + String(doc.size());
  tft->drawCentreString(texto, tftWidth / 2, tftHeight - (2 + FM * 9), 1);
  tft->drawRoundRect(tftWidth / 2 - (6 * 11), tftHeight - (10 + FM * 10), 12 * 11, 19, 3, FGCOLOR);
#else

  String texto = String(currentIndex + 1) + " of " + String(doc.size());
  setTftDisplay(int(tftWidth / 2 - 3 * texto.length()), tftHeight - (10 + FM * 6), FGCOLOR, FP, BGCOLOR);
  tft->println(texto);
#endif

#if defined(HAS_TOUCH)
  TouchFooter();
#endif
  int docsize = doc.size();
  if (docsize == 0)
    docsize = 1; // avoid division by zero
  int bar = int(tftWidth / (docsize));
  if (bar < 5)
    bar = 5;
  tft->fillRect((tftWidth * currentIndex) / docsize, tftHeight - 5, bar, 5, FGCOLOR);

#ifdef E_PAPER_DISPLAY
  tft->display(false);
  tft->startCallback();
#endif
}

/***************************************************************************************
** Function name: displayCurrentVersion
** Description:   Display Version on Screen before instalation
***************************************************************************************/
void displayCurrentVersion(String name, String author, String version, String published_at, int versionIndex, JsonArray versions)
{
#ifdef E_PAPER_DISPLAY
  tft->stopCallback();
#endif
  // tft->fillScreen(BGCOLOR);
  tft->fillRect(0, tftHeight - 5, tftWidth, 5, BGCOLOR);
  tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
  tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);

  setTftDisplay(10, 10, ~BGCOLOR, FM, BGCOLOR);
  String name2 = String(name);
  tftprintln(name2, 10, 2);
#if TFT_HEIGHT > 200
  setTftDisplay(10, 50, ALCOLOR, FM);
#endif
  tft->print("by: ");
  tft->setTextColor(~BGCOLOR);
  tft->println(String(author).substring(0, 14));

  tft->setTextColor(ALCOLOR);
  tft->setCursor(10, tft->getCursorY());
  tft->print("v: ");
  tft->setTextColor(~BGCOLOR);
  tft->println(String(version).substring(0, 15));

  tft->setTextColor(ALCOLOR);
  tft->setCursor(10, tft->getCursorY());
  tft->print("from: ");
  tft->setTextColor(~BGCOLOR);
  tft->println(String(published_at));

  if (versions.size() > 1)
  {
    tft->setTextColor(ALCOLOR);
    tft->drawChar2(10, tftHeight - (10 + FM * 9), '<', FGCOLOR, BGCOLOR);
    tft->drawChar2(tftWidth - (10 + FM * 6), tftHeight - (10 + FM * 9), '>', FGCOLOR, BGCOLOR);
    tft->setTextColor(~BGCOLOR);
  }

  setTftDisplay(-1, -1, ALCOLOR, FM, BGCOLOR);
  tft->drawCentreString("Options", tftWidth / 2, tftHeight - (10 + FM * 9), 1);
  tft->drawRoundRect(tftWidth / 2 - 3 * FM * 11, tftHeight - (12 + FM * 9), FM * 6 * 11, FM * 8 + 3, 3, ALCOLOR);

  int div = versions.size();
  if (div == 0)
    div = 1;

#if defined(HAS_TOUCH)
  TouchFooter(ALCOLOR);
#endif

  int bar = int(tftWidth / div);
  if (bar < 5)
    bar = 5;
  tft->fillRect((tftWidth * versionIndex) / div, tftHeight - 5, bar, 5, ALCOLOR);

#ifdef E_PAPER_DISPLAY
  tft->display(false);
  tft->startCallback();
#endif
}

/***************************************************************************************
** Function name: displayRedStripe
** Description:   Display Red Stripe with information
***************************************************************************************/
void displayRedStripe(String text, uint16_t fgcolor, uint16_t bgcolor)
{
  // save tft settings before showing the stripe
  int _size = tft->getTextsize();
  int _x = tft->getCursorX();
  int _y = tft->getCursorY();
  uint16_t _color = tft->getTextcolor();
  uint16_t _bgcolor = tft->getTextbgcolor();

  // stripe drwawing
  int size;
  if (text.length() * LW * FM < (tft->width() - 2 * FM * LW))
    size = FM;
  else
    size = FP;
  tft->fillRoundRect(10, tftHeight / 2 - 13, tftWidth - 20, 26, 7, bgcolor);
  tft->setTextColor(fgcolor, bgcolor);
  if (size == FM)
  {
    tft->setTextSize(FM);
    tft->setCursor(tftWidth / 2 - FM * 3 * text.length(), tftHeight / 2 - 8);
  }
  else
  {
    tft->setTextSize(FP);
    tft->setCursor(tftWidth / 2 - FP * 3 * text.length(), tftHeight / 2 - 8);
  }
  tft->println(text);

  // return previous tft settings
  tft->setTextSize(_size);
  tft->setTextColor(_color, _bgcolor);
  tft->setCursor(_x, _y);
}

/***************************************************************************************
** Function name: progressHandler
** Description:   Função para manipular o progresso da atualização
** Dependencia: prog_handler =>>    0 - Flash, 1 - SPIFFS
***************************************************************************************/
void progressHandler(int progress, size_t total)
{
#ifdef GxEPD2_DISPLAY
  static unsigned long lastUpdate = 0;
  tft->setFullWindow();
#endif
  int barWidth = map(progress, 0, total, 0, tftWidth - 40);
  if (progress == 0)
  {
    tft->setTextSize(FM);
    tft->setTextColor(ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
#if TFT_HEIGHT > 200
    tft->drawCentreString("-=Launcher=-", tftWidth / 2, 20, 1);
#else
    tft->drawCentreString("-=Launcher=-", tftWidth / 2, 10, 1);
#endif
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    if (prog_handler == 1)
    {
      tft->drawRect(18, tftHeight - 28, tftWidth - 36, 17, ALCOLOR);
      tft->fillRect(20, tftHeight - 26, tftWidth - 40, 13, BGCOLOR);
    }
    else
      tft->drawRect(18, tftHeight - 47, tftWidth - 36, 17, FGCOLOR);

    String txt;
    switch (prog_handler)
    {
    case 0:
      txt = "Installing FW";
      break;
    case 1:
      txt = "Installing SPIFFS";
      break;
    case 2:
      txt = "Downloading";
      break;
    }
    displayRedStripe(txt);
  }

  if (prog_handler == 1)
    tft->fillRect(20, tftHeight - 26, barWidth, 13, ALCOLOR);
  else
    tft->fillRect(20, tftHeight - 45, barWidth, 13, FGCOLOR);

#ifdef GxEPD2_DISPLAY
  if (millis() - lastUpdate > 3000)
  {
    tft->display();
    lastUpdate = millis();
  }
#endif
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Função para desenhar e mostrar as opçoes de contexto
***************************************************************************************/
#if defined(E_PAPER_DISPLAY) && !defined(GxEPD2_DISPLAY)
#define MAX_MENU_SIZE 13
#define FONT_S (FM * (LH + 3) + 4)
#else
#define FONT_S (FM * LH + 4)
#define MAX_MENU_SIZE (int)(tftHeight / 25)
#endif
Opt_Coord drawOptions(int idx, const std::vector<std::pair<std::string, std::function<void()>>> &fileList, std::vector<MenuOptions> &opt, uint16_t fgcolor, uint16_t bgcolor)
{
  int index = idx;
#ifdef E_PAPER_DISPLAY
  tft->stopCallback();
#endif

  Opt_Coord coord;
  opt.clear();
  int arraySize = fileList.size();
  int visibleCount = MAX_MENU_SIZE;
  int num_pages = 1 + arraySize / MAX_MENU_SIZE;
  static int show_page = 0;
  if (fileList.size() < MAX_MENU_SIZE)
    visibleCount = fileList.size();

#ifdef HAS_TOUCH
  if (arraySize <= MAX_MENU_SIZE)
    num_pages = 1;
  else
  {
    int remains = arraySize - (MAX_MENU_SIZE - 1);                                      // Primeira pagina tem MAX_MENU_SIZE-1 de tamanho, pois substitui o ultimo por "..."
    int nextPages = remains / (MAX_MENU_SIZE - 2);                                      // O restante do menu tem MAX_MENU_SIZE-2 de tamanho, pois substitui o primeiro e o ultimo por "..."
    int lastPage = (arraySize - remains - nextPages * (MAX_MENU_SIZE - 2)) > 0 ? 1 : 0; // A ultima pagina tem o restante do menu, que pode ser menor que MAX_MENU_SIZE-2
    num_pages = 1 + nextPages + lastPage;
    // Serial.printf("\rRemain items after 1st page: %d, nextPage: %d, lastPage: %d, arraySize: %d, Const: %d\n",
    //   remains, nextPages, lastPage, arraySize, MAX_MENU_SIZE);
  }
#endif
  int start = 0;
  if (num_pages > 1)
  {
    for (int i = 0; i <= num_pages; i++)
    { // check for the other pages
      int ini, end;
#ifdef HAS_TOUCH
      if (i == 0)
      {
        ini = 0;
        end = (MAX_MENU_SIZE - 1);
      }
      else
      {
        ini = 1 + i * (MAX_MENU_SIZE - 2);
        end = ini + (MAX_MENU_SIZE - 2);
      }
#else
      ini = i * MAX_MENU_SIZE; // index value at the top of the list, index starts at 0
      end = ini + MAX_MENU_SIZE;
#endif

      if (index >= ini && index < end)
      {
        start = ini;
        // Serial.printf("num_pages: %d, show_page: %d, index: %d\n", num_pages, i, index);
        // Serial.printf("ini: %d, end: %d\n", ini, end);
        if (index == ini || i != show_page)
          tft->fillRoundRect(tftWidth * 0.10, tftHeight / 2 - visibleCount * FONT_S / 2 - 5,
                             tftWidth * 0.8, FONT_S * visibleCount + 10, 5, bgcolor);
        show_page = i;
        break;
      }
    }
  }
  else if (index == 0)
  {
    tft->fillRoundRect(tftWidth * 0.10 + 1, tftHeight / 2 - visibleCount * FONT_S / 2 - 4,
                       tftWidth * 0.8 - 2, FONT_S * visibleCount + 8, 5, bgcolor);
  }

  int nchars = (tftWidth * 0.8 - 10) / (LW * FM) - 1;
  String txt = ">";
  int j = 0;
  int i = 0;
  int Max_items = MAX_MENU_SIZE;

  tft->setTextColor(fgcolor, bgcolor);
  tft->setTextSize(FM);
  tft->setCursor(tftWidth * 0.10 + 5, tftHeight / 2 - visibleCount * FONT_S / 2);

#ifdef HAS_TOUCH
  if (show_page == 0 && num_pages > 1)
  {
    Max_items = MAX_MENU_SIZE - 1;
  }
  else if (show_page > 0 && num_pages > 1)
  {
    Max_items = MAX_MENU_SIZE - 2;
  }
  if (show_page > 0)
  {
    opt.push_back(MenuOptions("", "-", nullptr, true, false, 10 + 5 * FM * LW, 0, tftWidth, FM * LH + 10));
    tft->setTextColor(ALCOLOR, BGCOLOR);
    txt = "..Page Up..";
    tft->drawCentreString(txt.substring(0, nchars), tftWidth / 2, tft->getCursorY(), 1);
    tft->println(); // add a new line to the line feeder
    j++;
  }
#endif

  while (i < arraySize && j < visibleCount)
  {
    if (i >= start)
    {
      uint16_t c_y = tft->getCursorY() + 4;
      MenuOptions optItem = MenuOptions(String(i), "", nullptr, true, false, tftWidth * 0.1, c_y - 4, tftWidth * 0.8, FM * LH + 4);
      tft->setCursor(tftWidth * 0.1, c_y);
      if (index == i)
      {
        optItem.selected = true;
        txt = ">";
        coord.x = tftWidth * 0.1 + FM * LW;
        coord.y = c_y;
        coord.size = nchars;
        coord.fgcolor = fgcolor;
        coord.bgcolor = bgcolor;
      }
      else
        txt = " ";
      txt += String(fileList[i].first.c_str()) + "                       ";
      tft->println(txt.substring(0, nchars));
      // Serial.println(txt.substring(0,nchars));
      //  tft->drawRect(optItem.x,optItem.y,optItem.w,optItem.h,BLUE); // debug purpose
      opt.push_back(optItem);
      j++;
    }
    i++;
    if (i == (start + Max_items))
    {
      // Serial.printf("Stopped at start+Max_items: %d + %d", start, Max_items);
      break;
    }
  }

#ifdef HAS_TOUCH
  if (num_pages != show_page + 1)
  {
    opt.push_back(MenuOptions("", "+", nullptr, true, false, 0, tft->getCursorY(), tftWidth, FM * LH + 6));
    txt = "..Page Down..";
    tft->drawCentreString(txt.substring(0, nchars), tftWidth / 2, tft->getCursorY() + 4, 1);
  }
#endif
  tft->drawRoundRect(tftWidth * 0.10, tftHeight / 2 - visibleCount * FONT_S / 2 - 5,
                     tftWidth * 0.8, FONT_S * visibleCount + 10, 5, fgcolor);

#ifdef E_PAPER_DISPLAY
  tft->display(false);
  tft->startCallback();
  delay(200);
#endif

  return coord;
}

/***************************************************************************************
** Function name: drawMainMenu
** Description:   Função para desenhar e mostrar o menu principal
***************************************************************************************/
void drawMainMenu(std::vector<MenuOptions> &opt, int index)
{
#ifdef E_PAPER_DISPLAY
  tft->stopCallback();
  tft->setFullWindow();
#endif
  uint8_t size = opt.size();
  if (size < 1)
  {
    displayRedStripe("No options available");
    return;
  }
  int cols = (tftHeight > 90) ? 3 : 5;             // Number of columns based on height
  int rows = (size + cols - 1) / cols;             // Calculate rows needed
  int w = (tftWidth - 16) / cols;                  // Width of each icon
  int h = (tftHeight - (26 + LH * FP + 6)) / rows; // Height of each icon

  int f_size = FG;
  if (tftHeight <= 135)
    f_size = FM;
  tft->setTextSize(f_size);

  for (int i = 0; i < size; ++i)
  {
    int col = i % cols;
    int row = i / cols;
    int y = 28 + row * h;
    int xOffset = 0;

    // Última linha incompleta: centralizar
    if (row == rows - 1 && (size % cols) != 0 && (size % cols) < cols)
    {
      int itemsInLastRow = size % cols;
      int totalWidthUsed = itemsInLastRow * w;
      xOffset = ((tftWidth - 16) - totalWidthUsed) / 2;
    }

    int x = 8 + xOffset + col * w;

    opt[i].x = x;
    opt[i].y = y;
    opt[i].w = w;
    opt[i].h = h;
    // Serial.printf("Menu Name: %s, x=%d, y=%d, w=%d, h=%d\n", opt[i].name, opt[i].x, opt[i].y, opt[i].w, opt[i].h); // Debug purpose

    if (i == index)
    {
      // Selected item
      tft->fillRoundRect(x + 6, y + 6, w - 6, h - 6, 5, DARKGREY);
      tft->fillRoundRect(x, y, w - 6, h - 6, 5, opt[i].active ? FGCOLOR : LIGHTGREY);
      tft->setTextColor(BGCOLOR, opt[i].active ? FGCOLOR : LIGHTGREY);
      // Draw text in the center of the icon
      tft->drawCentreString(opt[i].name, x + (w - 6) / 2, y + (h - 6) / 2 - LH * f_size / 2, 1);
    }
    else
    {
      // Non-selected item
      tft->drawRoundRect(x, y, w, h, 5, BGCOLOR);
      tft->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, BGCOLOR);
      tft->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5, BGCOLOR);
      tft->fillRoundRect(x + 3, y + 3, w - 6, h - 6, 5, BGCOLOR);
      tft->drawRoundRect(x + 3, y + 3, w - 6, h - 6, 5, opt[i].active ? FGCOLOR : DARKGREY);
      tft->setTextColor(opt[i].active ? FGCOLOR : DARKGREY, BGCOLOR);
      // Draw text in the center of the icon
      tft->drawCentreString(opt[i].name, x + w / 2, y + h / 2 - LH * f_size / 2, 1);
    }
    // tft->drawRect(opt[i].x,opt[i].y,opt[i].w,opt[i].h,BLUE); // debug purpose
  }

  tft->setTextSize(FP);
  tft->setTextColor(FGCOLOR, BGCOLOR);
  // Draw the description of the selected item
  tft->fillRect(10, tftHeight - (6 + LH * FP), tftWidth - 20, LH * FP, BGCOLOR);
  tft->drawCentreString(opt[index].text, tftWidth / 2, tftHeight - (6 + LH * FP), 1);
  // Draw Launcher version and battery value
#if TFT_HEIGHT < 200
  tft->drawString("Launcher", 12, 12);
#else
  tft->drawString("Launcher " + String(LAUNCHER), 12, 12);
#endif
  tft->setTextSize(f_size);
  drawDeviceBorder();
  int bat = getBattery();
  if (bat > 0)
    drawBatteryStatus(bat);
#ifdef E_PAPER_DISPLAY
  tft->display(false);
  tft->startCallback();
#endif
}
void drawDeviceBorder()
{
  tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
  tft->drawLine(5, 25, tftWidth - 6, 25, FGCOLOR);
}

void drawBatteryStatus(uint8_t bat)
{
  tft->drawRoundRect(tftWidth - 42, 7, 34, 17, 2, FGCOLOR);
  tft->setTextSize(FP);
  tft->setTextColor(FGCOLOR, BGCOLOR);
#if TFT_HEIGHT > 140 // Excludes Marauder Mini
  tft->drawRightString("  " + String(bat) + "%", tftWidth - 45, 12, 1);
#endif
  tft->fillRoundRect(tftWidth - 40, 9, 30, 13, 2, BGCOLOR);
  tft->fillRoundRect(tftWidth - 40, 9, 30 * bat / 100, 13, 2, FGCOLOR);
  tft->drawLine(tftWidth - 30, 9, tftWidth - 30, 9 + 13, BGCOLOR);
  tft->drawLine(tftWidth - 20, 9, tftWidth - 20, 9 + 13, BGCOLOR);
}

/***************************************************************************************
** Function name: listFiles
** Description:   Função para desenhar e mostrar o menu principal
***************************************************************************************/
#if defined(E_PAPER_DISPLAY) && !defined(GxEPD2_DISPLAY)
#define MAX_ITEMS 14
#else
#define MAX_ITEMS (int)((tftHeight - 20) / (LH * FM))
#endif
Opt_Coord listFiles(int index, String fileList[][3], std::vector<MenuOptions> &opt)
{

#ifdef E_PAPER_DISPLAY
  tft->stopCallback();
#endif
  Opt_Coord coord;
  opt.clear();
  tft->setCursor(10, 10);
  tft->setTextSize(FM);
  int i = 0;
  int arraySize = 0;
  while (fileList[arraySize][2] != "" && arraySize < MAXFILES)
    arraySize++;

  int num_pages = 0;
  static int show_page = 0;

  // calcula o numero de paginas
  if (arraySize <= MAX_ITEMS)
  {
    num_pages = 1;
  }
  else
  {
    uint8_t ofs = 0;
#ifdef HAS_TOUCH
    ofs = 1;
#endif
    int remains = arraySize - (MAX_ITEMS - ofs);
    int nextPages = remains / (MAX_ITEMS - 2 * ofs);
    int lastPage = (arraySize - remains - nextPages * (MAX_ITEMS - 2 * ofs)) > 0 ? 1 : 0;
    num_pages = 1 + nextPages + lastPage;
  }
  // Serial.printf("arraySize: %d, num_pages: %d, MAX_ITEMS: %d\n----------------------\n", arraySize, num_pages, MAX_ITEMS);
  int start = 0;
  if (num_pages > 1)
  {
    for (int i = 0; i <= num_pages; i++)
    { // check for the other pages
      int ini, end;
#ifdef HAS_TOUCH
      if (i == 0)
      {
        ini = 0;
        end = (MAX_ITEMS - 1);
      }
      else
      {
        ini = 1 + i * (MAX_ITEMS - 2);
        end = ini + (MAX_ITEMS - 2);
      }
#else
      ini = i * MAX_ITEMS; // index value at the top of the list, index starts at 0
      end = ini + MAX_ITEMS;
#endif

      // Serial.printf("\nnum_pages: %d, show_page: %d, index: %d\n", num_pages, i, index);
      // Serial.printf("ini: %d, end: %d\n", ini, end);
      if (index >= ini && index < end)
      {
        start = ini;
        if (index == ini || i != show_page)
          tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
        show_page = i;
        break;
      }
    }
  }

  int nchars = (tftWidth - 20) / (LW * FM);
  String txt = ">";
  int j = 0;
  int Max_items = MAX_ITEMS;
#ifdef HAS_TOUCH
  if (show_page == 0 && num_pages > 1)
  {
    Max_items = MAX_ITEMS - 1;
  }
  else if (show_page > 0 && num_pages > 1)
  {
    Max_items = MAX_ITEMS - 2;
  }
  if (show_page > 0)
  {
    opt.push_back(MenuOptions("", "-", nullptr, true, false, 10 + 5 * FM * LW, 0, tftWidth, FM * LH + 10));
    tft->setTextColor(ALCOLOR, BGCOLOR);
    tft->drawRightString("..Page Up..", tftWidth - 6, tft->getCursorY(), 1);
    tft->println("[ESC]");
  }
#endif

  while (i < arraySize)
  {
    if (i >= start && fileList[i][2] != "")
    {
      uint16_t c_y = tft->getCursorY();
      int first_offset = 0;
      tft->setCursor(10, c_y);

#ifdef HAS_TOUCH
      if (start == i && show_page == 0)
      {
        first_offset = 10 + 5 * LW * FM; // [ESC]
        tft->setTextColor(ALCOLOR, BGCOLOR);
        tft->print("[ESC]");
      }
#endif
      MenuOptions optItem = MenuOptions(String(i), "", nullptr, true, false, 0 + first_offset, c_y, tftWidth, FM * LH);
      if (fileList[i][2] == "folder")
        tft->setTextColor(FGCOLOR - 0x1111, BGCOLOR);
      else if (fileList[i][2] == "operator")
        tft->setTextColor(ALCOLOR, BGCOLOR);
      else
      {
        tft->setTextColor(FGCOLOR, BGCOLOR);
      }

      if (index == i)
      {
        optItem.selected = true;
        txt = ">";
        coord.x = 10 + FM * LW + (start == i ? 4 * FM * LW : 0);
        coord.y = c_y;
        coord.size = nchars - (start == i ? 4 : 0);
        coord.fgcolor = fileList[i][2] == "folder" ? FGCOLOR - 0x1111 : FGCOLOR;
        coord.bgcolor = BGCOLOR;
      }
      else
        txt = " ";
      txt += fileList[i][0] + "                       ";
      tft->println(txt.substring(0, nchars - (start == i ? 6 : 0)));
      opt.push_back(optItem);
      j++;
    }
    i++;
    if (i == (start + Max_items) || fileList[i][2] == "")
      break;
  }
#ifdef HAS_TOUCH
  if (num_pages != show_page + 1)
  {
    tft->setTextColor(ALCOLOR, BGCOLOR);
    opt.push_back(MenuOptions("", "+", nullptr, true, false, 0, tft->getCursorY(), tftWidth, FM * LH + 6));
    tft->drawCentreString("..Page Down..", tftWidth / 2, tft->getCursorY(), 1);
  }

#endif

#if defined(HAS_TOUCH)
  TouchFooter();
#endif
#ifdef E_PAPER_DISPLAY
  tft->display(false);
  tft->startCallback();
#endif
  return coord;
}

/*********************************************************************
**  Function: loopOptions
**  Where you choose among the options in menu
**********************************************************************/
void loopOptions(const std::vector<std::pair<std::string, std::function<void()>>> &options, bool bright)
{
  bool redraw = true;
  bool exit = false;
  int index = 0;
  log_i("Number of options: %d", options.size());
  int numOpt = options.size() - 1;
  Opt_Coord coord;
  std::vector<MenuOptions> list;
  int max_idx = 0;
  int min_idx = 255;
  LongPressTmp = millis();
  while (1)
  {
    if (redraw)
    {
      list = {};
      coord = drawOptions(index, options, list, ALCOLOR, BGCOLOR);
      max_idx = 0;
      min_idx = MAXFILES;
      int tmp = 0;
      for (auto item : list)
      {
        if (item.name != "")
        {
          tmp = item.name.toInt();
          // Serial.print(tmp); //Serial.print(" ");
          if (tmp > max_idx)
            max_idx = tmp;
          if (tmp < min_idx)
            min_idx = tmp;
        }
      }
      if (bright)
      {
        setBrightness(100 * (numOpt - index) / numOpt, false);
      }
      redraw = false;
    }
    String txt = options[index].first.c_str();
    displayScrollingText(txt, coord);

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
    if (touchPoint.pressed)
    {
      for (auto item : list)
      {
        if (item.contain(touchPoint.x, touchPoint.y))
        {
          resetGlobals();
          if (item.name == "")
          {
            if (item.text == "+")
              index = max_idx + 1;
            if (item.text == "-")
              index = min_idx - 1;
            if (index < 0)
              index = 0;
            // Serial.printf("\nPressed [%s], next index: %d\n",item.text,index);
            redraw = true;
            break;
          }
          else
          {
            if (index == item.name.toInt())
              SelPress = true;
            else
              redraw = true;
            index = item.name.toInt();
            break;
          }
        }
      }
      touchPoint.pressed = false;
    }
    if (check(PrevPress) || check(UpPress))
    {
      if (index == 0)
        index = options.size() - 1;
      else if (index > 0)
        index--;
      redraw = true;
    }
#else
    if (LongPress || PrevPress)
    {
      if (!LongPress)
      {
        LongPress = true;
        LongPressTmp = millis();
      }
      if (LongPress && millis() - LongPressTmp < 700)
      {
        if (!PrevPress)
        {
          AnyKeyPress = false;
          if (index == 0)
            index = options.size() - 1;
          else if (index > 0)
            index--;
          LongPress = false;
          redraw = true;
        }
        if (millis() - LongPressTmp > 200)
          tft->drawArc(tftWidth / 2, tftHeight / 2, 25, 15, 0, 360 * (millis() - (LongPressTmp + 200)) / 500, FGCOLOR - 0x1111);
        if (millis() - LongPressTmp > 700)
        { // longpress detected to exit
          LongPress = false;
          check(PrevPress);
          exit = true;
          break;
        }
        else
          goto WAITING;
      }
    }
#if defined(HAS_5_BUTTONS)
    if (check(UpPress))
    {
      if (index == 0)
        index = options.size() - 1;
      else if (index > 0)
        index--;
      redraw = true;
    }
#endif
#endif
  WAITING:
    /* DW Btn to next item */
    if (check(NextPress) || check(DownPress))
    {
      index++;
      if ((index + 1) > options.size())
        index = 0;
      redraw = true;
    }

    /* Select and run function */
    if (check(SelPress))
    {
      options[index].second();
      break;
    }

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
    if (check(EscPress) || returnToMenu || exit)
      break;
#else
    if (exit)
      break;
#endif
  }
  tft->fillScreen(BGCOLOR);
}

/*********************************************************************
**  Function: loopVersions
**  Where you choose which version to install/download **
**********************************************************************/
void loopVersions()
{
  JsonObject item = doc[currentIndex];

  int versionIndex = 0;
  const char *name = item["name"];
  const char *author = item["author"];
  JsonArray versions = item["versions"];
  bool redraw = true;

  LongPressTmp = millis();
  while (1)
  {
    if (returnToMenu)
      break; // Stops the loop to get back to Main menu

    JsonObject Version = versions[versionIndex];
    const char *version = Version["version"];
    const char *published_at = Version["published_at"];
    const char *file = Version["file"];
    bool spiffs = Version["s"].as<bool>();
    bool fat = Version["f"].as<bool>();
    bool fat2 = Version["f2"].as<bool>();
    bool nb = Version["nb"].as<bool>();
    uint32_t app_size = Version["as"].as<uint32_t>();
    uint32_t spiffs_size = Version["ss"].as<uint32_t>();
    uint32_t spiffs_offset = Version["so"].as<uint32_t>();
    uint32_t FAT_size[2] = {
        0,
        0,
    };
    uint32_t FAT_offset[2] = {
        0,
        0,
    };
    if (fat)
    {
      FAT_size[0] = Version["fs"].as<uint32_t>();
      FAT_offset[0] = Version["fo"].as<uint32_t>();
    }
    if (fat2)
    {
      FAT_size[1] = Version["fs2"].as<uint32_t>();
      FAT_offset[1] = Version["fo2"].as<uint32_t>();
    }
    if (redraw)
    {
      displayCurrentVersion(String(name), String(author), String(version), String(published_at), versionIndex, versions);
      redraw = false;
#ifdef E_PAPER_DISPLAY
      tft->display(false);
      delay(200);
#endif
    }
    /* DW Btn to next item */
    if (check(NextPress))
    {
      versionIndex++;
      if (versionIndex > versions.size() - 1)
        versionIndex = 0;
      redraw = true;
    }

    /* UP Btn go back to FW menu and ´<´ go to previous version item */

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
    /* UP Btn go to previous item */
    if (check(PrevPress))
    {
      versionIndex--;
      if (versionIndex < 0)
        versionIndex = versions.size() - 1;
      redraw = true;
    }

    if (check(EscPress))
    {
      goto SAIR;
    }
#else // Esc logic is holding previous btn fot 1 second +-
    if (LongPress || PrevPress)
    {
      if (!LongPress)
      {
        LongPress = true;
        LongPressTmp = millis();
      }
      if (LongPress && millis() - LongPressTmp < 800)
      {
      WAITING:
        vTaskDelay(10 / portTICK_PERIOD_MS);
        if (!PrevPress && millis() - LongPressTmp < 200)
        {
          AnyKeyPress = false;
          if (versionIndex == 0)
            versionIndex = versions.size() - 1;
          else if (versionIndex > 0)
            versionIndex--;
          LongPress = false;
          redraw = true;
        }
        if (!PrevPress && millis() - LongPressTmp > 200)
        {
          check(PrevPress);
          redraw = true;
          LongPress = false;
          goto EXIT_CHECK;
        }
        if (millis() - LongPressTmp > 200)
          tft->drawArc(tftWidth / 2, tftHeight / 2, 25, 15, 0, 360 * (millis() - (LongPressTmp + 200)) / 500, FGCOLOR - 0x1111);
        if (millis() - LongPressTmp > 700)
        { // longpress detected to exit
          returnToMenu = true;
          check(PrevPress);
          goto SAIR;
        }
        else
          goto WAITING;
      }
    EXIT_CHECK:
      yield();
    }

#endif

    /* Select to install */
    if (check(SelPress))
    {

      // Definição da matriz "Options"
      options = {
          {"OTA Install", [=]()
           { installFirmware(String(file), app_size, spiffs, spiffs_offset, spiffs_size, nb, fat, (uint32_t *)FAT_offset, (uint32_t *)FAT_size); }},
          {"Download->SD", [=]()
           { downloadFirmware(String(file), String(name) + "." + String(version).substring(0, 10), dwn_path); }},
          {"Back to List", [=]()
           { returnToMenu = true; }},
      };
      loopOptions(options);
      // On fail installing will run the following line
      redraw = true;
    }
  }
Sucesso:
  if (!returnToMenu)
    esp_restart();

// quando sair, redesenhar a tela
SAIR:
  if (!returnToMenu)
    displayCurrentItem(doc, currentIndex);
}

/*********************************************************************
**  Function: loopFirmware
**  Where you choose which Firmware to see more data
**********************************************************************/
void loopFirmware()
{
  LongPressTmp = millis();
  currentIndex = 0;
  displayCurrentItem(doc, currentIndex);

  while (1)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      /* UP Btn go to previous item */
      if (check(PrevPress))
      {
        if (currentIndex == 0)
          currentIndex = doc.size() - 1;
        else if (currentIndex > 0)
          currentIndex--;
        displayCurrentItem(doc, currentIndex);
#ifdef E_PAPER_DISPLAY
        tft->display(false);
        delay(200);
#endif
      }
      /* DW Btn to next item */
      if (check(NextPress))
      {
        currentIndex++;
        if ((currentIndex + 1) > doc.size())
          currentIndex = 0;
        displayCurrentItem(doc, currentIndex);
#ifdef E_PAPER_DISPLAY
        tft->display(false);
        delay(200);
#endif
      }

// Checks for long press to get back to Main Menu, only for StickCs.. Cardputer uses Esc btn
#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
      /* Select to install */
      if (check(SelPress))
      {
        loopVersions();
        delay(200);
        returnToMenu = false;
      }
#else
      if (LongPress || SelPress)
      {
        if (!LongPress)
        {
          LongPress = true;
          LongPressTmp = millis();
        }
        if (LongPress && millis() - LongPressTmp < 250)
          goto WAITING;
        LongPress = false;
        if (check(SelPress))
        {
          bool exit = false;
          options = {
              {"View version", [=]()
               { loopVersions(); }},
              {"Main Menu", [&]()
               { exit = true; }}};
          loopOptions(options);
          returnToMenu = false;
          if (exit)
          {
            returnToMenu = true;
            goto END;
          }
          displayCurrentItem(doc, currentIndex);
          delay(200);
        }
        else
        {
          check(SelPress);
          loopVersions(); // goes to the Version information
          displayCurrentItem(doc, currentIndex);
          delay(200);
          returnToMenu = false;
        }
      }
    WAITING:
      yield();
#endif

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
      if (check(EscPress))
        break; //  Esc btn to get back to Main Menu.
#endif

      if (returnToMenu)
        break; // break the loop and gets back to Main Menu
    }
    else
    {
      displayRedStripe("WiFi: Disconnected");
      delay(5000);
      break;
    }
  }
END:
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  doc.clear();
}

/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprintln(String txt, int margin, int numlines)
{
  int size = txt.length();
  if (numlines == 0)
    numlines = (tftHeight - 2 * margin) / (tft->getTextsize() * 8);
  int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
  int x = tft->getCursorX();
  int start = 0;
  while (size > 0 && numlines > 0)
  {
    if (tft->getCursorX() < margin)
      tft->setCursor(margin, tft->getCursorY());
    nchars = (tftWidth - tft->getCursorX() - margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
    tft->println(txt.substring(0, nchars));
    txt = txt.substring(nchars);
    size -= nchars;
    numlines--;
  }
}
/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprint(String txt, int margin, int numlines)
{
  int size = txt.length();
  if (numlines == 0)
    numlines = (tftHeight - 2 * margin) / (tft->getTextsize() * 8);
  int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
  int x = tft->getCursorX();
  int start = 0;
  bool prim = true;
  while (size > 0 && numlines > 0)
  {
    if (!prim)
    {
      tft->println();
    }
    if (tft->getCursorX() < margin)
      tft->setCursor(margin, tft->getCursorY());
    nchars = (tftWidth - tft->getCursorX() - margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
    tft->print(txt.substring(0, nchars));
    txt = txt.substring(nchars);
    size -= nchars;
    numlines--;
    prim = false;
  }
}

/***************************************************************************************
** Function name: getComplementaryColor
** Description:   Get simple complementary color in RGB565 format
***************************************************************************************/
uint16_t getComplementaryColor(uint16_t color)
{
  int r = 31 - ((color >> 11) & 0x1F);
  int g = 63 - ((color >> 5) & 0x3F);
  int b = 31 - (color & 0x1F);
  return (r << 11) | (g << 5) | b;
}
