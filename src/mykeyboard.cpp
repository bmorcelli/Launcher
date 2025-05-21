#include "mykeyboard.h"
#include "display.h"
#include <globals.h>
#include "settings.h"
#include "powerSave.h"

/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() { return 0; }


struct box_t
{
  int x;
  int y;
  int w;
  int h;
  std::uint16_t color;
  int touch_id = -1;
  char key;
  char key_sh;

  void clear(void)
  {
    for (int i = 0; i < 8; ++i)
    {
      tft->fillRect(x, y, w, h,BGCOLOR);
    }
  }
  void draw(bool shift, bool sel=false)
  {
    uint16_t c=FGCOLOR;
    uint16_t b=BGCOLOR;
    if(sel) {
      c=BGCOLOR;
      b=FGCOLOR;
    }
    int ie = touch_id < 0 ? 4 : 8;
    for (int i = 0; i < ie; ++i)
    {
      tft->drawRect(x, y, w, h,color);
      tft->setTextColor(color);
      if(shift) tft->drawChar2(x+w/2-FM*LW/2,y+h/2-FM*LH/2,key_sh,c,b);
      else tft->drawChar2(x+w/2-FM*LW/2,y+h/2-FM*LH/2,key,c,b);
    }
  }
  bool contain(int x, int y)
  {
    return this->x <= x && x < (this->x + this->w)
        && this->y <= y && y < (this->y + this->h);
  }
};

static constexpr std::size_t box_count = 52;
static box_t box_list[box_count];


String keyboard(String mytext, int maxSize, String msg) {
  resetTftDisplay();
  touchPoint.Clear();
  String _mytext = mytext;
  bool caps=false;
  bool redraw=true;
  long holdCode=millis(); //to hold the inputs for 250ms before adding other letter
  int cX =0; // Cursor position
  int cY =0; // Cursor position
  int x=0;
  int y=-1; // -1 is where buttons are, out of keys[][][] array
  int z=0;
  int x2=0;
  int y2=0;
  //       [x][y] [z], x2 and y2 are the previous position of x and y, used to redraw only that spot on keyboard screen
  char keys[4][12][2] = { //4 lines, with 12 characteres, low and high caps
    {
      { '1', '!' },//1
      { '2', '@' },//2
      { '3', '#' },//3
      { '4', '$' },//4
      { '5', '%' },//5
      { '6', '^' },//6
      { '7', '&' },//7
      { '8', '*' },//8
      { '9', '(' },//9
      { '0', ')' },//10
      { '-', '_' },//11
      { '=', '+' } //12
     },
    {
      { 'q', 'Q' },//1
      { 'w', 'W' },//2
      { 'e', 'E' },//3
      { 'r', 'R' },//4
      { 't', 'T' },//5
      { 'y', 'Y' },//6
      { 'u', 'U' },//7
      { 'i', 'I' },//8
      { 'o', 'O' },//9
      { 'p', 'P' },//10
      { '[', '{' },//11
      { ']', '}' } //12
    },
    {
      { 'a', 'A' },//1
      { 's', 'S' },//2
      { 'd', 'D' },//3
      { 'f', 'F' },//4
      { 'g', 'G' },//5
      { 'h', 'H' },//6
      { 'j', 'J' },//7
      { 'k', 'K' },//8
      { 'l', 'L' },//9
      { ';', ':' },//10
      { '"', '\'' },//11
      { '|', '\\' } //12
    },
    {
      { '\\', '|' },//1
      { 'z', 'Z' },//2
      { 'x', 'X' },//3
      { 'c', 'C' },//4
      { 'v', 'V' },//5
      { 'b', 'B' },//6
      { 'n', 'N' },//7
      { 'm', 'M' },//8
      { ',', '<' },//9
      { '.', '>' },//10
      { '?', '/' },//11
      { '/', '/' } //12
    }
  };
  #if defined(E_PAPER_DISPLAY)
    #define KBLH LH*FM+LH/2
    const int ofs[4][3] = {
      {7            , 3*LW*FM, 7+  0        + LW/2},
      {7+3*LW*FM+2  , 4*LW*FM, 7+3 *LW*FM+2 + LW/2},
      {7+7*LW*FM+4  , 4*LW*FM, 7+7 *LW*FM+4 + LW/2},
      {7+11*LW*FM+6 , 6*LW*FM, 7+11*LW*FM+6 + LW/2},
    };

  #elif FM>1 // Normal keyboard size
  //#if FM>1 // Normal keyboard size
    #define KBLH 20
    const int ofs[4][3] = {
      {7  , 46, 18 },
      {55 , 50, 64 },
      {107, 50, 115},
      {159, 74, 168},
    };
  #else // small keyboard size, for small letters (smaller screen)
    #define KBLH 10
    const int ofs[4][3] = {
      {7 , 20, 10},
      {27, 25, 30},
      {52, 25, 55},
      {77, 50, 80},
    };
  #endif
  const int _x = tftWidth/12;
  const int _y = (tftHeight - (2*KBLH + 14))/4;
  const int _xo = _x/2-3;
#if defined(HAS_TOUCH)
  int k=0;
  for(x2=0; x2<12;x2++) {
    for(y2=0; y2<4; y2++) {
      box_list[k].key=keys[y2][x2][0];
      box_list[k].key_sh=keys[y2][x2][1];
      box_list[k].color = ~BGCOLOR;
      box_list[k].x=x2*_x;
      box_list[k].y=y2*_y+2*KBLH+LH*FM;
      box_list[k].w=_x;
      box_list[k].h=_y;
      k++;
    }
  }
  // OK
  box_list[k].key=' ';
  box_list[k].key_sh=' ';
  box_list[k].color = ~BGCOLOR;
  box_list[k].x=ofs[0][0];
  box_list[k].y=0;
  box_list[k].w=ofs[0][1];
  box_list[k].h=KBLH;
  k++;
  // CAP
  box_list[k].key=' ';
  box_list[k].key_sh=' ';
  box_list[k].color = ~BGCOLOR;
  box_list[k].x=ofs[1][0];
  box_list[k].y=0;
  box_list[k].w=ofs[1][1];
  box_list[k].h=KBLH;
  k++;
  // DEL
  box_list[k].key=' ';
  box_list[k].key_sh=' ';
  box_list[k].color = ~BGCOLOR;
  box_list[k].x=ofs[2][0];
  box_list[k].y=0;
  box_list[k].w=ofs[2][1];
  box_list[k].h=KBLH;
  k++;
  // SPACE
  box_list[k].key=' ';
  box_list[k].key_sh=' ';
  box_list[k].color = ~BGCOLOR;
  box_list[k].x=ofs[3][0];
  box_list[k].y=0;
  box_list[k].w=ofs[3][1];
  box_list[k].h=KBLH;

  k=0;
  x2=0;
  y2=0;
#endif
  tft->drawPixel(0,0,0);
  tft->fillScreen(BGCOLOR);

#if defined(HAS_3_BUTTONS) // StickCs and Core for long press detection logic
  bool longNextPress = false;
  long longPressTmp=millis();
#endif  
  const int maxFMSize = tftWidth/(LW*FM)-1;
  const int maxFPSize = tftWidth/(LW)-2;
  while(1) {
    if(redraw) {
    #ifdef E_PAPER_DISPLAY
      tft->stopCallback();
    #endif
      tft->setCursor(0,0);
      tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
      tft->setTextSize(FM);

      //Draw the rectangles
      if(y<0 || y2<0) {

        tft->fillRect(0,1,tftWidth,KBLH,BGCOLOR);
        tft->drawRect(ofs[0][0],2,ofs[0][1],KBLH,getComplementaryColor(BGCOLOR));       // Ok Rectangle
        tft->drawRect(ofs[1][0],2,ofs[1][1],KBLH,getComplementaryColor(BGCOLOR));      // CAP Rectangle
        tft->drawRect(ofs[2][0],2,ofs[2][1],KBLH,getComplementaryColor(BGCOLOR));     // DEL Rectangle
        tft->drawRect(ofs[3][0],2,ofs[3][1],KBLH,getComplementaryColor(BGCOLOR));     // SPACE Rectangle
        tft->drawRect(3,KBLH+12,tftWidth-3,KBLH,FGCOLOR); // mystring Rectangle


        if(x==0 && y==-1) { tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR)); tft->fillRect(ofs[0][0],2,ofs[0][1],KBLH,getComplementaryColor(BGCOLOR)); }
        else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
        tft->drawString("OK",ofs[0][2], 4);


        if(x==1 && y==-1) { tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR)); tft->fillRect(ofs[1][0],2,ofs[1][1],KBLH,getComplementaryColor(BGCOLOR)); }
        else if(caps) { tft->fillRect(ofs[1][0],2,ofs[1][1],KBLH,DARKGREY); tft->setTextColor(getComplementaryColor(BGCOLOR), DARKGREY); }
        else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
        tft->drawString("CAP", ofs[1][2], 4);


        if(x==2 && y==-1) { tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR)); tft->fillRect(ofs[2][0],2,ofs[2][1],KBLH,getComplementaryColor(BGCOLOR)); }
        else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
        tft->drawString("DEL", ofs[2][2], 4);

        if(x>2 && y==-1) { tft->setTextColor(BGCOLOR, getComplementaryColor(BGCOLOR)); tft->fillRect(ofs[3][0],2,ofs[3][1],KBLH,getComplementaryColor(BGCOLOR)); }
        else tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
        tft->drawString("SPACE", ofs[3][2], 4);
      }
      tft->setTextSize(FP);
      tft->setTextColor(getComplementaryColor(BGCOLOR), 0x5AAB);

      tft->drawString(msg.substring(0,maxFPSize), 3, KBLH+4);

      tft->setTextSize(FM);


      // reseta o quadrado do texto
      if (mytext.length() == (maxFMSize) || mytext.length() == (maxFMSize+1) || mytext.length() == (maxFPSize) || mytext.length() == (maxFPSize+1)) tft->fillRect(3,KBLH+12,tftWidth-3,KBLH,BGCOLOR); // mystring Rectangle
      // escreve o texto
      tft->setTextColor(getComplementaryColor(BGCOLOR));
      if(mytext.length()>(maxFMSize)) {
        tft->setTextSize(FP);
        if(mytext.length()>maxFPSize) {
          tft->drawString(mytext.substring(0,maxFPSize), 5, KBLH+LH+6);
          tft->drawString(mytext.substring(maxFPSize,mytext.length()), 5, KBLH+2*LH+6);
        }
        else {
          tft->drawString(mytext, 5, KBLH+LH+6);
        }
      } else {
        tft->drawString(mytext, 5, KBLH+LH+6);
      }
      //desenha o retangulo colorido
      tft->drawRect(3,KBLH+12,tftWidth-3,KBLH,FGCOLOR); // mystring Rectangle


      tft->setTextColor(getComplementaryColor(BGCOLOR), BGCOLOR);
      tft->setTextSize(FM);

      int _i=0;
      for(int i=0;i<4;i++) {
        for(int j=0;j<12;j++) {
          /* Print the letters */
        #ifdef HAS_TOUCH
          box_list[_i++].draw(caps,x==j && y==i?true:false);
        #else
          //use last coordenate to paint only this letter
          if(x2==j && y2==i) { tft->setTextColor(~BGCOLOR, BGCOLOR); tft->fillRect(j*_x,i*_y+KBLH*2+14,_x,_y,BGCOLOR);}
          /* If selected, change font color and draw Rectangle*/
          if(x==j && y==i) { tft->setTextColor(BGCOLOR, ~BGCOLOR); tft->fillRect(j*_x,i*_y+KBLH*2+14,_x,_y,~BGCOLOR);}
          if(!caps) tft->drawChar2((j*_x+_xo), (i*_y+2*KBLH+16),keys[i][j][0], x==j && y==i? BGCOLOR : FGCOLOR, x==j && y==i? ~BGCOLOR : BGCOLOR);
          else tft->drawChar2((j*_x+_xo), (i*_y+2*KBLH+16),keys[i][j][1], x==j && y==i? BGCOLOR : FGCOLOR, x==j && y==i? ~BGCOLOR : BGCOLOR);
        #endif
          /* Return colors to normal to print the other letters */
          if(x==j && y==i) { tft->setTextColor(~BGCOLOR, BGCOLOR); }
        }
      }
      // save actual key coordenate
      x2=x;
      y2=y;
      redraw = false;
    #ifdef E_PAPER_DISPLAY
      tft->startCallback();
      tft->display(false);
    #endif
    }

    //cursor handler
    if(mytext.length()>(maxFMSize)) {
      tft->setTextSize(FP);
      if(mytext.length()>(maxFPSize)) {
        cY=KBLH+2*LH+6;
        cX=5+(mytext.length()-maxFPSize)*LW;
      }
      else {
        cY=KBLH+LH+6;
        cX=5+mytext.length()*LW;
      }
    } else {
      cY=KBLH+LH+6;
      cX=5+mytext.length()*LW*FM;
    }

    if(millis()-holdCode>250) { // allow reading inputs
  
    #if defined(HAS_TOUCH) // CYD, Core2, CoreS3
      #if defined(DONT_USE_INPUT_TASK)
        check(AnyKeyPress);
      #endif
      if (touchPoint.pressed)
      {
        SelPress = false;
        EscPress = false;
        NextPress = false;
        PrevPress = false;
        DownPress = false;
        UpPress = false;

        if (box_list[48].contain(touchPoint.x, touchPoint.y)) { break; }      // Ok
        if (box_list[49].contain(touchPoint.x, touchPoint.y)) { caps=!caps; tft->fillRect(0,54,tftWidth,tftHeight-54,BGCOLOR); goto THIS_END; } // CAP
        if (box_list[50].contain(touchPoint.x, touchPoint.y)) goto DEL;               // DEL
        if (box_list[51].contain(touchPoint.x, touchPoint.y)) { mytext += " "; goto THIS_END; } // SPACE
        for(k=0;k<48;k++){
          if (box_list[k].contain(touchPoint.x, touchPoint.y)) {
            if(caps) mytext += box_list[k].key_sh;
            else mytext += box_list[k].key;
          }
        }
        wakeUpScreen();
        THIS_END:
        touchPoint.Clear();
        redraw=true;
      }
    #endif

    #if defined(HAS_3_BUTTONS) // StickCs and Core
      if(check(SelPress))  {
        goto SELECT;
      }
      /* Down Btn to move in X axis (to the right) */
      if(longNextPress || NextPress) {
        if(!longNextPress) {
          longNextPress = true;
          longPressTmp = millis();
        }
        if(longNextPress && millis()-longPressTmp<200) goto WAITING;
        longNextPress=false;

        if(check(NextPress)) { x--;  /* delay(250); */ } // Long Press
        else x++; // Short Press
        if(y<0 && x>3) x=0;
        if(x>11) x=0;
        else if (x<0) x=11;
        redraw = true;
      }
      /* UP Btn to move in Y axis (Downwards) */
      if(LongPress || PrevPress) {
        if(!LongPress) {
          LongPress = true;
          longPressTmp = millis();
        }
        if(LongPress && millis()-longPressTmp<200) goto WAITING;
        LongPress=false;

        if(check(PrevPress)) { y--; /* delay(250); */ } // Long press
        else y++; // short press
        if(y>3) { y=-1; }
        else if(y<-1) y=3;
        redraw = true;
      }
    #elif defined (HAS_5_BUTTONS) // Smoochie and Marauder-Mini
      if(check(SelPress))  {
        goto SELECT;
      }
      /* Down Btn to move in X axis (to the right) */
      if(check(NextPress))
      {
        x++;
        if((y<0 && x>3) || x>11) x=0;
        redraw = true;
      }
      if(check(PrevPress))
      {
        x--;
        if(y<0 && x>3) x=3;
        else if(x<0) x=11;
        redraw = true;
      }
      /* UP Btn to move in Y axis (Downwards) */
      if(check(DownPress)) {    
        y++;
        if(y>3) { y=-1; }
        redraw = true;
      }
      if(check(UpPress)) {    
        y--;
        if(y<-1) y=3;
        redraw = true;
      }

    #elif defined (HAS_ENCODER) // T-Embed
      if(check(SelPress))  {
        goto SELECT;
      }
      /* Down Btn to move in X axis (to the right) */
      if(check(NextPress))
      {
        if(check(EscPress)) { y++; }
        else if ((x >= 3 && y < 0) || x == 11) { y++; x = 0; } 
        else x++;

        if (y > 3) y = -1;
        redraw = true;
      }
      /* UP Btn to move in Y axis (Downwards) */
      if(check(PrevPress)) {
        if(check(EscPress)) { y--; }
        else if(x==0) { y--; x--; }
        else x--;

        if(y<-1) { y=3; x=11; }
        else if(y<0 && x<0) x=3;
        else if (x<0) x=11;
        
        redraw = true;
      }

    #elif defined (HAS_KEYBOARD) // Cardputer and T-Deck
        if (KeyStroke.pressed) {
          wakeUpScreen();
          tft->setCursor(cX,cY);
          String keyStr = "";
          for (auto i : KeyStroke.word) {
            if (keyStr != "") {
              keyStr = keyStr + "+" + i;
            } else {
              keyStr += i;
            }
          }

          if(mytext.length()<maxSize && !KeyStroke.enter && !KeyStroke.del) {
            mytext += keyStr;
            if(mytext.length()!=(maxFMSize+1) && mytext.length()!=(maxFMSize+1)) tft->print(keyStr.c_str());
            cX=tft->getCursorX();
            cY=tft->getCursorY();
            if(mytext.length()==(maxFMSize+1)) redraw = true;
            if(mytext.length()==(maxFPSize+1)) redraw = true;
          }
          if (KeyStroke.del && mytext.length() > 0) { // delete 0x08
            // Handle backspace key
            mytext.remove(mytext.length() - 1);
            int fS=FM;
            if(mytext.length()>maxFPSize) { tft->setTextSize(FP); fS=FP; }
            else tft->setTextSize(FM);
            tft->setCursor((cX-fS*LW),cY);
            tft->setTextColor(FGCOLOR,BGCOLOR);
            tft->print(" "); 
            tft->setTextColor(getComplementaryColor(BGCOLOR), 0x5AAB);
            tft->setCursor(cX-fS*LW,cY);
            cX=tft->getCursorX();
            cY=tft->getCursorY();
            if(mytext.length()==maxFMSize) redraw = true;
            if(mytext.length()==maxFPSize) redraw = true;        
          }
          if (KeyStroke.enter) {
            break;
          }
          KeyStroke.Clear();
      }
      if(check(SelPress)) break;

    #endif
    } // end of holdCode detection

    if(false) { // When selecting some letter or something, use these goto addresses(ADD, DEL)
      SELECT:
        tft->setCursor(cX,cY);
        if(caps) z=1;
        else z=0;
        if(x==0 && y==-1) break; // Ok key,
        else if(x==1 && y==-1) caps=!caps; // CAPS key
        else if(x==2 && y==-1 && mytext.length() > 0) { // DEL key
          DEL:
          mytext.remove(mytext.length()-1);
          int fS=FM;
          if(mytext.length()>maxFPSize) { tft->setTextSize(FP); fS=FP; }
          else tft->setTextSize(FM);
          tft->setCursor((cX-fS*LW),cY);
          tft->setTextColor(FGCOLOR,BGCOLOR);
          tft->print(" ");
          tft->setTextColor(getComplementaryColor(BGCOLOR), 0x5AAB);
          tft->setCursor(cX-fS*LW,cY);
          cX=tft->getCursorX();
          cY=tft->getCursorY();
        }
        else if(x>2 && y==-1 && mytext.length()<maxSize) mytext += " "; // SPACE key
        else if(y>-1 && mytext.length()<maxSize) { // Letters
          ADD:
          mytext += keys[y][x][z];
          if(mytext.length()!=(maxFMSize+1) && mytext.length()!=(maxFMSize+1)) tft->print(keys[y][x][z]);
          cX=tft->getCursorX();
          cY=tft->getCursorY();
        }
        redraw = true;
        holdCode=millis();
    }
    WAITING: // Used in long press detection
    yield();

  }

  //Resets screen when finished writing
  tft->fillRect(0,0,tftWidth,tftHeight,BGCOLOR);
  resetTftDisplay();

  return mytext;
}


// This will get the value from InputHandler and read add into loopTask, 
// reseting the value after used
keyStroke _getKeyPress() { 
  vTaskSuspend( xHandle );
  keyStroke key=KeyStroke;
  KeyStroke.Clear();
  delay(10);
  vTaskResume( xHandle );
  return key; 
} 

/* Turns off device */
void powerOff() { }

/* Verifies if the appropriate btn was pressed to turn off device */
void checkReboot() { }