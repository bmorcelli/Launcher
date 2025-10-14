#include "display.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
#include <cstring>
#include <globals.h>

#if defined(HEADLESS)
SerialDisplayClass *tft = new SerialDisplayClass();
#elif defined(E_PAPER_DISPLAY) || defined(USE_TFT_ESPI) || defined(USE_LOVYANGFX) ||                         \
    defined(GxEPD2_DISPLAY) || defined(USE_M5GFX)
Ard_eSPI *tft = new Ard_eSPI();
#else
#ifdef TFT_PARALLEL_8_BIT
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    TFT_DC, TFT_CS, TFT_WR, TFT_RD, TFT_D0, TFT_D1, TFT_D2, TFT_D3, TFT_D4, TFT_D5, TFT_D6, TFT_D7
);
#elif RGB_PANEL // 16-par connections
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
#if defined(DISPLAY_ST7262_PAR)
    ST7262_PANEL_CONFIG_DE_GPIO_NUM /* DE */, ST7262_PANEL_CONFIG_VSYNC_GPIO_NUM /* VSYNC */,
    ST7262_PANEL_CONFIG_HSYNC_GPIO_NUM /* HSYNC */, ST7262_PANEL_CONFIG_PCLK_GPIO_NUM /* PCLK */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B0 /* R0 */, // for ST7262 panels (SUNTON boards) R and B are changed
    ST7262_PANEL_CONFIG_DATA_GPIO_B1 /* R1 */, ST7262_PANEL_CONFIG_DATA_GPIO_B2 /* R2 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_B3 /* R3 */, ST7262_PANEL_CONFIG_DATA_GPIO_B4 /* R4 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G0 /* G0 */, ST7262_PANEL_CONFIG_DATA_GPIO_G1 /* G1 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G2 /* G2 */, ST7262_PANEL_CONFIG_DATA_GPIO_G3 /* G3 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_G4 /* G4 */, ST7262_PANEL_CONFIG_DATA_GPIO_G5 /* G5 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R0 /* B0 */, ST7262_PANEL_CONFIG_DATA_GPIO_R1 /* B1 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R2 /* B2 */, ST7262_PANEL_CONFIG_DATA_GPIO_R3 /* B3 */,
    ST7262_PANEL_CONFIG_DATA_GPIO_R4 /* B4 */, 0 /* hsync_polarity */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH /* hsync_front_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH /* hsync_pulse_width */,
    ST7262_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH /* hsync_back_porch */, 0 /* vsync_polarity */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_FRONT_PORCH /* vsync_front_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_PULSE_WIDTH /* vsync_pulse_width */,
    ST7262_PANEL_CONFIG_TIMINGS_VSYNC_BACK_PORCH /* vsync_back_porch */,
    ST7262_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG /* pclk_active_neg */, 16000000 /* prefer_speed */
#elif defined(DISPLAY_ST7701_PAR)
    ST7701_PANEL_CONFIG_DE_GPIO_NUM /* DE */, ST7701_PANEL_CONFIG_VSYNC_GPIO_NUM /* VSYNC */,
    ST7701_PANEL_CONFIG_HSYNC_GPIO_NUM /* HSYNC */, ST7701_PANEL_CONFIG_PCLK_GPIO_NUM /* PCLK */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R0 /* R0 */, ST7701_PANEL_CONFIG_DATA_GPIO_R1 /* R1 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R2 /* R2 */, ST7701_PANEL_CONFIG_DATA_GPIO_R3 /* R3 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_R4 /* R4 */, ST7701_PANEL_CONFIG_DATA_GPIO_G0 /* G0 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G1 /* G1 */, ST7701_PANEL_CONFIG_DATA_GPIO_G2 /* G2 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G3 /* G3 */, ST7701_PANEL_CONFIG_DATA_GPIO_G4 /* G4 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_G5 /* G5 */, ST7701_PANEL_CONFIG_DATA_GPIO_B0 /* B0 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B1 /* B1 */, ST7701_PANEL_CONFIG_DATA_GPIO_B2 /* B2 */,
    ST7701_PANEL_CONFIG_DATA_GPIO_B3 /* B3 */, ST7701_PANEL_CONFIG_DATA_GPIO_B4 /* B4 */,
    1 /* hsync_polarity */, ST7701_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH /* hsync_front_porch */,
    ST7701_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH /* hsync_pulse_width */,
    ST7701_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH /* hsync_back_porch */, 1 /* vsync_polarity */,
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
Ard_eSPI *tft = new Ard_eSPI(
    bus, TFT_RST, ROTATION, TFT_IPS, TFT_WIDTH, TFT_HEIGHT, TFT_COL_OFS1, TFT_ROW_OFS1, TFT_COL_OFS2,
    TFT_ROW_OFS2
);
#endif

/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord) {
    int len = text.length();
    static String displayText = "";
    static int i = 0;
    static long _lastmillis = 0;
    if (!displayText.startsWith(text)) i = 0;
    displayText = text + "        "; // Add spaces for smooth looping
    int scrollLen = len + 8;         // Full text plus space buffer
    tft->setTextColor(coord.fgcolor, coord.bgcolor);
    if (len < coord.size) {
        // Text fits within limit, no scrolling needed
        return;
    } else if (millis() > _lastmillis + 200) {
        String scrollingPart =
            displayText.substring(i, i + (coord.size - 1)); // Display charLimit characters at a time
        tft->fillRect(
            coord.x, coord.y, (coord.size - 1) * LW * tft->getTextsize(), LH * tft->getTextsize(), BGCOLOR
        ); // Clear display area
        tft->setCursor(coord.x, coord.y);
        tft->setCursor(coord.x, coord.y);
        tft->print(scrollingPart);
        if (i >= scrollLen - coord.size) i = -1; // Loop back
        _lastmillis = millis();
        i++;
        if (i == 1) _lastmillis = millis() + 1000;
    }
}

/***************************************************************************************
** Function name: resetTftDisplay
** Description:   set cursor to 0,0, screen and text to default color
***************************************************************************************/
void resetTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg, uint16_t screen) {
    tft->setCursor(x, y);
    tft->fillScreen(screen);
    tft->setTextSize(size);
    tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: setTftDisplay
** Description:   set cursor, font color, size and bg font color
***************************************************************************************/
void setTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg) {
    if (x >= 0 && y < 0) tft->setCursor(x, tft->getCursorY());      // if -1 on x, sets only y
    else if (x < 0 && y >= 0) tft->setCursor(tft->getCursorX(), y); // if -1 on y, sets only x
    else if (x >= 0 && y >= 0) tft->setCursor(x, y);                // if x and y > 0, sets both
    tft->setTextSize(size);
    tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color) {
    tft->drawRoundRect(5, tftHeight + 2, tftWidth - 10, (FM * LH + 4), 5, color);
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
void TouchFooter2(uint16_t color) {
    tft->drawRoundRect(5, tftHeight + 2, tftWidth - 10, (FM * LH + 4), 5, color);
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
void initDisplay(bool doAll) {
#ifndef HEADLESS
    static uint8_t _name = random(0, 3);
    String name = "@Pirata";
    String txt;
    int cor, _x, _y, show;

#ifdef E_PAPER_DISPLAY // epaper display draws only once
    static bool runOnce = false;
    if (runOnce) goto END;
    else runOnce = true;
    tft->stopCallback();
#endif

    if (_name == 1) name = "u/bmorcelli";
    else if (_name == 2) name = "gh/bmorcelli";
    tft->drawRoundRect(3, 3, tftWidth - 6, tftHeight - 6, 5, FGCOLOR);
    tft->setTextSize(FP);
    tft->setCursor(10, 10);
    cor = 0;
    show = random(0, 40);
    _x = tft->getCursorX();
    _y = tft->getCursorY();

    while (tft->getCursorY() < (tftHeight - (LH + 4))) {
        cor = random(0, 11);
        tft->setTextSize(FP);
        show = random(0, 40);
        if (show == 0 || doAll) {
            if (cor == 10) {
                txt = " ";
            } else if (cor & 1) {
                tft->setTextColor(odd_color, BGCOLOR);
                txt = String(cor);
            } else {
                tft->setTextColor(even_color, BGCOLOR);
                txt = String(cor);
            }

            if (_x >= (tftWidth - (LW * FP + 4))) {
                _x = 10;
                _y += LH * FP;
            } else if (_x < 10) {
                _x = 10;
            }
            if (_y >= (tftHeight - (LH * FP + LH * FP / 2))) break;
            tft->setCursor(_x, _y);
            if (_y > (tftHeight - (LH * FM + LH * FP / 2)) &&
                _x >= (tftWidth - ((LW * FP + 4) + LW * FP * name.length()))) {
                tft->setTextColor(FGCOLOR);
                tft->print(name);
                _x += LW * FP * name.length();
            } else {
                tft->print(txt);
                _x += LW * FP;
            }
        } else {
            if (_y > (tftHeight - (LH * FM + LH * FP / 2)) &&
                _x >= (tftWidth - ((LW * FP + 4) + LW * FP * name.length())))
                _x += LW * FP * name.length();
            else _x += LW * FP;

            if (_x >= (tftWidth - (LW * FP + 4))) {
                _x = 10;
                _y += LH * FP;
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
    vTaskDelay(50 / portTICK_PERIOD_MS);
#endif
}
/***************************************************************************************
** Function name: initDisplayLoop
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplayLoop() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    vTaskDelay(pdTICKS_TO_MS(250));
    while (!check(AnyKeyPress)) {
        initDisplay();
        vTaskDelay(pdTICKS_TO_MS(50));
    }
    returnToMenu = true;
}

/***************************************************************************************
** Function name: displayCurrentVersion
** Description:   Display Version on Screen before instalation
***************************************************************************************/
void displayCurrentVersion(
    String name, String author, String version, String published_at, int versionIndex, JsonArray versions
) {
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

    if (versions.size() > 1) {
        tft->setTextColor(ALCOLOR);
        tft->drawChar2(10, tftHeight - (10 + FM * 9), '<', FGCOLOR, BGCOLOR);
        tft->drawChar2(tftWidth - (10 + FM * 6), tftHeight - (10 + FM * 9), '>', FGCOLOR, BGCOLOR);
        tft->setTextColor(~BGCOLOR);
    }

    setTftDisplay(-1, -1, ALCOLOR, FM, BGCOLOR);
    tft->drawCentreString("Options", tftWidth / 2, tftHeight - (10 + FM * 9), 1);
    tft->drawRoundRect(
        tftWidth / 2 - 3 * FM * 11, tftHeight - (12 + FM * 9), FM * 6 * 11, FM * 8 + 3, 3, ALCOLOR
    );

    int div = versions.size();
    if (div == 0) div = 1;

#if defined(HAS_TOUCH)
    TouchFooter(ALCOLOR);
#endif

    int bar = int(tftWidth / div);
    if (bar < 5) bar = 5;
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
void displayRedStripe(String text, uint16_t fgcolor, uint16_t bgcolor) {
    // save tft settings before showing the stripe
    int _size = tft->getTextsize();
    int _x = tft->getCursorX();
    int _y = tft->getCursorY();
    uint16_t _color = tft->getTextcolor();
    uint16_t _bgcolor = tft->getTextbgcolor();

    // stripe drwawing
    int size;
    if (text.length() * LW * FM < (tft->width() - 2 * FM * LW)) size = FM;
    else size = FP;
    tft->fillRoundRect(10, tftHeight / 2 - (FM * LH / 2 + 5), tftWidth - 20, FM * LH + 10, 7, bgcolor);
    tft->setTextColor(fgcolor, bgcolor);
    if (size == FM) {
        tft->setTextSize(FM);
        tft->setCursor(tftWidth / 2 - FM * 3 * text.length(), tftHeight / 2 - FM * LH / 2);
    } else {
        tft->setTextSize(FP);
        tft->setCursor(tftWidth / 2 - FP * 3 * text.length(), tftHeight / 2 - FP * LH / 2);
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
void progressHandler(size_t progress, size_t total) {
#ifdef GxEPD2_DISPLAY
    static unsigned long lastUpdate = 0;
    tft->setFullWindow();
#endif
    double fraction = (double)progress / (double)total;
    double barWidthFloat = (tftWidth - 40) * fraction;
    size_t barWidth = static_cast<size_t>(barWidthFloat);
    // Serial.printf("Total: %d, Progress: %d, Progress bar width: %d \n", total, progress, barWidth);
    if (progress == 0) {
        tft->setTextSize(FM);
        tft->setTextColor(ALCOLOR);
        tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
#if TFT_HEIGHT > 200
        tft->drawCentreString("-=Launcher=-", tftWidth / 2, 20, 1);
#else
        tft->drawCentreString("-=Launcher=-", tftWidth / 2, 10, 1);
#endif
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
        if (prog_handler == 1) {
            tft->drawRect(18, tftHeight - 28, tftWidth - 36, 17, ALCOLOR);
            tft->fillRect(20, tftHeight - 26, tftWidth - 40, 13, BGCOLOR);
        } else tft->drawRect(18, tftHeight - 47, tftWidth - 36, 17, FGCOLOR);

        String txt;
        switch (prog_handler) {
            case 0: txt = "Installing FW"; break;
            case 1: txt = "Installing SPIFFS"; break;
            case 2: txt = "Downloading"; break;
        }
        displayRedStripe(txt);
    }

    if (prog_handler == 1) tft->fillRect(20, tftHeight - 26, barWidth, 13, ALCOLOR);
    else tft->fillRect(20, tftHeight - 45, barWidth, 13, FGCOLOR);

#ifdef GxEPD2_DISPLAY
    if (millis() - lastUpdate > 3000) {
        tft->display();
        lastUpdate = millis();
    }
#endif
    wakeUpScreen();
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Função para desenhar e mostrar as opçoes de contexto
***************************************************************************************/
Opt_Coord drawOptions(
    int idx, std::vector<Option> &opt, std::vector<MenuOptions> &t_menu, uint16_t fgcolor, uint16_t bgcolor,
    bool border
) {
    int index = idx;
#ifdef E_PAPER_DISPLAY
    tft->stopCallback();
#endif

    Opt_Coord coord;
    coord.bgcolor = bgcolor;
    coord.fgcolor = fgcolor;

    t_menu.clear();
    int arraySize = opt.size();
    if (arraySize == 0) { return coord; }

    if (index < 0) index = 0;
    if (index >= arraySize) index = arraySize - 1;

#ifdef E_PAPER_DISPLAY
    int lineHeight = FM * (LH + 3);
#else
    int lineHeight = FM * LH;
#endif
    const int rowSpacing = 4;
    const int paddingTop = 4;
    const int paddingBottom = 4;
    const int paddingSide = 4;

    int contentWidth = border ? static_cast<int>(tftWidth * 0.8f) : tftWidth - 4;
    if (contentWidth < 1) contentWidth = tftWidth;
    int boxX = border ? (tftWidth - contentWidth) / 2 : 2;

    int availableHeight = border ? (tftHeight - 20) : (tftHeight - 4);
    int minHeight = lineHeight + paddingTop + paddingBottom;
    if (availableHeight < minHeight) availableHeight = minHeight;

    int totalRows = (availableHeight - paddingTop - paddingBottom + rowSpacing) / (lineHeight + rowSpacing);
    if (totalRows < 1) totalRows = 1;

    int start = 0;
    int optionCount = 0;
    bool showPageUp = false;
    bool showPageDown = false;

#ifdef HAS_TOUCH
    struct PageInfo {
        int start;
        int count;
        bool pageUp;
        bool pageDown;
    };

    std::vector<PageInfo> pages;
    int remaining = arraySize;
    int pageStart = 0;
    while (remaining > 0) {
        bool hasUp = !pages.empty();
        int maxOptions = totalRows - (hasUp ? 1 : 0);
        if (maxOptions < 1) maxOptions = 1;
        int count = remaining < maxOptions ? remaining : maxOptions;
        bool hasDown = (remaining > count);
        if (hasDown) {
            int maxWithDown = totalRows - (hasUp ? 1 : 0) - 1;
            if (maxWithDown < 1) maxWithDown = 1;
            count = count < maxWithDown ? count : maxWithDown;
            if (count >= remaining) hasDown = false;
        }
        if (count < 1) { count = remaining < 1 ? remaining : 1; }

        pages.push_back({pageStart, count, hasUp, hasDown});
        pageStart += count;
        remaining -= count;
    }

    if (pages.empty()) { pages.push_back({0, 0, false, false}); }

    int currentPage = 0;
#ifdef HAS_TOUCH
    int maxRowsAcrossPages = 0;
#endif
    for (size_t p = 0; p < pages.size(); ++p) {
#ifdef HAS_TOUCH
        int rowsForPage = pages[p].count + (pages[p].pageUp ? 1 : 0) + (pages[p].pageDown ? 1 : 0);
        if (rowsForPage > maxRowsAcrossPages) maxRowsAcrossPages = rowsForPage;
#endif

        int pageStartIndex = pages[p].start;
        int pageEndIndex = pageStartIndex + pages[p].count;
        if (pages[p].count == 0 && index == 0) { currentPage = p; }
        if (index >= pageStartIndex && index < pageEndIndex) {
            currentPage = p;
            break;
        }
        if (p == pages.size() - 1) { currentPage = p; }
    }

    start = pages[currentPage].start;
    optionCount = pages[currentPage].count;
    showPageUp = pages[currentPage].pageUp;
    showPageDown = pages[currentPage].pageDown;

    if (optionCount == 0) { optionCount = arraySize < totalRows ? arraySize : totalRows; }
#else
    start = (index / totalRows) * totalRows;
    optionCount = arraySize < totalRows ? arraySize : totalRows;
#endif

    int rowsThisPage = optionCount + (showPageUp ? 1 : 0) + (showPageDown ? 1 : 0);
    if (rowsThisPage < 1) rowsThisPage = 1;

    int rowsForHeight = rowsThisPage;
#ifdef HAS_TOUCH
    rowsForHeight = rowsForHeight > maxRowsAcrossPages ? rowsForHeight : maxRowsAcrossPages;
#else
    rowsForHeight = rowsForHeight > optionCount ? rowsForHeight : optionCount;
#endif
    int contentHeight =
        paddingTop + paddingBottom + rowsForHeight * lineHeight + (rowsForHeight - 1) * rowSpacing;
    int boxY;
    if (border) {
        boxY = (tftHeight - contentHeight) / 2;
        if (boxY < 10) boxY = 10;
        if (boxY + contentHeight > tftHeight - 10) boxY = tftHeight - 10 - contentHeight;
        if (boxY < 10) boxY = 10;
        if (boxY < 0) boxY = 0;
    } else {
        boxY = 2;
        contentHeight = tftHeight - 4;
    }

    bool firstItemSelected = (optionCount > 0 && index == start);
    tft->setTextSize(FM);

    if (border) {
        if (firstItemSelected) tft->fillRoundRect(boxX, boxY, contentWidth, contentHeight, 5, bgcolor);
        tft->drawRoundRect(boxX, boxY, contentWidth, contentHeight, 5, fgcolor);
    } else {
        if (firstItemSelected) tft->fillRoundRect(3, 3, tftWidth - 6, tftHeight - 6, 5, bgcolor);
        tft->drawRoundRect(2, 2, tftWidth - 4, tftHeight - 4, 5, fgcolor);
    }

    int lineWidth = contentWidth - paddingSide * 2;
    if (lineWidth < 0) lineWidth = contentWidth;
    int charWidth = LW * tft->getTextsize();
    if (charWidth <= 0) charWidth = 1;
    int indicatorWidth = charWidth;
    if (indicatorWidth > lineWidth) indicatorWidth = lineWidth;
    int textStartY = boxY + paddingTop;
    int rowIndex = 0;

    auto addNavLine = [&](const char *text, bool isUp) {
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        int textWidth = strlen(text) * charWidth;
        int navX = boxX + paddingSide + 0 > ((lineWidth - textWidth) / 2) ? 0 : ((lineWidth - textWidth) / 2);
        tft->fillRect(boxX + paddingSide, rowTop, lineWidth, lineHeight, bgcolor);
        tft->setCursor(navX, rowTop);
        tft->setTextColor(ALCOLOR, bgcolor);
        tft->drawCentreString(text, tftWidth / 2, rowTop, 1);

        MenuOptions navItem("", isUp ? "-" : "+", nullptr, true, false);
        navItem.setCoords(boxX + paddingSide, rowTop, lineWidth, lineHeight + rowSpacing);
        t_menu.push_back(navItem);

        rowIndex++;
    };
#ifdef HAS_TOUCH
    if (showPageUp) { addNavLine("-- Page Up --", true); }
#endif
    for (int i = 0; i < optionCount && (start + i) < arraySize; ++i) {
        int optionIndex = start + i;
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        int rowLeft = boxX + paddingSide;
        if (i > 0) tft->fillRect(rowLeft, rowTop - rowSpacing, lineWidth, rowSpacing, bgcolor);
        int prefixWidth = 0;
        int cursorX = rowLeft;
#ifdef HAS_TOUCH
        bool showEscLabel = (!border && start == 0 && optionIndex == 0);
        if (showEscLabel) {
            tft->setCursor(cursorX, rowTop);
            tft->setTextColor(RED, bgcolor);
            tft->print("[ESC]");
            prefixWidth += 5 * charWidth;
            cursorX += 5 * charWidth;
        }
#endif

        tft->setCursor(cursorX, rowTop);
        tft->setTextColor(fgcolor, bgcolor);
        char indicatorChar = (optionIndex == index) ? '>' : ' ';
        tft->print(indicatorChar);
        prefixWidth += indicatorWidth;
        cursorX += indicatorWidth;

        uint16_t color = opt[optionIndex].color;
        if (color == NO_COLOR) color = fgcolor;

        int labelX = cursorX;
        int labelWidth = lineWidth - prefixWidth;
        if (labelWidth < 0) labelWidth = 0;
        int labelCharLimit = labelWidth / charWidth;
        if (labelCharLimit < 1) labelCharLimit = 1;

        char txt[labelCharLimit];
        snprintf(txt, sizeof(txt), "%-*s", labelCharLimit, opt[optionIndex].label.c_str());

        tft->setCursor(labelX, rowTop);
        tft->setTextColor(color, bgcolor);
        tft->print(txt);

        MenuOptions optItem(String(optionIndex), "", nullptr, true, optionIndex == index);
        optItem.setCoords(labelX, rowTop, 0 > labelWidth ? 0 : labelWidth, lineHeight + rowSpacing);
        t_menu.push_back(optItem);

        if (optionIndex == index) {
            coord.x = labelX;
            coord.y = rowTop;
            coord.size = labelCharLimit;
            coord.fgcolor = color;
            coord.bgcolor = bgcolor;
        }

        rowIndex++;
    }
#ifdef HAS_TOUCH
    if (showPageDown) { addNavLine("-- Page Down --", false); }
#endif
    if (rowIndex < rowsForHeight) {
        int rowLeft = boxX + paddingSide;
        while (rowIndex < rowsForHeight) {
            int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
            tft->fillRect(rowLeft, rowTop, lineWidth, lineHeight, bgcolor);
            tft->setCursor(rowLeft, rowTop);
            tft->setTextColor(fgcolor, bgcolor);
            tft->print(' ');
            rowIndex++;
        }
    }

#ifdef E_PAPER_DISPLAY
    tft->display(false);
    tft->startCallback();
    vTaskDelay(pdTICKS_TO_MS(200));
#endif

    return coord;
}
/***************************************************************************************
** Function name: drawMainMenu
** Description:   Função para desenhar e mostrar o menu principal
***************************************************************************************/
void drawMainMenu(std::vector<MenuOptions> &opt, int index) {
#ifdef E_PAPER_DISPLAY
    tft->stopCallback();
    tft->setFullWindow();
#endif
    uint8_t size = opt.size();
    if (size < 1) {
        displayRedStripe("No options available");
        return;
    }
    int cols = (tftHeight > 90) ? 3 : 5;                                // Number of columns based on height
    int rows = (size + cols - 1) / cols;                                // Calculate rows needed
    int w = (tftWidth - 16) / cols;                                     // Width of each icon
    int h = (tftHeight - ((6 + 6 + FP * LH + 6) + LH * FP + 6)) / rows; // Height of each icon

    int f_size = FG;
    if (tftHeight <= 135) f_size = FM;
    tft->setTextSize(f_size);

    for (int i = 0; i < size; ++i) {
        int col = i % cols;
        int row = i / cols;
        int y = (6 + 6 + FP * LH + 8) + row * h;
        int xOffset = 0;

        // Última linha incompleta: centralizar
        if (row == rows - 1 && (size % cols) != 0 && (size % cols) < cols) {
            int itemsInLastRow = size % cols;
            int totalWidthUsed = itemsInLastRow * w;
            xOffset = ((tftWidth - 16) - totalWidthUsed) / 2;
        }

        int x = 8 + xOffset + col * w;

        opt[i].x = x;
        opt[i].y = y;
        opt[i].w = w;
        opt[i].h = h;
        // Serial.printf("Menu Name: %s, x=%d, y=%d, w=%d, h=%d\n", opt[i].name, opt[i].x, opt[i].y, opt[i].w,
        // opt[i].h); // Debug purpose

        if (i == index) {
            // Selected item
            tft->fillRoundRect(x + 6, y + 6, w - 6, h - 6, 5, DARKGREY);
            tft->fillRoundRect(x, y, w - 6, h - 6, 5, opt[i].active ? FGCOLOR : LIGHTGREY);
            tft->setTextColor(BGCOLOR, opt[i].active ? FGCOLOR : LIGHTGREY);
            // Draw text in the center of the icon
            tft->drawCentreString(opt[i].name, x + (w - 6) / 2, y + (h - 6) / 2 - LH * f_size / 2, 1);
        } else {
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
    if (bat > 0) drawBatteryStatus(bat);
#ifdef E_PAPER_DISPLAY
    tft->display(false);
    tft->startCallback();
#endif
}
void drawDeviceBorder() {
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    tft->drawLine(5, (6 + 6 + FP * LH + 5), tftWidth - 6, (6 + 6 + FP * LH + 5), FGCOLOR);
}

void drawBatteryStatus(uint8_t bat) {
    tft->drawRoundRect(tftWidth - 42, 7, 34, FP * LH + 9, 2, FGCOLOR);
    tft->setTextSize(FP);
    tft->setTextColor(FGCOLOR, BGCOLOR);
#if TFT_HEIGHT > 140 // Excludes Marauder Mini
    tft->drawRightString("  " + String(bat) + "%", tftWidth - 45, 12, 1);
#endif
    tft->fillRoundRect(tftWidth - 40, 9, 30, FP * LH + 5, 2, BGCOLOR);
    tft->fillRoundRect(tftWidth - 40, 9, 30 * bat / 100, FP * LH + 5, 2, FGCOLOR);
    tft->drawLine(tftWidth - 30, 9, tftWidth - 30, 9 + FP * LH + 6, BGCOLOR);
    tft->drawLine(tftWidth - 20, 9, tftWidth - 20, 9 + FP * LH + 6, BGCOLOR);
}

/*********************************************************************
**  Function: loopOptions
**  Where you choose among the options in menu
**********************************************************************/
int loopOptions(std::vector<Option> &options, bool bright, uint16_t al, uint16_t bg, bool border, int index) {
    bool redraw = true;
    bool exit = false;
    log_i("Number of options: %d", options.size());
    int numOpt = options.size() - 1;
    Opt_Coord coord;
    std::vector<MenuOptions> list;
    int max_idx = 0;
    int min_idx = 255;
    LongPressTmp = millis();
    while (1) {
        if (redraw) {
            list = {};
            coord = drawOptions(index, options, list, al, bg, border);
            max_idx = 0;
            min_idx = MAXFILES;
            int tmp = 0;
            for (auto item : list) {
                if (item.name != "") {
                    tmp = item.name.toInt();
                    // Serial.print(tmp); //Serial.print(" ");
                    if (tmp > max_idx) max_idx = tmp;
                    if (tmp < min_idx) min_idx = tmp;
                }
            }
            if (bright) { setBrightness(100 * (numOpt - index) / numOpt, false); }
            redraw = false;
        }
        String txt = options[index].label;
        displayScrollingText(txt, coord);

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            for (auto item : list) {
                if (item.contain(touchPoint.x, touchPoint.y)) {
                    resetGlobals();
                    if (item.name == "") {
                        if (item.text == "+") index = max_idx + 1;
                        if (item.text == "-") index = min_idx - 1;
                        if (index < 0) index = 0;
                        // Serial.printf("\nPressed [%s], next index: %d\n",item.text,index);
                        redraw = true;
                        break;
                    } else {
                        if (index == item.name.toInt()) SelPress = true;
                        else redraw = true;
                        index = item.name.toInt();
                        break;
                    }
                }
            }
            touchPoint.pressed = false;
        }
#endif
        if (check(PrevPress) || check(UpPress)) {
            if (index == 0) index = options.size() - 1;
            else if (index > 0) index--;
            redraw = true;
        }
#else
        if (LongPress || PrevPress) {
            if (!LongPress) {
                LongPress = true;
                LongPressTmp = millis();
            }
            if (LongPress && millis() - LongPressTmp < 700) {
                if (!PrevPress) {
                    AnyKeyPress = false;
                    if (index == 0) index = options.size() - 1;
                    else if (index > 0) index--;
                    LongPress = false;
                    redraw = true;
                }
                if (millis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (millis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111
                    );
                if (millis() - LongPressTmp > 700) { // longpress detected to exit
                    LongPress = false;
                    check(PrevPress);
                    exit = true;
                    break;
                } else goto WAITING;
            }
        }
#if defined(HAS_5_BUTTONS)
        if (check(UpPress)) {
            if (index == 0) index = options.size() - 1;
            else if (index > 0) index--;
            redraw = true;
        }
#endif
#endif
    WAITING:
        /* DW Btn to next item */
        if (check(NextPress) || check(DownPress)) {
            index++;
            if ((index + 1) > options.size()) index = 0;
            redraw = true;
        }

        /* Select and run function */
        if (check(SelPress)) {
            options[index].operation();
            break;
        }

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
        if (check(EscPress) || returnToMenu || exit) return -1;
#else
        if (exit) break;
#endif
    }
    if (border) tft->fillScreen(BGCOLOR);
#if defined(HAS_TOUCH)
    TouchFooter(FGCOLOR);
#endif
    return index;
}

/*********************************************************************
**  Function: loopVersions
**  Where you choose which version to install/download **
**********************************************************************/
void loopVersions(String _fid) {
    JsonDocument item = getVersionInfo(_fid);

    int versionIndex = 0;
    const char *name = item["name"];
    const char *author = item["author"];
    const char *fid = item["fid"];
    const bool star = item["star"].as<bool>();
    JsonArray versions = item["versions"];
    bool redraw = true;

    LongPressTmp = millis();
    while (1) {
        if (returnToMenu) break; // Stops the loop to get back to Main menu

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
        if (fat) {
            FAT_size[0] = Version["fs"].as<uint32_t>();
            FAT_offset[0] = Version["fo"].as<uint32_t>();
        }
        if (fat2) {
            FAT_size[1] = Version["fs2"].as<uint32_t>();
            FAT_offset[1] = Version["fo2"].as<uint32_t>();
        }
        if (redraw) {
            displayCurrentVersion(
                String(name), String(author), String(version), String(published_at), versionIndex, versions
            );
            redraw = false;
#ifdef E_PAPER_DISPLAY
            tft->display(false);
            vTaskDelay(pdTICKS_TO_MS(200));
#endif
        }
        /* DW Btn to next item */
        if (check(NextPress)) {
            versionIndex++;
            if (versionIndex > versions.size() - 1) versionIndex = 0;
            redraw = true;
        }

        /* UP Btn go back to FW menu and ´<´ go to previous version item */

#if defined(T_EMBED) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
        /* UP Btn go to previous item */
        if (check(PrevPress)) {
            versionIndex--;
            if (versionIndex < 0) versionIndex = versions.size() - 1;
            redraw = true;
        }

        if (check(EscPress)) { goto SAIR; }
#else // Esc logic is holding previous btn fot 1 second +-
        if (LongPress || PrevPress) {
            if (!LongPress) {
                LongPress = true;
                LongPressTmp = millis();
            }
            if (LongPress && millis() - LongPressTmp < 800) {
            WAITING:
                vTaskDelay(10 / portTICK_PERIOD_MS);
                if (!PrevPress && millis() - LongPressTmp < 200) {
                    AnyKeyPress = false;
                    if (versionIndex == 0) versionIndex = versions.size() - 1;
                    else if (versionIndex > 0) versionIndex--;
                    LongPress = false;
                    redraw = true;
                }
                if (!PrevPress && millis() - LongPressTmp > 200) {
                    check(PrevPress);
                    redraw = true;
                    LongPress = false;
                    goto EXIT_CHECK;
                }
                if (millis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (millis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111
                    );
                if (millis() - LongPressTmp > 700) { // longpress detected to exit
                    returnToMenu = true;
                    check(PrevPress);
                    goto SAIR;
                } else goto WAITING;
            }
        EXIT_CHECK:
            yield();
        }

#endif

        /* Select to install */
        if (check(SelPress)) {

            // Definição da matriz "Options"
            options = {
                {"OTA Install", [=]() {
                     installFirmware(
                         String(fid),
                         String(file),
                         app_size,
                         spiffs,
                         spiffs_offset,
                         spiffs_size,
                         nb,
                         fat,
                         (uint32_t *)FAT_offset,
                         (uint32_t *)FAT_size
                     );
                 }}
            };
            if (sdcardMounted) {
                options.push_back({"Download->SD", [=]() {
                                       downloadFirmware(
                                           String(fid),
                                           String(file),
                                           String(name) + "." + String(version).substring(0, 10),
                                           dwn_path
                                       );
                                   }});
                options.push_back({"Add to Favorite", [=] {
                                       JsonObject fav = favorite.add<JsonObject>();
                                       fav["name"] = String(name) + "-" + String(author);
                                       fav["fid"] = _fid;
                                       fav["link"] = "";
                                       saveConfigs();
                                   }});
            }
            options.push_back({"Back to List", [=]() { returnToMenu = true; }});

            loopOptions(options);
            // On fail installing will run the following line
            redraw = true;
        }
    }
Sucesso:
    if (!returnToMenu) esp_restart();

// quando sair, redesenhar a tela
SAIR:
    if (!returnToMenu) tft->fillScreen(BGCOLOR);
}

/*********************************************************************
**  Function: loopFirmware
**  Where you choose which Firmware to see more data
**********************************************************************/
void loopFirmware() {
    int _page = current_page;
    String order_by = "downloads";
    String query = "";
    bool star = false;
    bool refine = false;
    int index = 0;

RESTART:
    currentIndex = -1;
    if (_page != current_page || refine) {
        GetJsonFromLauncherHub(current_page, order_by, star, query);
        index = 1;
    }
    options = {};
    int items = doc["page_size"].as<int>();
    int page = doc["page"].as<int>();
    if (total_firmware < (page * items)) {
        if (page == 1) items = total_firmware;
        else items = total_firmware - items * (page - 1);
    }
    options.push_back({"[Refine Search]", [&]() { refine = true; }, ALCOLOR});

    if (current_page > 1) {
        // Volta uma página
        options.push_back({"[Previous Page]", [=]() { current_page -= 1; }, ALCOLOR});
    }
    for (int i = 0; i < items; i++) {
        bool stared = doc["items"][i]["star"].as<bool>();
        String txt =
            doc["items"][i]["name"].as<String>() + " (" + doc["items"][i]["author"].as<String>() + ")";
        options.push_back({txt, [=]() { currentIndex = i; }, stared ? FGCOLOR - 0x1111 : FGCOLOR});
    };
    if (total_firmware > doc["page_size"].as<int>() * current_page) {
        // Avança uma pagina
        options.push_back({"[Next Page]", [=]() { current_page += 1; }, ALCOLOR});
    }
    options.push_back({"[Main Menu]", [=]() { returnToMenu = true; }, ALCOLOR});

    tft->fillScreen(BGCOLOR);
    index = loopOptions(options, false, FGCOLOR, BGCOLOR, false, index);
    if (currentIndex >= 0) loopVersions(doc["items"][currentIndex]["fid"].as<String>());
    if (refine) {
        refine = false;
        std::vector<Option> opt = {
            {order_by == "downloads" ? "Order by name" : "Order by downloads",
             [&]() {
                 order_by = order_by == "downloads" ? "name" : "downloads";
                 refine = true;
             }                                                                                          },
            {star == true ? "Starred -> Off" : "Starred -> On",
             [&]() {
                 star = !star;
                 refine = true;
             }                                                                                          },
            {"Text Search",
             [&]() {
                 String _q = keyboard(query, 76, "Search Firmware");
                 if (_q != String(KEY_ESCAPE)) query = _q;
                 refine = true;
             }                                                                                          },
            {"Back to list",                                                   [&]() { refine = false; }}
        };
        loopOptions(opt);
    }
    if (!returnToMenu && index >= 0) goto RESTART;
    doc.clear();
}

/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprintln(String txt, int margin, int numlines) {
    int size = txt.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextsize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    while (size > 0 && numlines > 0) {
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
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
void tftprint(String txt, int margin, int numlines) {
    int size = txt.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextsize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    bool prim = true;
    while (size > 0 && numlines > 0) {
        if (!prim) { tft->println(); }
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextsize()); // 6 pixels of width fot a letter size 1
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
uint16_t getComplementaryColor(uint16_t color) {
    int r = 31 - ((color >> 11) & 0x1F);
    int g = 63 - ((color >> 5) & 0x3F);
    int b = 31 - (color & 0x1F);
    return (r << 11) | (g << 5) | b;
}
