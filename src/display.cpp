#include "display.h"
#include "app_registry.h"
#include "cardkb2.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include "settings.h"
#include "utils.h"
#include <cstring>
#include <globals.h>
#include <vector>

// The panel, its data bus and every pin come from the TFT_* / USE_* build flags
// in boards/<board>/platformio.ini — DisplayDrivers builds the whole chain from
// them. See the library's README for the flag vocabulary.
tft_display *tft = new tft_display();

/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord) {
    int len = text.length();
    static String displayText = "";
    static int i = 0;
    static long _lastmillis = 0;
#if defined(E_PAPER_DISPLAY)
    const int deadTime = 1500;
#else
    const int deadTime = 200;
#endif
    if (!displayText.startsWith(text)) i = 0;
    displayText = text + "        "; // Add spaces for smooth looping
    int scrollLen = len + 8;         // Full text plus space buffer
    tft->setTextColor(coord.fgcolor, coord.bgcolor);
    if (len < coord.size) {
        // Text fits within limit, no scrolling needed
        return;
    } else if (launcherMillis() > _lastmillis + deadTime) {
        String scrollingPart =
            displayText.substring(i, i + (coord.size - 1)); // Display charLimit characters at a time
        tft->fillRect(
            coord.x, coord.y, (coord.size - 1) * LW * tft->getTextSize(), LH * tft->getTextSize(), BGCOLOR
        ); // Clear display area
        tft->setCursor(coord.x, coord.y);
        tft->setCursor(coord.x, coord.y);
        tft->print(scrollingPart);
        if (i >= scrollLen - coord.size) i = -1; // Loop back
        _lastmillis = launcherMillis();
        i++;
        if (i == 1) _lastmillis = launcherMillis() + 1000;
        tft->display(false);
    }
}

/***************************************************************************************
** Function name: drawCharAt
** Description:   one character in explicit colours, current text colours kept
***************************************************************************************/
void drawCharAt(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg) {
    const uint16_t oldFg = tft->getTextColor();
    const uint16_t oldBg = tft->getTextBgColor();
    tft->setTextColor(fg, bg);
    tft->drawString(String(c), x, y);
    tft->setTextColor(oldFg, oldBg);
}

static inline void drawOptionsErase(const Opt_Coord &coord) {
    if (coord.boxW == 0 || coord.boxH == 0) return;
    tft->fillRect(coord.boxX, coord.boxY, coord.boxW, coord.boxH, coord.bgcolor);
    tft->display(false);
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
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
    tft->drawRoundRect(5 + RES, tftHeight + 2, tftWidth - 10 - 2 * RES, (_fm * LH + 4), 5, color);
    tft->setTextColor(color);
    tft->setTextSize(_fm);
    tft->drawString("<<<", 11 + RES, tftHeight + 4);
    tft->drawCentreString("SEL", tftWidth / 2, tftHeight + 4, 1);
    tft->drawRightString(">>>", tftWidth - (RES + 11), tftHeight + 4, 1);
#endif
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter2(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
    tft->drawRoundRect(5 + RES, tftHeight + 2, tftWidth - 10 - 2 * RES, (_fm * LH + 4), 5, color);
    tft->setTextColor(color);
    tft->setTextSize(_fm);
    tft->drawString("<<", 11 + RES, tftHeight + 4);
    tft->drawCentreString("LAUNCHER", tftWidth / 2, tftHeight + 4, 1);
    tft->drawRightString(">>", tftWidth - (RES + 11), tftHeight + 4, 1);
#endif
}

/***************************************************************************************
** Function name: BootScreen
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplay(bool doAll) {
#ifndef HEADLESS
    static uint8_t _name = launcherRandom(0, 3);
    String name = "@Pirata";
    String txt;
    int cor, _x, _y, show;

#ifdef E_PAPER_DISPLAY // epaper display draws only once
    static bool runOnce = false;
    static long lastMillis = 0;
    if (runOnce && launcherMillis() - lastMillis < 5000) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        return;
    } else {
        runOnce = true;
        lastMillis = launcherMillis();
    }
#endif

    if (_name == 1) name = "u/bmorcelli";
    else if (_name == 2) name = "gh/bmorcelli";
    tft->drawRoundRect(3, 3, tftWidth - 6, tftHeight - 6, 5, FGCOLOR);

    int matrixTopMargin = 10;
#if defined(HAS_TOUCH) || defined(HAS_KEYBOARD) || defined(USE_CARDKB2)
    std::vector<MenuOptions> &bootAppShortcuts = launcherBootAppShortcuts();
    if (!bootAppShortcuts.empty()) matrixTopMargin = drawBootAppShortcuts(bootAppShortcuts) + 2;
#endif

    tft->setTextSize(_fp);
    tft->setCursor(10, matrixTopMargin);
    cor = 0;
    show = launcherRandom(0, 40);
    _x = tft->getCursorX();
    _y = tft->getCursorY();

#if defined(E_PAPER_DISPLAY)
    // On e-paper, printing one character at a time (doAll fills every cell) is far more
    // draw calls than the panel needs — the per-character odd/even colour is not worth it
    // here anyway, so accumulate each row and push it as a single string.
    bool batchRow = doAll;
    String rowBuffer;
    int rowBufferY = _y;
#endif

    while (tft->getCursorY() < (tftHeight - (LH + 4))) {
        cor = launcherRandom(0, 11);
        tft->setTextSize(_fp);
        show = launcherRandom(0, 40);
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

            if (_x >= (tftWidth - (LW * _fp + 4))) {
#if defined(E_PAPER_DISPLAY)
                if (batchRow && rowBuffer.length() > 0) {
                    tft->setTextColor(FGCOLOR, BGCOLOR);
                    tft->drawString(rowBuffer, 10, rowBufferY);
                    rowBuffer = "";
                }
#endif
                _x = 10;
                _y += LH * _fp;
#if defined(E_PAPER_DISPLAY)
                rowBufferY = _y;
#endif
            } else if (_x < 10) {
                _x = 10;
            }
            if (_y >= (tftHeight - (LH * _fp + LH * _fp / 2))) break;
            tft->setCursor(_x, _y);
            if (_y > (tftHeight - (LH * _fm + LH * _fp / 2)) &&
                _x >= (tftWidth - ((LW * _fp + 4) + LW * _fp * name.length()))) {
#if defined(E_PAPER_DISPLAY)
                if (batchRow) rowBuffer += name;
                else
#endif
                {
                    tft->setTextColor(FGCOLOR);
                    tft->print(name);
                }
                _x += LW * _fp * name.length();
            } else {
#if defined(E_PAPER_DISPLAY)
                if (batchRow) rowBuffer += txt;
                else
#endif
                    tft->print(txt);
                _x += LW * _fp;
            }
        } else {
            if (_y > (tftHeight - (LH * _fm + LH * _fp / 2)) &&
                _x >= (tftWidth - ((LW * _fp + 4) + LW * _fp * name.length())))
                _x += LW * _fp * name.length();
            else _x += LW * _fp;

            if (_x >= (tftWidth - (LW * _fp + 4))) {
                _x = 10;
                _y += LH * _fp;
                ALIVIATE_TASK;
            }
        }
        tft->setCursor(_x, _y);
    }
#if defined(E_PAPER_DISPLAY)
    if (batchRow && rowBuffer.length() > 0) {
        tft->setTextColor(FGCOLOR, BGCOLOR);
        tft->drawString(rowBuffer, 10, rowBufferY);
    }
#endif
    tft->setTextSize(_fg);
    tft->setTextColor(FGCOLOR);
    // Both arms of the old #if TFT_HEIGHT > 200 here were the same line.
    tft->drawCentreString("Launcher", tftWidth / 2, tftHeight / 2 - 10, 1);
    tft->setTextSize(_fg);
    tft->setTextColor(FGCOLOR);

    String selectedAppName = launcherSelectedBootAppName();
    if (!selectedAppName.isEmpty()) {
        selectedAppName = selectedAppName.substring(0, tftWidth / (_fm * LW) - 4);
        tft->setTextSize(_fm);
        tft->setTextColor(FGCOLOR, BGCOLOR);
        int appTextY = tftHeight - (1.5 * (_fm * LH) + 10);
        tft->drawCentreString(" " + selectedAppName + " ", tftWidth / 2, appTextY, 1);
    }

    if (doAll) TouchFooter2();
    tft->display(false);
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
    const String &name, const String &author, const String &version, const String &published_at,
    int versionIndex, JsonArray versions
) {
    // tft->fillScreen(BGCOLOR);
    tft->fillRect(0, tftHeight - 5, tftWidth, 5, BGCOLOR);
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);

    setTftDisplay(10, 10, ~BGCOLOR, _fm, BGCOLOR);
    String name2 = String(name);
    tftprintln(name2, 10, 2);
    // Tall panels have room to put the author on its own line.
    if (panelHeight() > 200) setTftDisplay(10, 50, ALCOLOR, _fm);
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
        drawCharAt(10, tftHeight - (10 + _fm * 9), '<', FGCOLOR, BGCOLOR);
        drawCharAt(tftWidth - (10 + _fm * 6), tftHeight - (10 + _fm * 9), '>', FGCOLOR, BGCOLOR);
        tft->setTextColor(~BGCOLOR);
    }

    setTftDisplay(-1, -1, ALCOLOR, _fm, BGCOLOR);
    tft->drawCentreString("Options", tftWidth / 2, tftHeight - (10 + _fm * 9), 1);
    tft->drawRoundRect(
        tftWidth / 2 - 3 * _fm * 11, tftHeight - (12 + _fm * 9), _fm * 6 * 11, _fm * 8 + 3, 3, ALCOLOR
    );

    int div = versions.size();
    if (div == 0) div = 1;

    TouchFooter(ALCOLOR);

    int bar = int(tftWidth / div);
    if (bar < 5) bar = 5;
    tft->fillRect((tftWidth * versionIndex) / div, tftHeight - 5, bar, 5, ALCOLOR);

    tft->display(false);
}

/***************************************************************************************
** Function name: displayRedStripe
** Description:   Display Red Stripe with information
***************************************************************************************/
void displayRedStripe(const String &text, uint16_t fgcolor, uint16_t bgcolor, bool keepAwake) {
    // save tft settings before showing the stripe
    int _size = tft->getTextSize();
    int _x = tft->getCursorX();
    int _y = tft->getCursorY();
    uint16_t _color = tft->getTextColor();
    uint16_t _bgcolor = tft->getTextBgColor();
    Serial.println(String("Display Red Stripe: ") + text);
    // A stripe is put up to be read, so it restarts the idle clock. Long stages —
    // an HTTP connect, an erase, a retry backoff — otherwise pass without a single
    // wake, and the user ends up watching an unlit screen for the whole of one.
    if (keepAwake) wakeUpScreen();

#if E_PAPER_DISPLAY
    bgcolor = BLACK;
    fgcolor = WHITE;
#endif
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
#endif

    // stripe drawing
    int size = text.length() * LW * _fm < (tft->width() - 2 * _fm * LW) ? _fm : _fp;
    int paddingX = 8;
    int paddingY = 5;
    int rectX = 10;
    int rectW = tftWidth - 20;
    int maxLineChars = (rectW - 2 * paddingX) / (LW * size);
    if (maxLineChars < 1) maxLineChars = 1;

    std::vector<String> lines;
    String line;
    String word;
    auto appendWord = [&]() {
        if (word.isEmpty()) return;

        while (static_cast<int>(word.length()) > maxLineChars) {
            if (!line.isEmpty()) {
                lines.push_back(line);
                line = "";
            }
            lines.push_back(word.substring(0, maxLineChars));
            word = word.substring(maxLineChars);
        }

        int extraSpace = line.isEmpty() ? 0 : 1;
        if (!line.isEmpty() && static_cast<int>(line.length() + extraSpace + word.length()) > maxLineChars) {
            lines.push_back(line);
            line = "";
        }
        if (!line.isEmpty()) line += " ";
        line += word;
        word = "";
    };

    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\n') {
            appendWord();
            lines.push_back(line);
            line = "";
        } else if (c == ' ') {
            appendWord();
        } else {
            word += c;
        }
    }
    appendWord();
    if (!line.isEmpty() || lines.empty()) lines.push_back(line);

    int lineHeight = size * LH;
    int rectH = static_cast<int>(lines.size()) * lineHeight + 2 * paddingY;
    int maxRectH = tftHeight > 20 ? tftHeight - 20 : tftHeight;
    if (rectH > maxRectH) rectH = maxRectH;
    int rectY = tftHeight / 2 - rectH / 2;
    if (rectY < 0) rectY = 0;

    tft->fillRoundRect(rectX, rectY, rectW, rectH, 7, bgcolor);
    tft->setTextColor(fgcolor, bgcolor);
    tft->setTextSize(size);

    int visibleLines = (rectH - 2 * paddingY) / lineHeight;
    int textY = rectY + (rectH - visibleLines * lineHeight) / 2;
    for (int i = 0; i < visibleLines && i < static_cast<int>(lines.size()); ++i) {
        tft->drawCentreString(lines[i], tftWidth / 2, textY + i * lineHeight, 1);
    }

    tft->display(false);
#if E_PAPER_DISPLAY
#if defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
#endif
#endif
    // return previous tft settings
    tft->setTextSize(_size);
    tft->setTextColor(_color, _bgcolor);
    tft->setCursor(_x, _y);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/***************************************************************************************
** Function name: displayError
** Description:   Show an error via the red stripe, then either wait for a keypress
**               or block for a fixed delay, depending on waitKeyPress.
***************************************************************************************/
void displayError(String txt, bool waitKeyPress) {
    displayRedStripe(txt);
    Serial.println("ERR: " + txt);
    if (!waitKeyPress) {
        const uint32_t tmp = launcherMillis() + 2000;
        while (launcherMillis() < tmp) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (check(AnyKeyPress)) break;
        }
    }
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(pdMS_TO_TICKS(10));
}

void displayMsg(String txt, bool waitKeyPress) { displayError(txt, waitKeyPress); }

/***************************************************************************************
** Function name: progressHandler
** Description:   Função para manipular o progresso da atualização
** Dependencia: prog_handler =>>    0 - Flash, 1 - SPIFFS
***************************************************************************************/
void progressHandler(size_t progress, size_t total) {
    vTaskDelay(pdMS_TO_TICKS(2));
    tft->drawPixel(0, 0, 0);
#if defined(E_PAPER_DISPLAY)
    static unsigned long lastUpdate = 0;
#endif
    static unsigned long lastProgressDraw = 0;
    static size_t lastProgressBarWidth = 0;
    double fraction = (double)progress / (double)total;
    double barWidthFloat = (tftWidth - 40) * fraction;
    size_t barWidth = static_cast<size_t>(barWidthFloat);
    // Serial.printf("Total: %d, Progress: %d, Progress bar width: %d \n", total, progress, barWidth);
    if (progress == 0) {
        lastProgressDraw = launcherMillis();
        lastProgressBarWidth = 0;
        tft->setTextSize(_fm);
        tft->setTextColor(ALCOLOR);
        tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
        tft->drawCentreString("-=Launcher=-", tftWidth / 2, panelHeight() > 200 ? 20 : 10, 1);
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
        if (prog_handler == 1) {
            tft->drawRect(18, tftHeight - 28, tftWidth - 36, 17, ALCOLOR);
            tft->fillRect(20, tftHeight - 26, tftWidth - 40, 13, BGCOLOR);
        } else tft->drawRect(18, tftHeight - 47, tftWidth - 36, 17, FGCOLOR);

        String txt;
        switch (prog_handler) {
            case 0: txt = "Installing FW"; break;
            case 1: txt = "Copying Data"; break;
            case 2: txt = "Downloading"; break;
        }
        displayRedStripe(txt);
    }
    if (progress > 0 && progress < total) {
        unsigned long now = launcherMillis();
        if (barWidth == lastProgressBarWidth || now - lastProgressDraw < 80) {
            wakeUpScreen();
            return;
        }
        lastProgressDraw = now;
        lastProgressBarWidth = barWidth;
    }

    if (prog_handler == 1) tft->fillRect(20, tftHeight - 26, barWidth, 13, ALCOLOR);
    else tft->fillRect(20, tftHeight - 45, barWidth, 13, FGCOLOR);

#if defined(E_PAPER_DISPLAY)
    if (launcherMillis() - lastUpdate > 2000) {
        tft->display();
        lastUpdate = launcherMillis();
    }
#else
    tft->display();
#endif
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
#endif
    wakeUpScreen();
    vTaskDelay(pdMS_TO_TICKS(2));
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Função para desenhar e mostrar as opçoes de contexto
***************************************************************************************/
Opt_Coord drawOptions(
    int idx, std::vector<Option> &opt, std::vector<MenuOptions> &t_menu, uint16_t fgcolor, uint16_t bgcolor,
    bool border, bool forceFullRedraw
) {
    // Cached layout of the previous call, used to tell whether this redraw only moved the
    // selection within the same page (draw just the two changed rows) or whether the page,
    // menu or styling changed (repaint everything). loopOptions is the sole caller and always
    // passes forceFullRedraw=true on the first draw of a session, so stale statics from an
    // unrelated menu can never leak into a partial redraw.
    static int lastIndex = -1;
    static int lastStart = -1;
    static int lastOptionCount = -1;
    static int lastArraySize = -1;
    static bool lastShowPageUp = false;
    static bool lastShowPageDown = false;
    static bool lastBorder = false;
    static uint16_t lastFgcolor = 0;
    static uint16_t lastBgcolor = 0;
    static int lastBoxX = -1;
    static int lastBoxY = -1;

    int index = idx;
    uint16_t alcolor = ALCOLOR;
#ifdef E_PAPER_DISPLAY
    bgcolor = WHITE; // 0xffff
    alcolor = BLACK; // 0x0000
    fgcolor = BLACK;
#endif

    Opt_Coord coord;
    coord.bgcolor = bgcolor;
    coord.fgcolor = fgcolor;

    t_menu.clear();
    int arraySize = opt.size();
    if (arraySize == 0) { return coord; }

    if (index < 0) index = 0;
    if (index >= arraySize) index = arraySize - 1;

    int lineHeight = _fm * LH;
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
    coord.boxX = boxX;
    coord.boxY = boxY;
    coord.boxW = contentWidth;
    coord.boxH = contentHeight;

    // Same page, same menu, same styling, only the selected row moved: repaint just the
    // previously- and newly-selected rows instead of the whole box. Anything structural
    // (paging, menu identity, colors, first draw) falls back to a full repaint.
    bool partialRedraw = !forceFullRedraw && index != lastIndex && start == lastStart &&
                         optionCount == lastOptionCount && arraySize == lastArraySize &&
                         showPageUp == lastShowPageUp && showPageDown == lastShowPageDown &&
                         border == lastBorder && fgcolor == lastFgcolor && bgcolor == lastBgcolor &&
                         boxX == lastBoxX && boxY == lastBoxY;
    int prevIndex = lastIndex;

    lastIndex = index;
    lastStart = start;
    lastOptionCount = optionCount;
    lastArraySize = arraySize;
    lastShowPageUp = showPageUp;
    lastShowPageDown = showPageDown;
    lastBorder = border;
    lastFgcolor = fgcolor;
    lastBgcolor = bgcolor;
    lastBoxX = boxX;
    lastBoxY = boxY;

    bool firstItemSelected = (optionCount > 0 && index == start);
    tft->setTextSize(_fm);

    if (!partialRedraw) {
        if (border) {
            if (firstItemSelected) tft->fillRoundRect(boxX, boxY, contentWidth, contentHeight, 5, bgcolor);
            tft->drawRoundRect(boxX, boxY, contentWidth, contentHeight, 5, fgcolor);
        } else {
            if (firstItemSelected) tft->fillRoundRect(3, 3, tftWidth - 6, tftHeight - 6, 5, bgcolor);
            tft->drawRoundRect(2, 2, tftWidth - 4, tftHeight - 4, 5, fgcolor);
        }
    }

    int lineWidth = contentWidth - paddingSide * 2;
    if (lineWidth < 0) lineWidth = contentWidth;
    int charWidth = LW * tft->getTextSize();
    if (charWidth <= 0) charWidth = 1;
    int indicatorWidth = charWidth;
    if (indicatorWidth > lineWidth) indicatorWidth = lineWidth;
    int textStartY = boxY + paddingTop;
    int rowIndex = 0;

    auto addNavLine = [&](const char *text, bool isUp, bool paint) {
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        if (paint) {
            int textWidth = strlen(text) * charWidth;
            int navX =
                boxX + paddingSide + 0 > ((lineWidth - textWidth) / 2) ? 0 : ((lineWidth - textWidth) / 2);
            tft->fillRect(boxX + paddingSide, rowTop, lineWidth, lineHeight, bgcolor);
            tft->setCursor(navX, rowTop);
            tft->setTextColor(alcolor, bgcolor);
            tft->drawCentreString(text, tftWidth / 2, rowTop, 1);
        }

        MenuOptions navItem("", isUp ? "-" : "+", nullptr, true, false);
        navItem.setCoords(boxX + paddingSide, rowTop, lineWidth, lineHeight + rowSpacing);
        t_menu.push_back(navItem);

        rowIndex++;
    };
#ifdef HAS_TOUCH
    if (showPageUp) { addNavLine("-- Page Up --", true, !partialRedraw); }
#endif
    for (int i = 0; i < optionCount && (start + i) < arraySize; ++i) {
        ALIVIATE_TASK;
        int optionIndex = start + i;
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        int rowLeft = boxX + paddingSide;
        bool rowNeedsPaint = !partialRedraw || optionIndex == index || optionIndex == prevIndex;
        if (rowNeedsPaint && i > 0)
            tft->fillRect(rowLeft, rowTop - rowSpacing, lineWidth, rowSpacing, bgcolor);
        int prefixWidth = 0;
        int cursorX = rowLeft;
#ifdef HAS_TOUCH
        int escWidth = 0;
        bool showEscLabel = (!border && i == 0);
#endif
#ifdef HAS_TOUCH
        if (RES && !border) {
            if (i < (RES / (LH * _fm) + 1)) cursorX += RES - i * LW * _fm;
        }
#endif

        char indicatorChar = (optionIndex == index) ? '>' : ' ';
        if (rowNeedsPaint) {
            tft->setCursor(cursorX, rowTop);
            tft->setTextColor(fgcolor, bgcolor);
            tft->print(indicatorChar);
        }
        prefixWidth += indicatorWidth;
        cursorX += indicatorWidth;

        uint16_t color = opt[optionIndex].color;
        if (color == NO_COLOR) color = fgcolor;
#ifdef E_PAPER_DISPLAY
        color = BLACK;
#endif

        int labelX = cursorX;
        int labelWidth = lineWidth - prefixWidth;
#ifdef HAS_TOUCH
        if (showEscLabel) {
            const char *escText = "[ESC]";
            escWidth = strlen(escText) * charWidth;
            int escX = boxX + paddingSide + lineWidth - escWidth;
            if (escX < labelX) escX = labelX;
            if (rowNeedsPaint) {
                tft->setCursor(escX, rowTop);
                tft->setTextColor(alcolor, bgcolor);
                tft->print(escText);
            }

            MenuOptions escItem("", "ESC", nullptr, true, false);
            escItem.setCoords(
                escX > 4 ? escX - 4 : 0, rowTop > 2 ? rowTop - 2 : 0, escWidth + 8, lineHeight + rowSpacing
            );
            t_menu.push_back(escItem);

            labelWidth -= escWidth + charWidth;
        }
#endif
        if (RES && !border) {
            if (i < (RES / (LH * _fm) + 1)) { labelWidth -= RES / (i + 1); }
            if (i >= (optionCount - (RES / (LH * _fm) + 1))) { labelWidth -= RES / (optionCount - i); }
        }
        if (labelWidth < 0) labelWidth = 0;
        int labelCharLimit = labelWidth / charWidth;
        if (labelCharLimit < 1) labelCharLimit = 1;

        char txt[labelCharLimit];
        snprintf(txt, sizeof(txt), "%-*s", labelCharLimit, opt[optionIndex].label.c_str());

        if (rowNeedsPaint) {
            tft->setCursor(labelX, rowTop);
            tft->setTextColor(color, bgcolor);
            tft->print(txt);
        }

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
    if (showPageDown) { addNavLine("-- Page Down --", false, !partialRedraw); }
#endif
    if (partialRedraw) {
        tft->display(false);
        return coord;
    }
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
    TouchFooter(FGCOLOR);
    tft->display(false);

    return coord;
}
/***************************************************************************************
** Function name: drawMainMenu
** Description:   Função para desenhar e mostrar o menu principal
***************************************************************************************/
void drawMainMenu(std::vector<MenuOptions> &opt, int index, bool forceFullRedraw) {
    // Remembers which item was drawn as selected last time, so a plain selection move only
    // has to erase the old highlight and paint the new one instead of repainting every icon.
    // The grid layout (position/size per item) never changes between calls in the same
    // session, so all other icons stay pixel-identical and don't need to be touched.
    static int lastIndex = -1;
    static int bat = 0;
    static bool wifi = false;

    uint8_t size = opt.size();
    if (size < 1) {
        displayRedStripe("No options available");
        return;
    }
    bool compactOneLine = tftHeight <= 90;
    int cols = compactOneLine ? 5 : 3; // Number of columns based on height
    int visibleItems = compactOneLine && size > cols ? cols : size;
    int rows = compactOneLine ? 1 : (size + cols - 1) / cols;             // Calculate rows needed
    int w = (tftWidth - 16) / cols;                                       // Width of each icon
    int h = (tftHeight - ((6 + 6 + _fp * LH + 6) + LH * _fp + 6)) / rows; // Height of each icon

    int maxIconTextSize = tftHeight <= 135 ? _fm : _fg;

    // The carousel (compact one-line) layout reassigns every slot's item on each move, so it
    // always needs a full repaint; the fixed grid can be updated incrementally.
    bool fullRedraw =
        forceFullRedraw || compactOneLine || lastIndex < 0 || lastIndex >= static_cast<int>(size);
    int prevIndex = lastIndex;
    lastIndex = index;

    for (int i = 0; i < size; ++i) opt[i].resetCoords();

    for (int slot = 0; slot < visibleItems; ++slot) {
        ALIVIATE_TASK;
        int i = slot;
        if (compactOneLine && size > cols) {
            int centerSlot = cols / 2;
            i = (index + slot - centerSlot + size) % size;
        }

        int col = slot % cols;
        int row = compactOneLine ? 0 : slot / cols;
        int y = (6 + 6 + _fp * LH + 8) + row * h;
        int xOffset = 0;

        // Última linha incompleta: centralizar
        if (!compactOneLine && row == rows - 1 && (size % cols) != 0 && (size % cols) < cols) {
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

        if (!fullRedraw && i != index && i != prevIndex) continue;

        uint16_t itemColor = opt[i].active ? opt[i].color : DARKGREY;
        uint16_t selectedColor = opt[i].active ? opt[i].color : LIGHTGREY;
        int f_size = maxIconTextSize;
        const int textLimit = w - 10;
        tft->setTextSize(f_size);
        if (static_cast<int>(opt[i].name.length()) * LW * f_size > textLimit && f_size > _fm) {
            f_size = _fm;
            tft->setTextSize(f_size);
        }
        if (static_cast<int>(opt[i].name.length()) * LW * f_size > textLimit && f_size > _fp) {
            f_size = _fp;
            tft->setTextSize(f_size);
        }

        if (i == index) {
            // Selected item
            tft->fillRoundRect(x + 6, y + 6, w - 6, h - 6, 5, DARKGREY);
            tft->fillRoundRect(x, y, w - 6, h - 6, 5, selectedColor);
            tft->setTextColor(BGCOLOR, selectedColor);
            // Draw text in the center of the icon
            tft->drawCentreString(opt[i].name, x + (w - 6) / 2, y + (h - 6) / 2 - LH * f_size / 2, 1);
        } else {
            int drawX = x;
            int drawY = y;
            int drawW = w;
            int drawH = h;
            if (compactOneLine) {
                int insetY = h > 20 ? 4 : 2;
                tft->fillRoundRect(x, y, w, h, 5, BGCOLOR);
                drawY += insetY;
                drawH -= 2 * insetY;
                if (drawH < 8) {
                    drawY = y;
                    drawH = h;
                }
            }
            // Clear residue from previous selected state: top-left (button) and bottom-right (shadow)
            tft->fillRect(x, y, 4, 4, BGCOLOR);
            tft->fillRect(x + w - 4, y + h - 4, 4, 4, BGCOLOR);
            // Non-selected item
            tft->drawRoundRect(drawX, drawY, drawW, drawH, 5, BGCOLOR);
            tft->drawRoundRect(drawX + 1, drawY + 1, drawW - 2, drawH - 2, 5, BGCOLOR);
            tft->drawRoundRect(drawX + 2, drawY + 2, drawW - 4, drawH - 4, 5, BGCOLOR);
            tft->fillRoundRect(drawX + 3, drawY + 3, drawW - 6, drawH - 6, 5, BGCOLOR);
            tft->drawRoundRect(drawX + 3, drawY + 3, drawW - 6, drawH - 6, 5, itemColor);
            tft->setTextColor(itemColor, BGCOLOR);
            // Draw text in the center of the icon
            tft->drawCentreString(opt[i].name, drawX + drawW / 2, drawY + drawH / 2 - LH * f_size / 2, 1);
        }
        // tft->drawRect(opt[i].x,opt[i].y,opt[i].w,opt[i].h,BLUE); // debug purpose
    }

    tft->setTextSize(_fp);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    // Draw the description of the selected item (always changes with the selection)
    tft->fillRect(10, tftHeight - (6 + LH * _fp), tftWidth - 20, LH * _fp, BGCOLOR);
    tft->drawCentreString(opt[index].text, tftWidth / 2, tftHeight - (6 + LH * _fp), 1);
    if (fullRedraw) {
        // Draw Launcher version and battery value
        // Short panels have no room for the version next to the battery gauge.
        if (panelHeight() < 200) tft->drawString("Launcher", 12 + RES, 12);
        else tft->drawString("Launcher " + String(LAUNCHER), 12 + RES, 12);
        tft->setTextSize(maxIconTextSize);
        drawDeviceBorder();
        TouchFooter();
        bat = 0;
        wifi = false;
    }
    int _bat = getBattery();
    if (bat != _bat && _bat > 0) {
        drawBatteryStatus(_bat);
        bat = _bat;
    }
    bool _wifi = launcherWifiIsConnected();
    if (_wifi && wifi != _wifi) drawWifiStatus(bat > 0);
    wifi = _wifi;
    tft->display(false);
}
/***************************************************************************************
** Function name: launcherBootAppShortcuts
** Description:   Lazily builds and caches the boot shortcut cards (one per installed
**                 app). Shared by initDisplay (drawing) and the bootscreen input loop
**                 (touch hit-testing), so both sides agree on labels/coordinates.
***************************************************************************************/
std::vector<MenuOptions> &launcherBootAppShortcuts() {
    static std::vector<MenuOptions> shortcuts;
    static bool built = false;
    if (!built) {
        for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
            String label = app.label;
            String icon = (app.name.isEmpty() ? app.label : app.name).substring(0, 5);
            icon.toUpperCase();
            shortcuts.push_back({icon, "", [label]() { launcherBootAppByLabel(label.c_str()); }});
        }
        built = true;
    }
    return shortcuts;
}

/***************************************************************************************
** Function name: drawBootAppShortcuts
** Description:   Draws a row of cards with each installed app's initials at the top of
**                 the bootscreen, with its keyboard digit shortcut in the top-left
**                 corner, so a tap/keypress boots that app directly. Returns the total
**                 height, in pixels, occupied by the cards.
***************************************************************************************/
int drawBootAppShortcuts(std::vector<MenuOptions> &opt) {
    uint8_t size = opt.size();

    if (size < 1) return 0;

#if defined(USE_CARDKB2) && !defined(HAS_TOUCH)
    if (!CardKB2Installed) return 0;
#endif

    int boxH = LH * _fm + 8;
    int minBoxW = boxH * 2; // keep the card proportional to its (now taller) height
    int maxCols = (tftWidth - 8) / minBoxW;
    if (maxCols < 1) maxCols = 1;
    if (maxCols > size) maxCols = size;
    int boxW = (tftWidth - 8) / maxCols;
    int y = 4;

    // Only as many characters as actually fit the card, so names never overlap
    // when several apps are installed and the cards get narrow.
    int maxNameChars = (boxW - 6) / (LW * _fm);
    if (maxNameChars < 1) maxNameChars = 1;

    for (int i = 0; i < size; ++i) {
        int col = i % maxCols;
        int row = i / maxCols;
        int x = 4 + col * boxW;
        int by = y + row * (boxH + 2);
        opt[i].setCoords(x, by, boxW - 2, boxH);

        // tft->fillRoundRect(x, by, boxW - 2, boxH, 3, BGCOLOR);
        tft->drawRoundRect(x, by, boxW - 2, boxH, 3, FGCOLOR);

        String name = opt[i].name;
        if (static_cast<int>(name.length()) > maxNameChars) name = name.substring(0, maxNameChars);
        tft->setTextSize(_fm);
        tft->setTextColor(FGCOLOR, BGCOLOR);
        tft->drawCentreString(name, x + (boxW - 2) / 2, by + boxH / 2 - (LH * _fm) / 2, 1);

        if (i < 10) { // Only the first 10 apps have a keyboard digit shortcut (1..9,0)
            String shortcutLabel = (i == 9) ? "0" : String(i + 1);
            tft->setTextSize(_fp);
            tft->setTextColor(ALCOLOR, BGCOLOR);
            tft->drawString(shortcutLabel, x + 2, by + 1);
        }
    }

    int rows = (size + maxCols - 1) / maxCols;
    return y + rows * (boxH + 2);
}
void drawDeviceBorder() {
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    tft->drawLine(5, (6 + 6 + _fp * LH + 5), tftWidth - 6, (6 + 6 + _fp * LH + 5), FGCOLOR);
}

void drawWifiStatus(bool hasBattery) {
    const int size = LH * _fp;
    int u = size / 4;
    if (u < 1) u = 1;
    const int gap = 4;
    int batteryLeft = tftWidth - 5 - (LW * _fp * 4 * hasBattery + 40) - RES;
    int cx = batteryLeft - gap - 3 * u;
    int by = 7 + (_fp * LH + 9) / 2 + u;
    int dot = u < 2 ? 2 : u;
    tft->fillRect(cx - 3 * u - 1, 6, 6 * u + 3, 4 * u + dot + 2, BGCOLOR);
    int thick = size / 8;
    if (thick < 1) thick = 1;
    for (int k = 1; k <= 3; ++k) {
        int hw = k * u;
        int apexY = by - k * u - u + 2;
        int drop = u;
        for (int t = 0; t < thick; ++t) {
            tft->drawLine(cx - hw, apexY + drop + t, cx, apexY + t, FGCOLOR);
            tft->drawLine(cx, apexY + t, cx + hw, apexY + drop + t, FGCOLOR);
        }
    }
    tft->fillRect(cx - dot / 2, by - dot / 2 + 2, dot, dot, FGCOLOR);
}

void drawBatteryStatus(uint8_t bat) {
    tft->drawRoundRect(tftWidth - 42 - RES, 7, 34, _fp * LH + 9, 2, FGCOLOR);
    tft->setTextSize(_fp);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    // Excludes the Marauder Mini and anything else that short: no room for a
    // percentage beside the gauge.
    if (panelHeight() > 140) tft->drawRightString("  " + String(bat) + "%", tftWidth - 45 - RES, 12, 1);
    tft->fillRoundRect(tftWidth - 40 - RES, 9, 30, _fp * LH + 5, 2, BGCOLOR);
    tft->fillRoundRect(tftWidth - 40 - RES, 9, 30 * bat / 100, _fp * LH + 5, 2, FGCOLOR);
    tft->drawLine(tftWidth - 30 - RES, 9, tftWidth - 30 - RES, 9 + _fp * LH + 6, BGCOLOR);
    tft->drawLine(tftWidth - 20 - RES, 9, tftWidth - 20 - RES, 9 + _fp * LH + 6, BGCOLOR);
}

/*********************************************************************
**  Function: loopOptions
**  Where you choose among the options in menu
**********************************************************************/
int loopOptions(std::vector<Option> &options, bool bright, uint16_t al, uint16_t bg, bool border, int index) {
    bool redraw = true;
    bool exit = false;
    bool firstDraw = true;
#if defined(HAS_TOUCH)
    bool escRequested = false; // set only by the explicit [ESC] label (touch, border==false)
#endif
    log_i("Number of options: %d", options.size());
    int numOpt = options.size() - 1;
    Opt_Coord coord;
    std::vector<MenuOptions> list;
    int max_idx = 0;
    int min_idx = 255;
    LongPressTmp = launcherMillis();
    while (1) {
        if (redraw) {
            list = {};
            bool wasFirstDraw = firstDraw;
            coord = drawOptions(index, options, list, al, bg, border, firstDraw);
            firstDraw = false;
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
            M5.Display.setEpdMode(epd_mode_t::epd_text);
#endif
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
        if (index >= 0 && index < static_cast<int>(options.size())) {
            String txt = options[index].label;
            displayScrollingText(txt, coord);
        }

#if defined(HAS_ENCODER) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
#if defined(HAS_TOUCH)
        if (border == false) EscPress = false;
        if (touchPoint.pressed) {
            for (auto item : list) {
                if (item.contain(touchPoint.x, touchPoint.y)) {
                    resetGlobals();
                    if (item.name == "") {
                        if (item.text == "ESC") {
                            escRequested = true;
                        } else {
                            if (item.text == "+") index = max_idx + 1;
                            if (item.text == "-") index = min_idx - 1;
                            if (index < 0) index = 0;
                            // Serial.printf("\nPressed [%s], next index: %d\n",item.text,index);
                            redraw = true;
                        }
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
                LongPressTmp = launcherMillis();
            }
            if (LongPress && launcherMillis() - LongPressTmp < 700) {
                if (!PrevPress) {
                    AnyKeyPress = false;
                    if (index == 0) index = options.size() - 1;
                    else if (index > 0) index--;
                    LongPress = false;
                    redraw = true;
                }
                if (launcherMillis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (launcherMillis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111,
                        BGCOLOR
                    );
                if (launcherMillis() - LongPressTmp > 700) { // longpress detected to exit
                    LongPress = false;
                    check(PrevPress);
                    exit = true;
                    break;
                } else goto WAITING;
            }
        }
#if defined(HAS_5_BUTTONS) || defined(USE_CARDKB2)
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
            drawOptionsErase(coord);
            options[index].operation();
            break;
        }

#if defined(HAS_TOUCH)
        if (border == false) {
            if (escRequested || returnToMenu || exit) return -1;
            EscPress = false; // swallow any stray heat-map ESC over the list rows
        } else {
            if (check(EscPress) || returnToMenu || exit) return -1;
        }
#else
        if (check(EscPress) || returnToMenu || exit) return -1;
#endif
    }

#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
#endif
    return index;
}

/*********************************************************************
**  Function: loopVersions
**  Where you choose which version to install/download **
**********************************************************************/
void loopVersions(const String &_fid) {
    JsonDocument item = getVersionInfo(_fid);
    if (item.isNull()) { return; }
    int versionIndex = 0;
    const char *name = item["name"];
    const char *author = item["author"];
    const char *fid = item["fid"];
    const bool star = item["star"].as<bool>();
    JsonArray versions = item["versions"];
    bool redraw = true;

    LongPressTmp = launcherMillis();
    while (1) {
        if (returnToMenu) break; // Stops the loop to get back to Main menu

        JsonObject Version = versions[versionIndex];
        const char *version = Version["version"];
        const char *published_at = Version["published_at"];
        const char *file = Version["file"];
        if (redraw) {
            displayCurrentVersion(
                String(name), String(author), String(version), String(published_at), versionIndex, versions
            );
            redraw = false;
            tft->display(false);
        }
        /* DW Btn to next item */
        if (check(NextPress)) {
            versionIndex++;
            if (versionIndex > versions.size() - 1) versionIndex = 0;
            redraw = true;
        }

        /* UP Btn go back to FW menu and ´<´ go to previous version item */

#if defined(HAS_ENCODER) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
        /* UP Btn go to previous item */
        if (check(PrevPress)) {
            versionIndex--;
            if (versionIndex < 0) versionIndex = versions.size() - 1;
            redraw = true;
        }

#else // Esc logic is holding previous btn fot 1 second +-
        if (LongPress || PrevPress) {
            if (!LongPress) {
                LongPress = true;
                LongPressTmp = launcherMillis();
            }
            if (LongPress && launcherMillis() - LongPressTmp < 800) {
            WAITING:
                vTaskDelay(10 / portTICK_PERIOD_MS);
                if (!PrevPress && launcherMillis() - LongPressTmp < 200) {
                    AnyKeyPress = false;
                    if (versionIndex == 0) versionIndex = versions.size() - 1;
                    else if (versionIndex > 0) versionIndex--;
                    LongPress = false;
                    redraw = true;
                }
                if (!PrevPress && launcherMillis() - LongPressTmp > 200) {
                    check(PrevPress);
                    redraw = true;
                    LongPress = false;
                    goto EXIT_CHECK;
                }
                if (launcherMillis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (launcherMillis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111,
                        BGCOLOR
                    );
                if (launcherMillis() - LongPressTmp > 700) { // longpress detected to exit
                    returnToMenu = true;
                    check(PrevPress);
                    goto SAIR;
                } else goto WAITING;
            }
        EXIT_CHECK:
            yield();
        }

#endif
        if (check(EscPress)) { goto SAIR; }

        /* Select to install */
        if (check(SelPress)) {

            // Definição da matriz "Options"
            options = {
                {"OTA Install", [=]() {
                     installFirmwareFromManifest(
                         String(fid), String(version), String(name) + " - " + String(version)
                     );
                 }}
            };
            if (sdcardMounted) {
                options.push_back({"Download->SD", [=]() {
                                       downloadFirmware(
                                           String(fid),
                                           String(file),
                                           String(name) + "." + String(version).substring(0, 10),
                                           dwn_path,
                                           String(version)
                                       );
                                   }});
                options.push_back({"Add to Favorite", [=] {
                                       JsonObject fav = favorite.add<JsonObject>();
                                       fav["name"] =
                                           String(name) + " - " + String(author) + " (" + ota_tag + ")";
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
    if (!returnToMenu) { return (void)releaseHeapObjectsAndReboot(); }

// quando sair, redesenhar a tela
SAIR:
    if (!returnToMenu) tft->fillScreen(BGCOLOR);
}

/*********************************************************************
**  Function: loopFirmware
**  Where you choose which Firmware to see more data
**********************************************************************/
void loopFirmware(bool isUpdate) {
    int _page = current_page;
    String order_by = "downloads";
    String query = "";
    bool star = false;
    bool refine = false;
    bool refined = false;
    int index = 0;

RESTART:
    currentIndex = -1;
    if (isUpdate && index > 0) {
        checkForUpdates();
    } else if (_page != current_page || refined) {
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

    if (sdcardMounted && !doc["items"][0]["file"].as<String>().isEmpty() && isUpdate) {
        options.push_back(
            {"[Update all]",
             [=]() {
                 int count = (int)doc["items"].size();
                 for (int i = 0; i < count; i++) {
                     String fid = doc["items"][i]["fid"].as<String>();
                     String file = doc["items"][i]["file"].as<String>();
                     String name = doc["items"][i]["name"].as<String>();
                     String ver = doc["items"][i]["version"].as<String>();
                     downloadFirmware(fid, file, name + "." + ver.substring(0, 10), dwn_path, ver, true);
                     if (returnToMenu) break;
                 }
             },
             ALCOLOR}
        );
    }

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
            {"Order by downloads",
             [&]() {
                 order_by = "downloads";
                 refined = true;
             }, order_by == "downloads" ? FGCOLOR : NO_COLOR},
            {"Order by name",
             [&]() {
                 order_by = "name";
                 refined = true;
             }, order_by == "name" ? FGCOLOR : NO_COLOR},
            {"Order by latest",
             [&]() {
                 order_by = "date";
                 refined = true;
             }, order_by == "date" ? FGCOLOR : NO_COLOR},
            {star == true ? "[x] Starred Only" : "[ ] Starred Only",
             [&]() {
                 star = !star;
                 refined = true;
             }},
            {"Text Search",
             [&]() {
                 String _q = keyboard(query, 76, "Search Firmware");
                 if (_q != String(KEY_ESCAPE)) {
                     query = _q;
                     refined = true;
                 }
             }},
            {"Back to list", [&]() { yield(); }}
        };
        loopOptions(opt);
    }
    if (!returnToMenu && index >= 0) goto RESTART;
    doc.clear();
    RAM_LOG("firmwareList-doc-cleared");
}

/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprintln(const String &txt, int margin, int numlines) {
    String rem = txt; // working copy: consumed line by line below
    int size = rem.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextSize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    while (size > 0 && numlines > 0) {
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
        tft->println(rem.substring(0, nchars));
        rem = rem.substring(nchars);
        size -= nchars;
        numlines--;
    }
}
/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprint(const String &txt, int margin, int numlines) {
    String rem = txt; // working copy: consumed line by line below
    int size = rem.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextSize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    bool prim = true;
    while (size > 0 && numlines > 0) {
        if (!prim) { tft->println(); }
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
        tft->print(rem.substring(0, nchars));
        rem = rem.substring(nchars);
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
