#include "mykeyboard.h"
#include "display.h"
#include "powerSave.h"
#include "settings.h"
#include <globals.h>

int max_FM_size = tftWidth / (LW * FM) - 1;
int max_FP_size = tftWidth / (LW)-2;

// QWERTY KEYSET
const int qwerty_keyboard_width = 12;
const int qwerty_keyboard_height = 4;
char qwerty_keyset[qwerty_keyboard_height][qwerty_keyboard_width][2] = {
    //  4 lines, with 12 characters, capital and lowercase
    {{'1', '!'},
     {'2', '@'},
     {'3', '#'},
     {'4', '$'},
     {'5', '%'},
     {'6', '^'},
     {'7', '&'},
     {'8', '*'},
     {'9', '('},
     {'0', ')'},
     {'-', '_'},
     {'=', '+'} },
    {{'q', 'Q'},
     {'w', 'W'},
     {'e', 'E'},
     {'r', 'R'},
     {'t', 'T'},
     {'y', 'Y'},
     {'u', 'U'},
     {'i', 'I'},
     {'o', 'O'},
     {'p', 'P'},
     {'[', '{'},
     {']', '}'} },
    {{'a', 'A'},
     {'s', 'S'},
     {'d', 'D'},
     {'f', 'F'},
     {'g', 'G'},
     {'h', 'H'},
     {'j', 'J'},
     {'k', 'K'},
     {'l', 'L'},
     {';', ':'},
     {'"', '\''},
     {'|', '\\'}},
    {{'\\', '|'},
     {'z', 'Z'},
     {'x', 'X'},
     {'c', 'C'},
     {'v', 'V'},
     {'b', 'B'},
     {'n', 'N'},
     {'m', 'M'},
     {',', '<'},
     {'.', '>'},
     {'?', '/'},
     {'/', '/'} }
};

// HEX KEYSET
const int hex_keyboard_width = 4;
const int hex_keyboard_height = 4;
char hex_keyset[hex_keyboard_height][hex_keyboard_width][2] = {
    {{'0', '0'}, {'1', '1'}, {'2', '2'}, {'3', '3'}},
    {{'4', '4'}, {'5', '5'}, {'6', '6'}, {'7', '7'}},
    {{'8', '8'}, {'9', '9'}, {'A', 'a'}, {'B', 'b'}},
    {{'C', 'c'}, {'D', 'd'}, {'E', 'e'}, {'F', 'f'}},
};

// NUMBERS ONLY KEYSET
const int numpad_keyboard_width = 4;
const int numpad_keyboard_height = 3;
char numpad_keyset[numpad_keyboard_height][numpad_keyboard_width][2] = {
    // 3 lines, with 4 characters each:
    {{'1', '1'}, {'2', '2'}, {'3', '3'}, {'\0', '\0'}},
    {{'4', '4'}, {'5', '5'}, {'6', '6'}, {'.', '.'}  },
    {{'7', '7'}, {'8', '8'}, {'9', '9'}, {'0', '0'}  }
};

#if defined(HAS_TOUCH)
struct box_t {
    int x;
    int y;
    int w;
    int h;
    std::uint16_t color;
    int touch_id = -1;
    char key;
    char key_sh;

    void clear(void) {
        for (int i = 0; i < 8; ++i) { tft->fillRect(x, y, w, h, BGCOLOR); }
    }
    void draw(void) {
        int ie = touch_id < 0 ? 4 : 8;
        for (int i = 0; i < ie; ++i) {
            tft->drawRect(x, y, w, h, color);
            tft->setTextColor(color);
            tft->drawString(String(key), x + w / 2 - FM * LW / 2, y + h / 2 - FM * LH / 2);
        }
    }
    bool contain(int x, int y) {
        return this->x <= x && x < (this->x + this->w) && this->y <= y && y < (this->y + this->h);
    }
};

#endif

// Retrieves the current keyStroke from InputHandler, resets it after use.
// This function is used in loopTask to get the latest key press.
keyStroke _getKeyPress() {
#ifndef DONT_USE_INPUT_TASK
    keyStroke key = KeyStroke;
    KeyStroke.Clear();
    return key;
#else
    keyStroke key = KeyStroke;
    KeyStroke.Clear();
    return key;
#endif
} // Returns a keyStroke that the keyboards won't recognize by default

/*********************************************************************
** Shared keyboard helper functions
**********************************************************************/

/// Handles character deletion from the text string and screen
bool handleDelete(String &current_text, int &cursor_x, int &cursor_y) {
    if (current_text.length() == 0) return false;

    // remove from string
    current_text.remove(current_text.length() - 1);
    // delete from screen:
    int fontSize = FM;
    if (current_text.length() > max_FP_size) {
        tft->setTextSize(FP);
        fontSize = FP;
    } else tft->setTextSize(FM);
    tft->setCursor((cursor_x - fontSize * LW), cursor_y);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->print(" ");
    tft->setTextColor(getComplementaryColor(BGCOLOR), 0x5AAB);
    tft->setCursor(cursor_x - fontSize * LW, cursor_y);
    cursor_x = tft->getCursorX();
    cursor_y = tft->getCursorY();
    return true;
}

/// Handles adding a character to the text string
bool handleCharacterAdd(
    String &current_text, char character, int &cursor_x, int &cursor_y, const int max_size
) {
    if (current_text.length() >= max_size) return false;

    current_text += character;
    if (current_text.length() != (max_FP_size + 1)) tft->print(character);
    cursor_x = tft->getCursorX();
    cursor_y = tft->getCursorY();
    return true;
}

/// Handles adding space to the text string
bool handleSpaceAdd(String &current_text, const int max_size) {
    if (current_text.length() >= max_size) return false;
    current_text += ' ';
    return true;
}

// Enum for keyboard action results
enum KeyboardAction { KEYBOARD_CONTINUE, KEYBOARD_OK, KEYBOARD_CANCEL, KEYBOARD_REDRAW };

/// Handles keyboard selection logic for regular keyboard
KeyboardAction handleKeyboardSelection(
    int &x, int &y, String &current_text, bool &caps, int &cursor_x, int &cursor_y, const int max_size,
    char character
) {
    tft->setCursor(cursor_x, cursor_y);

    if (y == -1) {
        switch (x) {
            case 0: // OK button
                return KEYBOARD_OK;
            case 1: // CAP button
                caps = !caps;
                return KEYBOARD_REDRAW;
            case 2: // DEL button
                if (handleDelete(current_text, cursor_x, cursor_y)) return KEYBOARD_REDRAW;
                break;
            case 3: // SPACE button
                if (handleSpaceAdd(current_text, max_size)) return KEYBOARD_REDRAW;
                break;
            case 4: // BACK button
                current_text = KEY_ESCAPE;
                return KEYBOARD_CANCEL;
            default: break;
        }

    } else if (y > -1 && current_text.length() < max_size) {
        // add a letter to current_text
        if (handleCharacterAdd(current_text, character, cursor_x, cursor_y, max_size)) {
            if (current_text.length() >= max_size) { // put the Cursor at "Ok" when max size reached
                x = 0;
                y = -1;
            }

            return KEYBOARD_REDRAW;
        }
    }

    return KEYBOARD_CONTINUE;
}

template <int KeyboardHeight, int KeyboardWidth>
String generalKeyboard(
    String current_text, int max_size, String textbox_title, char keys[KeyboardHeight][KeyboardWidth][2]
) {
    resetTftDisplay();
    touchPoint.Clear();

    /* SUPPORT VARIABLES */
    bool caps = false;
    bool selection_made = false; // used for detecting if an key or a button was selected
    bool redraw = true;
    long last_input_time = millis(); // used for input debouncing
    // cursor coordinates: kep track of where the next character should be printed (in screen pixels)
    int cursor_x = 0;
    int cursor_y = 0;
    // keyboard navigation coordinates: keep track of which key (or button) is currently selected
    int x = 0;
    int y = -1; // -1 is where the buttons_strings are, out of the keys[][][] array
    int old_x = 0;
    int old_y = 0;
    //       [x][y] [z], old_x and old_y are the previous position of x and y, used to redraw only that spot
    //       on keyboard screen

    /*====================Initial Setup====================*/

    int buttons_number = 5;

    /*-----------------------------HOW btns_layout IS CALCULATED-----------------------------*/
    // const char *buttons_strings[] = {"OK", "aa", "<-", "[_]", "Esc"};
    // // { x coord of btn border, btn width, x coord of the inside text }
    // int btns_layout[buttons_number][3];
    // where:
    //      LW = Letter Width of font 1 (Ususally 6px)
    //      PAD = padding between buttons (2px usually)
    //      FM = Font Size for medium size (2 for the most of devices, 1 for smaller screens, and bigger for
    //      bigger screens)
    //      x=item number, y=Sum of characters of previous items;
    //      n=number of characters of this item
    // btns_layout[0][0] = x*PAD + y*LW*FM;
    // btns_layout[0][1] = n*LW*FM; //
    // btns_layout[0][3] = x*PAD + y*LW*FM + LW*FM/2;
    //
    // for (size_t i = 0; i < buttons_number; i++) {
    //     // start of previous btn + width of that btn + 2px padding between the buttons
    //     btns_layout[i][0] = btns_layout[i - 1][0] + btns_layout[i - 1][1] + 2;
    //     // 12px per character (10 for char + 2 for padding before next letter) - last padding
    //     // + 9px padding * 2 (before and after string)
    //     btns_layout[i][1] = (strlen(buttons_strings[i]) * 12) - 2 + 9 * 2;
    //     // x coord for start of string
    //     btns_layout[i][2] = btns_layout[i][0] + 9;
    // }
    //
    // for smaller screens is the same thing, just different values for padding etc.
    //
    // btns_layouts are hard coded because there is no way yet to enable/disable buttons,
    // so these do not change
    /*---------------------------------------------------------------------------------------*/

#define PAD 2
#define KBLH (6 + LH * FM) // Keyboard Buttons Line Height
    // { x coord of btn border, btn width, x coord of the inside text }
    // 12 px = 10 px + 2 of padding between the letters -> refer to the section above to better understand
    // ((12px * n_letters) - 2px ) + 9*2px = width
    const int btns_layout[5][3] = {
        {1 * PAD + 0 * LW * FM,  3 * LW * FM, 1 * PAD + 0 * LW * FM + LW * FM / 2 }, // OK
        {2 * PAD + 3 * LW * FM,  3 * LW * FM, 2 * PAD + 3 * LW * FM + LW * FM / 2 }, // ab (Caps)
        {3 * PAD + 6 * LW * FM,  3 * LW * FM, 3 * PAD + 6 * LW * FM + LW * FM / 2 }, // <- (DEL)
        {4 * PAD + 9 * LW * FM,  4 * LW * FM, 4 * PAD + 9 * LW * FM + LW * FM / 2 }, // [_] (SPACE)
        {5 * PAD + 13 * LW * FM, 4 * LW * FM, 5 * PAD + 13 * LW * FM + LW * FM / 2}, // Esc
    };

    const int key_width = tftWidth / KeyboardWidth;
    const int key_height = (tftHeight - (2 * KBLH + LH * FM)) / KeyboardHeight;
    // characters are px high and 10px wide
    const int text_offset_x = key_width / 2 - LW * FM / 2;
    const int text_offset_y = key_height / 2 - LH * FP / 2; // Centralize the characters in the key box

#if defined(HAS_TOUCH) // filling touch box list
    // Calculate actual box count
    const int keyboard_boxes = KeyboardHeight * KeyboardWidth;
    const int box_count = keyboard_boxes + buttons_number;

    box_t box_list[box_count];

    int k = 0;
    // Setup keyboard touch boxes
    for (int i = 0; i < KeyboardWidth; i++) {      // x coord
        for (int j = 0; j < KeyboardHeight; j++) { // y coord
            box_list[k].key = keys[j][i][0];
            box_list[k].key_sh = keys[j][i][1];
            box_list[k].color = ~BGCOLOR;
            box_list[k].x = i * key_width;
            box_list[k].y = j * key_height + 2 * KBLH + LH * FM;
            box_list[k].w = key_width;
            box_list[k].h = key_height;
            k++;
        }
    }
    const int buttons_start_index = k;
    // Setup buttons_strings touch boxes
    for (int i = 0; i < buttons_number; i++) {
        box_list[k].key = ' ';
        box_list[k].key_sh = ' ';
        box_list[k].color = ~BGCOLOR;
        box_list[k].x = btns_layout[i][0];
        box_list[k].y = 0;
        box_list[k].w = btns_layout[i][1];
        box_list[k].h = KBLH + 2;
        k++;
    }

    k = 0;
#endif

    tft->fillScreen(BGCOLOR); // reset the screen

#if defined(HAS_3_BUTTONS) // StickCs and Core for long press detection logic
    uint8_t longNextPress = 0;
    uint8_t longPrevPress = 0;
    unsigned long LongPressTmp = millis();
#endif

    // main loop
    while (1) {
        if (redraw) {
#ifdef E_PAPER_DISPLAY
            tft->stopCallback();
#endif
            // setup
            tft->setCursor(0, 0);
            tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
            tft->setTextSize(FM);

            // Draw the top row buttons_strings
            if (y < 0 || old_y < 0) {
                tft->fillRect(0, 1, tftWidth, KBLH, BGCOLOR);
                // Draw the buttons_strings borders
                for (int i = 0; i < buttons_number; ++i) {
                    tft->drawRect(
                        btns_layout[i][0], 2, btns_layout[i][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                }

                /* Highlight the corresponding button when the user cursor is over it */
                // OK
                if (x == 0 && y == -1) {
                    tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR));
                    tft->fillRect(
                        btns_layout[0][0], 2, btns_layout[0][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                } else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                tft->drawString("OK", btns_layout[0][2], 5);
                // CAP
                if (x == 1 && y == -1) {
                    tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR));
                    tft->fillRect(
                        btns_layout[1][0], 2, btns_layout[1][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                } else if (caps) {
                    tft->fillRect(btns_layout[1][0], 2, btns_layout[1][1], KBLH, DARKGREY);
                    tft->setTextColor(getComplementaryColor(BGCOLOR), DARKGREY);
                    tft->drawString("ab", btns_layout[1][2], 5);
                } else {
                    tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                    tft->drawString("A@", btns_layout[1][2], 5);
                }

                // DEL
                if (x == 2 && y == -1) {
                    tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR));
                    tft->fillRect(
                        btns_layout[2][0], 2, btns_layout[2][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                } else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                tft->drawString("<-", btns_layout[2][2], 5);
                // SPACE
                if (x == 3 && y == -1) {
                    tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR));
                    tft->fillRect(
                        btns_layout[3][0], 2, btns_layout[3][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                } else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                tft->drawString("[_]", btns_layout[3][2], 5);
                //   BACK
                if (x > 3 && y == -1) {
                    tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR));
                    tft->fillRect(
                        btns_layout[4][0], 2, btns_layout[4][1], KBLH, getComplementaryColor(BGCOLOR)
                    );
                } else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                tft->drawString("Esc", btns_layout[4][2], 5);
            }

            // Prints the chars counter
            tft->setTextSize(FP);
            String chars_counter = String(current_text.length()) + "/" + String(max_size);
            tft->fillRect(
                tftWidth - ((chars_counter.length() * LW * FP) + 20), // 6px per char + 1 padding
                KBLH + 4,
                (chars_counter.length() * LW * FP) + 20,
                7,
                BGCOLOR
            ); // clear previous text
            tft->drawString(chars_counter, tftWidth - ((chars_counter.length() * LW * FP) + 10), KBLH + 4);

            tft->drawString(
                textbox_title.substring(0, max_FP_size - chars_counter.length() - 1), 3, KBLH + 4
            );
            // Drawing the textbox and the currently typed string
            tft->setTextSize(FM);
            // reset the text box if needed
            if (current_text.length() == (max_FM_size) || current_text.length() == (max_FM_size + 1) ||
                current_text.length() == (max_FP_size) || current_text.length() == (max_FP_size + 1))
                tft->fillRect(3, KBLH + LH * FP + 4, tftWidth - 3, KBLH, BGCOLOR);
            // typed string border
            tft->drawRect(3, KBLH + LH * FP + 4, tftWidth - 3, KBLH, FGCOLOR);
            // write the text
            if (current_text.length() >
                max_FM_size) { // if the text is too long, we try to set the smaller font
                tft->setTextSize(FP);
                if (current_text.length() >
                    max_FP_size) { // if its still too long, we divide it into two lines
                    tft->drawString(current_text.substring(0, max_FP_size), 5, KBLH + LH * FP + 6);
                    tft->drawString(
                        current_text.substring(max_FP_size, current_text.length()), 5, KBLH + 2 * LH * FP + 6
                    );
                } else {
                    tft->drawString(current_text, 5, KBLH + LH * FP + 6);
                }
            } else {
                // else if it fits, just draw the text
                tft->drawString(current_text, 5, KBLH + LH * FP + 6);
            }

            tft->setTextSize(FM);
            // Draw the actual keyboard
            for (int i = 0; i < KeyboardHeight; i++) {
                for (int j = 0; j < KeyboardWidth; j++) {
                    // key coordinates
                    int key_x = j * key_width;
                    int key_y = i * key_height + KBLH * 2 + LH * FP + 6;

                    // Use the previous coordinates to redraw only the previous letter
                    if (old_x == j && old_y == i) {
                        tft->setTextColor(~BGCOLOR, BGCOLOR);
                        tft->fillRect(key_x, key_y, key_width, key_height, BGCOLOR);
                    }
                    // If selected, highlight it by changing font color and filling the back rectangle
                    if (x == j && y == i) {
                        tft->setTextColor(BGCOLOR, ~BGCOLOR);
                        tft->fillRect(key_x, key_y, key_width, key_height, ~BGCOLOR);
                    }

                    // Print the letters
                    if (!caps)
                        tft->drawChar2(
                            key_x + text_offset_x,
                            key_y + 2 + text_offset_y,
                            keys[i][j][0],
                            x == j && y == i ? BGCOLOR : FGCOLOR,
                            x == j && y == i ? ~BGCOLOR : BGCOLOR
                        );
                    else
                        tft->drawChar2(
                            key_x + text_offset_x,
                            key_y + 2 + text_offset_y,
                            keys[i][j][1],
                            x == j && y == i ? BGCOLOR : FGCOLOR,
                            x == j && y == i ? ~BGCOLOR : BGCOLOR
                        );

                    // Return colors to normal to print the other letters
                    if (x == j && y == i) { tft->setTextColor(~BGCOLOR, BGCOLOR); }
                }
            }
            // backup key coordinates
            old_x = x;
            old_y = y;
            redraw = false;
            tft->display(false);
#ifdef E_PAPER_DISPLAY
            tft->startCallback();
#if defined(USE_M5GFX)
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
#endif
#endif
        }

        // Cursor Handler
        if (current_text.length() > max_FM_size) {
            tft->setTextSize(FP);
            if (current_text.length() > (max_FP_size)) {
                cursor_y = KBLH + 2 * LH * FP + 6;
                cursor_x = 5 + (current_text.length() - max_FP_size) * LW * FP;
            } else {
                cursor_y = KBLH + LH * FP + 6;
                cursor_x = 5 + current_text.length() * LW * FP;
            }
        } else {
            cursor_y = KBLH + LH * FP + 6;
            cursor_x = 5 + current_text.length() * LW * FM;
        }

        if (millis() - last_input_time > 250) { // INPUT DEBOUCING
            // waits at least 250ms before accepting another input, to prevent rapid involuntary repeats

#if defined(HAS_TOUCH) // CYD, Core2, CoreS3
#if defined(DONT_USE_INPUT_TASK)
            check(AnyKeyPress);
#endif
            if (touchPoint.pressed) {
                // If using touchscreen and buttons_strings, reset the navigation states to avoid inconsistent
                // behavior, and reset the navigation coords to the OK button.
                SelPress = false;
                EscPress = false;
                NextPress = false;
                PrevPress = false;
                UpPress = false;
                DownPress = false;
                x = 0;
                y = -1;

                bool touchHandled = false;

                if (box_list[buttons_start_index].contain(touchPoint.x, touchPoint.y)) { // OK btn
                    break;
                }
                if (box_list[buttons_start_index + 1].contain(touchPoint.x, touchPoint.y)) { // CAPS btn
                    caps = !caps;
                    tft->fillRect(0, 54, tftWidth, tftHeight - 54, BGCOLOR);
                    touchHandled = true;
                }
                if (box_list[buttons_start_index + 2].contain(touchPoint.x, touchPoint.y)) { // DEL btn
                    if (current_text.length() > 0) {
                        handleDelete(current_text, cursor_x, cursor_y);
                        touchHandled = true;
                    }
                }
                if (box_list[buttons_start_index + 3].contain(touchPoint.x, touchPoint.y)) { // SPACE btn
                    if (current_text.length() < max_size) {
                        handleSpaceAdd(current_text, max_size);
                        touchHandled = true;
                    }
                }
                if (box_list[buttons_start_index + 4].contain(touchPoint.x, touchPoint.y)) { // BACK btn
                    current_text = KEY_ESCAPE; // ASCII ESC CHARACTER
                    break;
                }
                for (k = 0; k < keyboard_boxes; k++) {
                    if (box_list[k].contain(touchPoint.x, touchPoint.y)) {
                        if (caps)
                            handleCharacterAdd(
                                current_text, box_list[k].key_sh, cursor_x, cursor_y, max_size
                            );
                        else handleCharacterAdd(current_text, box_list[k].key, cursor_x, cursor_y, max_size);
                        touchHandled = true;
                        break;
                    }
                }

                if (touchHandled) {
                    wakeUpScreen();
                    touchPoint.Clear();
                    redraw = true;
                }
            }
#endif

#if defined(HAS_3_BUTTONS) // StickCs and Core
            if (check(SelPress)) {
                selection_made = true;
            } else {
                /* Down Btn to move in X axis (to the right) */
                if (longNextPress || NextPress) {
                    unsigned long now = millis();
                    if (!longNextPress) {
                        longNextPress = 1;
                        LongPress = true;
                        LongPressTmp = now;
                    }
                    delay(1); // does not work without it
                    // Check if the button is held long enough (long press)
                    if (now - LongPressTmp > 300) {
                        x--; // Long press action
                        longNextPress = 2;
                        LongPress = false;
                        check(NextPress);
                        LongPressTmp = now;
                    } else if (!NextPress) {
                        if (longNextPress != 2) x++; // Short press action
                        longNextPress = 0;
                    } else {
                        continue;
                    }
                    LongPress = false;
                    // delay(10);
                    if (y < 0 && x >= buttons_number) x = 0;
                    if (x >= KeyboardWidth) x = 0;
                    else if (x < 0) x = KeyboardWidth - 1;
                    redraw = true;
                }
                /* UP Btn to move in Y axis (Downwards) */
                if (longPrevPress || PrevPress) {
                    unsigned long now = millis();
                    if (!longPrevPress) {
                        longPrevPress = 1;
                        LongPress = true;
                        LongPressTmp = now;
                    }
                    delay(1); // does not work without it
                    // Check if the button is held long enough (long press)
                    if (now - LongPressTmp > 300) {
                        y--; // Long press action
                        longPrevPress = 2;
                        LongPress = false;
                        check(PrevPress);
                        LongPressTmp = now;
                    } else if (!PrevPress) {
                        if (longPrevPress != 2) y++; // Short press action
                        longPrevPress = 0;
                    } else {
                        continue;
                    }
                    LongPress = false;
                    if (y >= KeyboardHeight) {
                        y = -1;
                    } else if (y < -1) y = KeyboardHeight - 1;
                    redraw = true;
                }
            }
#elif defined(HAS_5_BUTTONS) // Smoochie and Marauder-Mini
            if (check(SelPress)) {
                selection_made = true;
            } else {
                /* Down Btn to move in X axis (to the right) */
                if (check(NextPress)) {
                    x++;
                    if ((y < 0 && x >= buttons_number) || x >= KeyboardWidth) x = 0;
                    redraw = true;
                }
                if (check(PrevPress)) {
                    x--;
                    if (y < 0 && x >= buttons_number) x = buttons_number - 1;
                    else if (x < 0) x = KeyboardWidth - 1;
                    redraw = true;
                }
                /* UP Btn to move in Y axis (Downwards) */
                if (check(DownPress)) {
                    y++;
                    if (y > KeyboardHeight - 1) { y = -1; }
                    redraw = true;
                }
                if (check(UpPress)) {
                    y--;
                    if (y < -1) y = KeyboardHeight - 1;
                    redraw = true;
                }
            }
#elif defined(HAS_KEYBOARD)  // Cardputer, T-Deck and T-LoRa-Pager
            if (KeyStroke.pressed) {
                wakeUpScreen();
                tft->setCursor(cursor_x, cursor_y);
                tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
                String keyStr = "";
                for (auto i : KeyStroke.word) {
                    if (keyStr != "") {
                        keyStr = keyStr + "+" + i;
                    } else {
                        keyStr += i;
                    }
                }
                if (KeyStroke.fn && KeyStroke.exit_key) {
                    current_text = KEY_ESCAPE;
                    break;
                }

                if (current_text.length() < max_size && !KeyStroke.enter && !KeyStroke.del) {
                    current_text += keyStr;
                    if (current_text.length() != (max_FM_size + 1) &&
                        current_text.length() != (max_FM_size + 1))
                        tft->print(keyStr.c_str());
                    cursor_x = tft->getCursorX();
                    cursor_y = tft->getCursorY();
                    if (current_text.length() == (max_FM_size + 1) ||
                        current_text.length() == (max_FP_size + 1))
                        redraw = true;
                }
                if (KeyStroke.del && current_text.length() > 0) { // delete 0x08
                    // Handle backspace key
                    current_text.remove(current_text.length() - 1);
                    int fontSize = FM;
                    if (current_text.length() > max_FP_size) {
                        tft->setTextSize(FP);
                        fontSize = FP;
                    } else tft->setTextSize(FM);
                    cursor_x = cursor_x - fontSize * LW;
                    tft->setCursor(cursor_x, cursor_y);
                    tft->fillRect(cursor_x, cursor_y, fontSize * LW, fontSize * LH, BGCOLOR);
                    if (current_text.length() == max_FM_size) redraw = true;
                    if (current_text.length() == max_FP_size) redraw = true;
                }
                if (KeyStroke.enter) { break; }
                KeyStroke.Clear();
                int _of = tft->getTextsize();
                tft->setTextSize(FP);
                String chars_counter = String(current_text.length()) + "/" + String(max_size);
                tft->drawString(
                    chars_counter, tftWidth - ((chars_counter.length() * LW * FP) + 10), KBLH + 4
                );
                tft->setTextSize(_of);
                tft->display(false);
#ifdef E_PAPER_DISPLAY
                tft->startCallback();
#endif
            }
#if !defined(T_LORA_PAGER) // T-LoRa-Pager does not have a select button
            if (check(SelPress)) break;
#endif
#endif

#if defined(HAS_ENCODER) // T-Embed and T-LoRa-Pager
#ifndef HAS_TOUCH
            LongPress = true;
#endif
            // WaveSentry has Encoder and Touchscreen
            // if touchscreen is pressed, ignore encoder input
            if ((check(SelPress) || selection_made) && touchPoint.pressed == false) {
                LongPress = false;
                selection_made = true;
            } else {
                /* NEXT "Btn" to move forward on th X axis (to the right) */
                // if ESC is pressed while NEXT or PREV is received, then we navigate on the Y axis instead
                if (check(NextPress) && touchPoint.pressed == false) {
                    if (EscPress) {
                        y++;
                    } else if ((x >= buttons_number - 1 && y <= -1) || (x >= KeyboardWidth - 1 && y >= 0)) {
                        // if we are at the end of the current line
                        y++;   // next line
                        x = 0; // reset to first key
                    } else x++;

                    if (y >= KeyboardHeight)
                        y = -1; // if we are at the end of the keyboard, then return to the top

                    // If we move to a new line using the ESC-press navigation and the previous x coordinate
                    // is greater than the number of available buttons_strings on the new line, reset x to
                    // avoid out-of-bounds behavior, this can only happen when switching to the first line, as
                    // the others have all the same number of keys
                    if (y == -1 && x >= buttons_number) x = 0;

                    redraw = true;
                }
                /* PREV "Btn" to move backwards on th X axis (to the left) */
                if (check(PrevPress) && touchPoint.pressed == false) {
                    if (EscPress) {
                        y--;
                    } else if (x <= 0) {
                        y--;
                        if (y == -1) x = buttons_number - 1;
                        else x = KeyboardWidth - 1;
                    } else x--;

                    if (y < -1) { // go back to the bottom right of the keyboard
                        y = KeyboardHeight - 1;
                        x = KeyboardWidth - 1;
                    }
                    // else if (y == -1 && x >= buttons_number) x = buttons_number - 1;
                    // else if (x < 0) x = KeyboardWidth - 1;

                    redraw = true;
                }
            }
#endif
        } // end of physical input detection

        if (selection_made) { // if something was selected then handle it
            selection_made = false;

            char selected_char = (y == -1) ? ' ' : keys[y][x][caps];

            if (selected_char == '\0') { continue; } // if we selected a key which have the value of

            KeyboardAction action = handleKeyboardSelection(
                x, y, current_text, caps, cursor_x, cursor_y, max_size, selected_char
            );

            if (action == KEYBOARD_OK) { // OK BTN
                break;
            } else if (action == KEYBOARD_CANCEL) { // BACK BTN
                current_text = KEY_ESCAPE;          // ASCII ESC CHARACTER
                break;
            } else if (action == KEYBOARD_REDRAW) {
                redraw = true;
            }

            last_input_time = millis();
        }
    }

    // Resets screen when finished writing
    tft->fillScreen(BGCOLOR);
    resetTftDisplay();
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
#endif
    return current_text;
}

/// This calls the QUERTY keyboard. Returns the user typed strings, return the ASCII ESC character
/// if the operation was cancelled
String keyboard(String current_text, int max_size, String textbox_title) {
    max_FM_size = tftWidth / (LW * FM) - 1;
    max_FP_size = tftWidth / (LW)-2;
    return generalKeyboard<qwerty_keyboard_height, qwerty_keyboard_width>(
        current_text, max_size, textbox_title, qwerty_keyset
    );
}

// /// This calls a keyboard with the characters useful to write hexadecimal codes.
// /// Returns the user typed strings, return the ASCII ESC character if the operation was cancelled
// String hex_keyboard(String current_text, int max_size, String textbox_title) {
//     return generalKeyboard<hex_keyboard_height, hex_keyboard_width>(
//         current_text, max_size, textbox_title, hex_keyset
//     );
// }

// /// This calls a numbers only keyboard. Returns the user typed strings, return the ASCII ESC character
// /// if the operation was cancelled
// String num_keyboard(String current_text, int max_size, String textbox_title) {
//     return generalKeyboard<numpad_keyboard_height, numpad_keyboard_width>(
//         current_text, max_size, textbox_title, numpad_keyset
//     );
// }

/* Turns off device */
void powerOff() {}

/* Verifies if the appropriate btn was pressed to turn off device */
void checkReboot() {}
/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 0-100
***************************************************************************************/
int getBattery() {
#ifdef ANALOG_BAT_PIN
#ifndef ANALOG_BAT_MULTIPLIER
#define ANALOG_BAT_MULTIPLIER 2.0f
#endif
    static bool adcInitialized = false;
    if (!adcInitialized) {
        pinMode(ANALOG_BAT_PIN, INPUT);
        adcInitialized = true;
    }
    uint32_t adcReading = analogReadMilliVolts(ANALOG_BAT_PIN);
    float actualVoltage = (float)adcReading * ANALOG_BAT_MULTIPLIER;
    const float MIN_VOLTAGE = 3300.0f;
    const float MAX_VOLTAGE = 4150.0f;
    float percent = ((actualVoltage - MIN_VOLTAGE) / (MAX_VOLTAGE - (MIN_VOLTAGE + 50.0f))) * 100.0f;

    if (percent < 0) percent = 1;
    if (percent > 100) percent = 100;
    return (int)percent;
#endif
    return 0;
}
