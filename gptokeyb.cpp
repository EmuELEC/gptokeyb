/* Copyright (c) 2021
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
#
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
#
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301 USA
#
* Authored by: Kris Henriksen <krishenriksen.work@gmail.com>
#
* AnberPorts-Keyboard-Mouse
* 
* Part of the code is from from https://github.com/krishenriksen/AnberPorts/blob/master/AnberPorts-Keyboard-Mouse/main.c (mostly the fake keyboard)
* Fake Xbox code from: https://github.com/Emanem/js2xbox
* 
* Modified (badly) by: Shanti Gilbert for EmuELEC
* Modified further by: Nikolai Wuttke for EmuELEC (Added support for SDL and the SDLGameControllerdb.txt)
* 
* Any help improving this code would be greatly appreciated! 
* 
* DONE: Xbox360 mode: Fix triggers so that they report from 0 to 255 like real Xbox triggers
*       Xbox360 mode: Figure out why the axis are not correctly labeled?  SDL_CONTROLLER_AXIS_RIGHTX / SDL_CONTROLLER_AXIS_RIGHTY / SDL_CONTROLLER_AXIS_TRIGGERLEFT / SDL_CONTROLLER_AXIS_TRIGGERRIGHT
*       Keyboard mode: Add a config file option to load mappings from.
*       add L2/R2 triggers
* 
* Spaghetti code incoming, beware :)
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>
#include <linux/uinput.h>

#include <libevdev-1.0/libevdev/libevdev-uinput.h>
#include <libevdev-1.0/libevdev/libevdev.h>

#include <fcntl.h>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <vector>

#include <SDL.h>

#define CONFIG_ARG_MAX_BYTES 128
#define SDL_DEFAULT_REPEAT_DELAY 500
#define SDL_DEFAULT_REPEAT_INTERVAL 30

struct config_option
{
  char key[CONFIG_ARG_MAX_BYTES];
  char value[CONFIG_ARG_MAX_BYTES];
};

std::vector<config_option> parseConfigFile(const char* path)
{
  std::vector<config_option> result;

  FILE* fp;

  if ((fp = fopen(path, "r+")) == NULL) {
    perror("fopen()");
    return result;
  }

  while (1) {
    result.emplace_back();
    auto& co = result.back();

    if (fscanf(fp, "%s = %s", &co.key[0], &co.value[0]) != 2) {
      if (feof(fp)) {
        break;
      }

      if (co.key[0] == '#') {
        while (fgetc(fp) != '\n') {
          // Do nothing (to move the cursor to the end of the line).
        }
        result.pop_back();
        continue;
      }
      
      perror("fscanf()");
      result.pop_back();
      continue;
    }
  }

  return result;
}

static int uinp_fd = -1;
struct uinput_user_dev uidev;

bool kill_mode = false;
bool sudo_kill = false; //allow sudo kill instead of killall for non-emuelec systems
bool pckill_mode = false; //emit alt+f4 to close apps on pc during kill mode, if env variable is set
bool openbor_mode = false;
bool xbox360_mode = false;
bool textinputpreset_mode = false; 
bool textinputinteractive_mode = false;
bool textinputinteractive_noautocapitals = false;
bool textinputinteractive_extrasymbols = false;
bool app_exult_adjust = false;

const int maxKeysNoExtendedSymbols = 69; //number of keys available for interactive text input
const int maxKeysWithSymbols = 96; //number of keys available for interactive text input with extra symbols
int maxKeys = maxKeysNoExtendedSymbols;
const int maxChars = 20; // length of text in characters that can be entered
int character_set[maxKeysWithSymbols]; // keys that can be selected in text input interactive mode
bool character_set_shift[maxKeysWithSymbols]; // indicate which keys require shift
int current_character = 0; 
int current_key[maxChars]; // current key selected for each key
char* AppToKill;
bool config_mode = false;
bool hotkey_override = false;
bool emuelec_override = false;
char* hotkey_code;

struct
{
  int hotkey_jsdevice;
  int start_jsdevice;
  int textinputinteractivetrigger_jsdevice; // to trigger text input interactive
  int textinputpresettrigger_jsdevice; // to trigger text input preset
  int textinputconfirmtrigger_jsdevice; // to trigger text input confirm via Enter key
  int mouseX = 0;
  int mouseY = 0;
  int current_left_analog_x = 0;
  int current_left_analog_y = 0;
  int current_right_analog_x = 0;
  int current_right_analog_y = 0;
  int current_l2 = 0;
  int current_r2 = 0;
  bool hotkey_pressed = false; // current state of hotkey
  bool hotkey_was_pressed = false; // indicates hotkey button has been pressed, and key may need to be processed on button's release, if hotkey combo isn't triggered
  bool start_pressed = false;
  bool start_was_pressed = false; // indicates start button has been pressed, and key may need to be processed on button's release, if start combo isn't triggered
  bool textinputinteractive_mode_active = false;
  bool textinputinteractivetrigger_pressed = false;
  bool textinputpresettrigger_pressed = false;
  bool textinputconfirmtrigger_pressed = false;
  bool left_analog_was_up = false;
  bool left_analog_was_down = false;
  bool left_analog_was_left = false;
  bool left_analog_was_right = false;
  bool right_analog_was_up = false;
  bool right_analog_was_down = false;
  bool right_analog_was_left = false;
  bool right_analog_was_right = false;
  bool a_hk_was_pressed = false;
  bool b_hk_was_pressed = false;
  bool x_hk_was_pressed = false;
  bool y_hk_was_pressed = false;
  bool l1_hk_was_pressed = false;
  bool r1_hk_was_pressed = false;
  bool l2_was_pressed = false;
  bool l2_hk_was_pressed = false;
  bool r2_was_pressed = false;
  bool r2_hk_was_pressed = false;
  bool hotkey_combo_triggered = false; //keep track of whether a hotkey combo was pressed; if so, don't send hotkey key when hotkey is released
  bool start_combo_triggered = false; //keep track of whether a start combo was pressed; if so, don't send start key when start is released
  short key_to_repeat = 0;
  SDL_TimerID key_repeat_timer_id = 0;
} state;

struct
{
  short back = KEY_ESC;
  bool back_repeat = false;
  short back_modifier = 0;
  short start = KEY_ENTER;
  bool start_repeat = false;
  short start_modifier = 0;
  short guide = KEY_ENTER;
  bool guide_repeat = false;
  short guide_modifier = 0;
  short a = KEY_X;
  short a_hk = KEY_ENTER;
  bool a_repeat = false;
  short a_modifier = 0;
  short a_hk_modifier = 0;
  short b = KEY_Z;
  short b_hk = KEY_ESC;
  bool b_repeat = false;
  short b_modifier = 0;
  short b_hk_modifier = 0;
  short x = KEY_C;
  short x_hk = KEY_C;
  bool x_repeat = false;
  short x_modifier = 0;
  short x_hk_modifier = 0;
  short y = KEY_A;
  short y_hk = KEY_A;
  bool y_repeat = false;
  short y_modifier = 0;
  short y_hk_modifier = 0;
  short l1 = KEY_RIGHTSHIFT;
  short l1_hk = KEY_ESC;
  bool l1_repeat = false;
  short l1_modifier = 0;
  short l1_hk_modifier = 0;
  short l2 = KEY_HOME;
  short l2_hk = KEY_HOME;
  bool l2_repeat = false;
  short l2_modifier = 0;
  short l2_hk_modifier = 0;
  short l3 = BTN_LEFT;
  bool l3_repeat = false;
  short l3_modifier = 0;
  short r1 = KEY_LEFTSHIFT;
  short r1_hk = KEY_ENTER;
  bool r1_repeat = false;
  short r1_modifier = 0;
  short r1_hk_modifier = 0;
  short r2 = KEY_END;
  short r2_hk = KEY_END;
  bool r2_repeat = false;
  short r2_modifier = 0;
  short r2_hk_modifier = 0;
  short r3 = BTN_RIGHT;
  bool r3_repeat = false;
  short r3_modifier = 0;
  short up = KEY_UP;
  bool up_repeat = false;
  short up_modifier = 0;
  short down = KEY_DOWN;
  bool down_repeat = false;
  short down_modifier = 0;
  short left = KEY_LEFT;
  bool left_repeat = false;
  short left_modifier = 0;
  short right = KEY_RIGHT;
  bool right_repeat = false;
  short right_modifier = 0;

  bool left_analog_as_mouse = false;
  bool right_analog_as_mouse = false;
  short left_analog_up = KEY_W;
  bool left_analog_up_repeat = false;
  short left_analog_up_modifier = 0;
  short left_analog_down = KEY_S;
  bool left_analog_down_repeat = false;
  short left_analog_down_modifier = 0;
  short left_analog_left = KEY_A;
  bool left_analog_left_repeat = false;
  short left_analog_left_modifier = 0;
  short left_analog_right = KEY_D;
  bool left_analog_right_repeat = false;
  short left_analog_right_modifier = 0;
  short right_analog_up = KEY_END;
  bool right_analog_up_repeat = false;
  short right_analog_up_modifier = 0;
  short right_analog_down = KEY_HOME;
  bool right_analog_down_repeat = false;
  short right_analog_down_modifier = 0;
  short right_analog_left = KEY_LEFT;
  bool right_analog_left_repeat = false;
  short right_analog_left_modifier = 0;
  short right_analog_right = KEY_RIGHT;
  bool right_analog_right_repeat = false;
  short right_analog_right_modifier = 0;

  int deadzone_y = 15000;
  int deadzone_x = 15000;
  int deadzone_triggers = 3000;

  int fake_mouse_scale = 512;
  int fake_mouse_delay = 16;

  Uint32 key_repeat_interval = SDL_DEFAULT_REPEAT_INTERVAL * 2; 
  Uint32 key_repeat_delay = SDL_DEFAULT_REPEAT_DELAY; 
  
  char* text_input_preset;
} config;

// convert ASCII chars to key codes
short char_to_keycode(const char* str)
{
  short keycode;

  // arrow keys
  if (strcmp(str, "up") == 0)
    keycode = KEY_UP;
  else if (strcmp(str, "down") == 0)
    keycode = KEY_DOWN;
  else if (strcmp(str, "left") == 0)
    keycode = KEY_LEFT;
  else if (strcmp(str, "right") == 0)
    keycode = KEY_RIGHT;

  // special keys
  else if (strcmp(str, "mouse_left") == 0)
    keycode = BTN_LEFT;
  else if (strcmp(str, "mouse_right") == 0)
    keycode = BTN_RIGHT;
  else if (strcmp(str, "space") == 0)
    keycode = KEY_SPACE;
  else if (strcmp(str, "esc") == 0)
    keycode = KEY_ESC;
  else if (strcmp(str, "end") == 0)
    keycode = KEY_END;
  else if (strcmp(str, "home") == 0)
    keycode = KEY_HOME;
  else if (strcmp(str, "shift") == 0)
    keycode = KEY_LEFTSHIFT;
  else if (strcmp(str, "leftshift") == 0)
    keycode = KEY_LEFTSHIFT;
  else if (strcmp(str, "rightshift") == 0)
    keycode = KEY_RIGHTSHIFT;
  else if (strcmp(str, "ctrl") == 0)
    keycode = KEY_LEFTCTRL;
  else if (strcmp(str, "leftctrl") == 0)
    keycode = KEY_LEFTCTRL;
  else if (strcmp(str, "rightctrl") == 0)
    keycode = KEY_RIGHTCTRL;
  else if (strcmp(str, "alt") == 0)
    keycode = KEY_LEFTALT;
  else if (strcmp(str, "leftalt") == 0)
    keycode = KEY_LEFTALT;
  else if (strcmp(str, "rightalt") == 0)
    keycode = KEY_RIGHTALT;
  else if (strcmp(str, "backspace") == 0)
    keycode = KEY_BACKSPACE;
  else if (strcmp(str, "enter") == 0)
    keycode = KEY_ENTER;
  else if (strcmp(str, "pageup") == 0)
    keycode = KEY_PAGEUP;
  else if (strcmp(str, "pagedown") == 0)
    keycode = KEY_PAGEDOWN;
  else if (strcmp(str, "insert") == 0)
    keycode = KEY_INSERT;
  else if (strcmp(str, "delete") == 0)
    keycode = KEY_DELETE;
  else if (strcmp(str, "capslock") == 0)
    keycode = KEY_CAPSLOCK;
  else if (strcmp(str, "tab") == 0)
    keycode = KEY_TAB;
  else if (strcmp(str, "pause") == 0)
    keycode = KEY_PAUSE;
  else if (strcmp(str, "menu") == 0)
    keycode = KEY_MENU;
    
  // normal keyboard
  else if (strcmp(str, "a") == 0)
    keycode = KEY_A;
  else if (strcmp(str, "b") == 0)
    keycode = KEY_B;
  else if (strcmp(str, "c") == 0)
    keycode = KEY_C;
  else if (strcmp(str, "d") == 0)
    keycode = KEY_D;
  else if (strcmp(str, "e") == 0)
    keycode = KEY_E;
  else if (strcmp(str, "f") == 0)
    keycode = KEY_F;
  else if (strcmp(str, "g") == 0)
    keycode = KEY_G;
  else if (strcmp(str, "h") == 0)
    keycode = KEY_H;
  else if (strcmp(str, "i") == 0)
    keycode = KEY_I;
  else if (strcmp(str, "j") == 0)
    keycode = KEY_J;
  else if (strcmp(str, "k") == 0)
    keycode = KEY_K;
  else if (strcmp(str, "l") == 0)
    keycode = KEY_L;
  else if (strcmp(str, "m") == 0)
    keycode = KEY_M;
  else if (strcmp(str, "n") == 0)
    keycode = KEY_N;
  else if (strcmp(str, "o") == 0)
    keycode = KEY_O;
  else if (strcmp(str, "p") == 0)
    keycode = KEY_P;
  else if (strcmp(str, "q") == 0)
    keycode = KEY_Q;
  else if (strcmp(str, "r") == 0)
    keycode = KEY_R;
  else if (strcmp(str, "s") == 0)
    keycode = KEY_S;
  else if (strcmp(str, "t") == 0)
    keycode = KEY_T;
  else if (strcmp(str, "u") == 0)
    keycode = KEY_U;
  else if (strcmp(str, "v") == 0)
    keycode = KEY_V;
  else if (strcmp(str, "w") == 0)
    keycode = KEY_W;
  else if (strcmp(str, "x") == 0)
    keycode = KEY_X;
  else if (strcmp(str, "y") == 0)
    keycode = KEY_Y;
  else if (strcmp(str, "z") == 0)
    keycode = KEY_Z;

  else if (strcmp(str, "1") == 0)
    keycode = KEY_1;
  else if (strcmp(str, "2") == 0)
    keycode = KEY_2;
  else if (strcmp(str, "3") == 0)
    keycode = KEY_3;
  else if (strcmp(str, "4") == 0)
    keycode = KEY_4;
  else if (strcmp(str, "5") == 0)
    keycode = KEY_5;
  else if (strcmp(str, "6") == 0)
    keycode = KEY_6;
  else if (strcmp(str, "7") == 0)
    keycode = KEY_7;
  else if (strcmp(str, "8") == 0)
    keycode = KEY_8;
  else if (strcmp(str, "9") == 0)
    keycode = KEY_9;
  else if (strcmp(str, "0") == 0)
    keycode = KEY_0;

  else if (strcmp(str, "f1") == 0)
    keycode = KEY_F1;
  else if (strcmp(str, "f2") == 0)
    keycode = KEY_F2;
  else if (strcmp(str, "f3") == 0)
    keycode = KEY_F3;
  else if (strcmp(str, "f4") == 0)
    keycode = KEY_F4;
  else if (strcmp(str, "f5") == 0)
    keycode = KEY_F5;
  else if (strcmp(str, "f6") == 0)
    keycode = KEY_F6;
  else if (strcmp(str, "f7") == 0)
    keycode = KEY_F7;
  else if (strcmp(str, "f8") == 0)
    keycode = KEY_F8;
  else if (strcmp(str, "f9") == 0)
    keycode = KEY_F9;
  else if (strcmp(str, "f10") == 0)
    keycode = KEY_F10;

  else if (strcmp(str, "@") == 0)
    keycode = KEY_2; // with SHIFT
  else if (strcmp(str, "#") == 0)
    keycode = KEY_3; // with SHIFT
  //else if (strcmp(str, "€") == 0) keycode = KEY_5; // with ALTGR; not ASCII
  else if (strcmp(str, "%") == 0)
    keycode = KEY_5; // with SHIFT
  else if (strcmp(str, "&") == 0)
    keycode = KEY_7; // with SHIFT
  else if (strcmp(str, "*") == 0)
    keycode = KEY_8; // with SHIFT; alternative is KEY_KPASTERISK
  else if (strcmp(str, "-") == 0)
    keycode = KEY_MINUS; // alternative is KEY_KPMINUS
  else if (strcmp(str, "+") == 0)
    keycode = KEY_EQUAL; // with SHIFT; alternative is KEY_KPPLUS
  else if (strcmp(str, "(") == 0)
    keycode = KEY_9; // with SHIFT
  else if (strcmp(str, ")") == 0)
    keycode = KEY_0; // with SHIFT

  else if (strcmp(str, "!") == 0)
    keycode = KEY_1; // with SHIFT
  else if (strcmp(str, "\"") == 0)
    keycode = KEY_APOSTROPHE; // with SHIFT, dead key
  else if (strcmp(str, "\'") == 0)
    keycode = KEY_APOSTROPHE; // dead key
  else if (strcmp(str, ":") == 0)
    keycode = KEY_SEMICOLON; // with SHIFT
  else if (strcmp(str, ";") == 0)
    keycode = KEY_SEMICOLON;
  else if (strcmp(str, "/") == 0)
    keycode = KEY_SLASH;
  else if (strcmp(str, "?") == 0)
    keycode = KEY_SLASH; // with SHIFT
  else if (strcmp(str, ".") == 0)
    keycode = KEY_DOT;
  else if (strcmp(str, ",") == 0)
    keycode = KEY_COMMA;

  // special chars
  else if (strcmp(str, "~") == 0)
    keycode = KEY_GRAVE; // with SHIFT, dead key
  else if (strcmp(str, "`") == 0)
    keycode = KEY_GRAVE; // dead key
  else if (strcmp(str, "|") == 0)
    keycode = KEY_BACKSLASH; // with SHIFT
  else if (strcmp(str, "{") == 0)
    keycode = KEY_LEFTBRACE; // with SHIFT
  else if (strcmp(str, "}") == 0)
    keycode = KEY_RIGHTBRACE; // with SHIFT
  else if (strcmp(str, "$") == 0)
    keycode = KEY_4; // with SHIFT
  else if (strcmp(str, "^") == 0)
    keycode = KEY_6; // with SHIFT, dead key
  else if (strcmp(str, "_") == 0)
    keycode = KEY_MINUS; // with SHIFT
  else if (strcmp(str, "=") == 0)
    keycode = KEY_EQUAL;
  else if (strcmp(str, "[") == 0)
    keycode = KEY_LEFTBRACE;
  else if (strcmp(str, "]") == 0)
    keycode = KEY_RIGHTBRACE;
  else if (strcmp(str, "\\") == 0)
    keycode = KEY_BACKSLASH;
  else if (strcmp(str, "<") == 0)
    keycode = KEY_COMMA; // with SHIFT
  else if (strcmp(str, ">") == 0)
    keycode = KEY_DOT; // with SHIFT

  return keycode;
}

void initialiseCharacters()
{
  if (textinputinteractive_noautocapitals) {
    current_key[0] = 26; // if environment variable has been set to disable capitalisation of first characters start with all lower case  
  } else {
    current_key[0] = 0; // otherwise start with upper case for 1st character
  }
  for (int ii = 1; ii < maxChars; ii++) { // start with lower case for other character onwards
    current_key[ii] = 26;
  }

}

void initialiseCharacterSet()
{
  character_set[0]=char_to_keycode("a"); //capital letters
  character_set_shift[0]=true;
  character_set[1]=char_to_keycode("b");
  character_set_shift[1]=true;
  character_set[2]=char_to_keycode("c");
  character_set_shift[2]=true;
  character_set[3]=char_to_keycode("d");
  character_set_shift[3]=true;
  character_set[4]=char_to_keycode("e");
  character_set_shift[4]=true;
  character_set[5]=char_to_keycode("f");
  character_set_shift[5]=true;
  character_set[6]=char_to_keycode("g");
  character_set_shift[6]=true;
  character_set[7]=char_to_keycode("h");
  character_set_shift[7]=true;
  character_set[8]=char_to_keycode("i");
  character_set_shift[8]=true;
  character_set[9]=char_to_keycode("j");
  character_set_shift[9]=true;
  character_set[10]=char_to_keycode("k");
  character_set_shift[10]=true;
  character_set[11]=char_to_keycode("l");
  character_set_shift[11]=true;
  character_set[12]=char_to_keycode("m");
  character_set_shift[12]=true;
  character_set[13]=char_to_keycode("n");
  character_set_shift[13]=true;
  character_set[14]=char_to_keycode("o");
  character_set_shift[14]=true;
  character_set[15]=char_to_keycode("p");
  character_set_shift[15]=true;
  character_set[16]=char_to_keycode("q");
  character_set_shift[16]=true;
  character_set[17]=char_to_keycode("r");
  character_set_shift[17]=true;
  character_set[18]=char_to_keycode("s");
  character_set_shift[18]=true;
  character_set[19]=char_to_keycode("t");
  character_set_shift[19]=true;
  character_set[20]=char_to_keycode("u");
  character_set_shift[20]=true;
  character_set[21]=char_to_keycode("v");
  character_set_shift[21]=true;
  character_set[22]=char_to_keycode("w");
  character_set_shift[22]=true;
  character_set[23]=char_to_keycode("x");
  character_set_shift[23]=true;
  character_set[24]=char_to_keycode("y");
  character_set_shift[24]=true;
  character_set[25]=char_to_keycode("z");
  character_set_shift[25]=true;
  character_set[26]=char_to_keycode("a"); //lower case
  character_set_shift[26]=false;
  character_set[27]=char_to_keycode("b");
  character_set_shift[27]=false;
  character_set[28]=char_to_keycode("c");
  character_set_shift[28]=false;
  character_set[29]=char_to_keycode("d");
  character_set_shift[29]=false;
  character_set[30]=char_to_keycode("e");
  character_set_shift[30]=false;  
  character_set[31]=char_to_keycode("f");
  character_set_shift[31]=false;
  character_set[32]=char_to_keycode("g");
  character_set_shift[32]=false;
  character_set[33]=char_to_keycode("h");
  character_set_shift[33]=false;
  character_set[34]=char_to_keycode("i");
  character_set_shift[34]=false;
  character_set[35]=char_to_keycode("j");
  character_set_shift[35]=false;
  character_set[36]=char_to_keycode("k");
  character_set_shift[36]=false;
  character_set[37]=char_to_keycode("l");
  character_set_shift[37]=false;
  character_set[38]=char_to_keycode("m");
  character_set_shift[38]=false;
  character_set[39]=char_to_keycode("n");
  character_set_shift[39]=false;
  character_set[40]=char_to_keycode("o");
  character_set_shift[40]=false;
  character_set[41]=char_to_keycode("p");
  character_set_shift[41]=false;
  character_set[42]=char_to_keycode("q");
  character_set_shift[42]=false;
  character_set[43]=char_to_keycode("r");
  character_set_shift[43]=false;
  character_set[44]=char_to_keycode("s");
  character_set_shift[44]=false;
  character_set[45]=char_to_keycode("t");
  character_set_shift[45]=false;
  character_set[46]=char_to_keycode("u");
  character_set_shift[46]=false;
  character_set[47]=char_to_keycode("v");
  character_set_shift[47]=false;
  character_set[48]=char_to_keycode("w");
  character_set_shift[48]=false;
  character_set[49]=char_to_keycode("x");
  character_set_shift[49]=false;
  character_set[50]=char_to_keycode("y");
  character_set_shift[50]=false;
  character_set[51]=char_to_keycode("z");
  character_set_shift[51]=false;
  character_set[52]=char_to_keycode("0");
  character_set_shift[52]=false;
  character_set[53]=char_to_keycode("1");
  character_set_shift[53]=false;
  character_set[54]=char_to_keycode("2");
  character_set_shift[54]=false;
  character_set[55]=char_to_keycode("3");
  character_set_shift[55]=false;
  character_set[56]=char_to_keycode("4");
  character_set_shift[56]=false;
  character_set[57]=char_to_keycode("5");
  character_set_shift[57]=false;
  character_set[58]=char_to_keycode("6");
  character_set_shift[58]=false;
  character_set[59]=char_to_keycode("7"); 
  character_set_shift[59]=false;
  character_set[60]=char_to_keycode("8"); 
  character_set_shift[60]=false;
  character_set[61]=char_to_keycode("9"); 
  character_set_shift[61]=false;
  character_set[62]=char_to_keycode("space"); 
  character_set_shift[62]=false;
  character_set[63]=char_to_keycode("."); 
  character_set_shift[63]=false;
  character_set[64]=char_to_keycode(","); 
  character_set_shift[64]=false;
  character_set[65]=char_to_keycode("-"); 
  character_set_shift[65]=false;
  character_set[66]=char_to_keycode("_"); 
  character_set_shift[66]=true;
  character_set[67]=char_to_keycode("("); 
  character_set_shift[67]=true;
  character_set[68]=char_to_keycode(")");  
  character_set_shift[68]=true;

  if (textinputinteractive_extrasymbols) {
    maxKeys = maxKeysWithSymbols;
    character_set[69]=char_to_keycode("@");  
    character_set_shift[69]=true;
    character_set[70]=char_to_keycode("#");  
    character_set_shift[70]=true;
    character_set[71]=char_to_keycode("%");  
    character_set_shift[71]=true;
    character_set[72]=char_to_keycode("&");  
    character_set_shift[72]=true;
    character_set[73]=char_to_keycode("*");  
    character_set_shift[73]=true;
    character_set[74]=char_to_keycode("-");  
    character_set_shift[74]=false;
    character_set[75]=char_to_keycode("+");  
    character_set_shift[75]=true;
    character_set[76]=char_to_keycode("!");  
    character_set_shift[76]=true;
    character_set[77]=char_to_keycode("\"");  
    character_set_shift[77]=true;
    character_set[78]=char_to_keycode("\'");  
    character_set_shift[78]=false;
    character_set[79]=char_to_keycode(":");  
    character_set_shift[79]=true;
    character_set[80]=char_to_keycode(";");  
    character_set_shift[80]=false;
    character_set[81]=char_to_keycode("/");  
    character_set_shift[81]=false;
    character_set[82]=char_to_keycode("?");  
    character_set_shift[82]=true;
    character_set[83]=char_to_keycode("~");  
    character_set_shift[83]=true;
    character_set[84]=char_to_keycode("`");  
    character_set_shift[84]=false;
    character_set[85]=char_to_keycode("|");  
    character_set_shift[85]=true;
    character_set[86]=char_to_keycode("{");  
    character_set_shift[86]=true;
    character_set[87]=char_to_keycode("}");  
    character_set_shift[87]=true;
    character_set[88]=char_to_keycode("$");  
    character_set_shift[88]=true;
    character_set[89]=char_to_keycode("^");  
    character_set_shift[89]=true;
    character_set[90]=char_to_keycode("=");  
    character_set_shift[90]=false;
    character_set[91]=char_to_keycode("[");  
    character_set_shift[91]=false;
    character_set[92]=char_to_keycode("]");  
    character_set_shift[92]=false;
    character_set[93]=char_to_keycode("\\");  
    character_set_shift[93]=false;
    character_set[94]=char_to_keycode("<");  
    character_set_shift[94]=true;
    character_set[95]=char_to_keycode(">");  
    character_set_shift[95]=true;
  }
  initialiseCharacters();
}

void readConfigFile(const char* config_file)
{
  const auto parsedConfig = parseConfigFile(config_file);
  for (const auto& co : parsedConfig) {
    if (strcmp(co.key, "back") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.back_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.back_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.back_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.back_modifier = KEY_LEFTSHIFT;
        } else {
            config.back = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "guide") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.guide_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.guide_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.guide_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.guide_modifier = KEY_LEFTSHIFT;
        } else {
            config.guide = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "start") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.start_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.start_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.start_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.start_modifier = KEY_LEFTSHIFT;
        } else {
            config.start = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "a") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.a_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.a_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.a_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.a_modifier = KEY_LEFTSHIFT;
        } else {
            config.a = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "a_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.a_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.a_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.a_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.a_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "b") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.b_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.b_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.b_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.b_modifier = KEY_LEFTSHIFT;
        } else {
            config.b = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "b_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.b_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.b_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.b_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.b_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "x") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.x_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.x_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.x_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.x_modifier = KEY_LEFTSHIFT;
        } else {
            config.x = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "x_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.x_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.x_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.x_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.x_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "y") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.y_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.y_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.y_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.y_modifier = KEY_LEFTSHIFT;
        } else {
            config.y = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "y_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.y_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.y_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.y_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.y_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "l1") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.l1_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.l1_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.l1_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.l1_modifier = KEY_LEFTSHIFT;
        } else {
            config.l1 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "l1_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.l1_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.l1_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.l1_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.l1_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "l2") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.l2_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.l2_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.l2_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.l2_modifier = KEY_LEFTSHIFT;
        } else {
            config.l2 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "l2_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.l2_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.l2_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.l2_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.l2_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "l3") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.l3_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.l3_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.l3_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.l3_modifier = KEY_LEFTSHIFT;
        } else {
            config.l3 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "r1") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.r1_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.r1_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.r1_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.r1_modifier = KEY_LEFTSHIFT;
        } else {
            config.r1 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "r1_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.r1_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.r1_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.r1_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.r1_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "r2") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.r2_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.r2_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.r2_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.r2_modifier = KEY_LEFTSHIFT;
        } else {
            config.r2 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "r2_hk") == 0) {
        if (strcmp(co.value, "add_alt") == 0) {
            config.r2_hk_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.r2_hk_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.r2_hk_modifier = KEY_LEFTSHIFT;
        } else {
            config.r2_hk = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "r3") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.r3_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.r3_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.r3_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.r3_modifier = KEY_LEFTSHIFT;
        } else {
            config.r3 = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "up") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.up_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.up_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.up_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.up_modifier = KEY_LEFTSHIFT;
        } else {
            config.up = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "down") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.down_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.down_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.down_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.down_modifier = KEY_LEFTSHIFT;
        } else {
            config.down = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "left") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.left_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.left_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.left_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.left_modifier = KEY_LEFTSHIFT;
        } else {
            config.left = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "right") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.right_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.right_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.right_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.right_modifier = KEY_LEFTSHIFT;
        } else {
            config.right = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "left_analog_up") == 0) {
      if (strcmp(co.value, "mouse_movement_up") == 0) {
        config.left_analog_as_mouse = true;
      } else {
        if (strcmp(co.value, "repeat") == 0) {
            config.left_analog_up_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.left_analog_up_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.left_analog_up_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.left_analog_up_modifier = KEY_LEFTSHIFT;
        } else {
            config.left_analog_up = char_to_keycode(co.value);
        }
      }
    } else if (strcmp(co.key, "left_analog_down") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.left_analog_down_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.left_analog_down_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.left_analog_down_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.left_analog_down_modifier = KEY_LEFTSHIFT;
        } else {
            config.left_analog_down = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "left_analog_left") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.left_analog_left_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.left_analog_left_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.left_analog_left_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.left_analog_left_modifier = KEY_LEFTSHIFT;
        } else {
            config.left_analog_left = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "left_analog_right") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.left_analog_right_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.left_analog_right_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.left_analog_right_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.left_analog_right_modifier = KEY_LEFTSHIFT;
        } else {
            config.left_analog_right = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "right_analog_up") == 0) {
      if (strcmp(co.value, "mouse_movement_up") == 0) {
        config.right_analog_as_mouse = true;
      } else {
        if (strcmp(co.value, "repeat") == 0) {
            config.right_analog_up_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.right_analog_up_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.right_analog_up_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.right_analog_up_modifier = KEY_LEFTSHIFT;
        } else {
            config.right_analog_up = char_to_keycode(co.value);
        }
      }
    } else if (strcmp(co.key, "right_analog_down") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.right_analog_down_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.right_analog_down_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.right_analog_down_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.right_analog_down_modifier = KEY_LEFTSHIFT;
        } else {
            config.right_analog_down = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "right_analog_left") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.right_analog_left_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.right_analog_left_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.right_analog_left_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.right_analog_left_modifier = KEY_LEFTSHIFT;
        } else {
            config.right_analog_left = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "right_analog_right") == 0) {
        if (strcmp(co.value, "repeat") == 0) {
            config.right_analog_right_repeat = true;
        } else if (strcmp(co.value, "add_alt") == 0) {
            config.right_analog_right_modifier = KEY_LEFTALT;
        } else if (strcmp(co.value, "add_ctrl") == 0) {
            config.right_analog_right_modifier = KEY_LEFTCTRL;
        } else if (strcmp(co.value, "add_shift") == 0) {
            config.right_analog_right_modifier = KEY_LEFTSHIFT;
        } else {
            config.right_analog_right = char_to_keycode(co.value);
        }
    } else if (strcmp(co.key, "deadzone_y") == 0) {
      config.deadzone_y = atoi(co.value);
    } else if (strcmp(co.key, "deadzone_x") == 0) {
      config.deadzone_x = atoi(co.value);
    } else if (strcmp(co.key, "deadzone_triggers") == 0) {
      config.deadzone_triggers = atoi(co.value);
    } else if (strcmp(co.key, "mouse_scale") == 0) {
      config.fake_mouse_scale = atoi(co.value);
    } else if (strcmp(co.key, "mouse_delay") == 0) {
      config.fake_mouse_delay = atoi(co.value);
    } else if (strcmp(co.key, "repeat_delay") == 0) {
      config.key_repeat_delay = atoi(co.value);
    } else if (strcmp(co.key, "repeat_interval") == 0) {
      config.key_repeat_interval = atoi(co.value);
    } 
  }
}

int applyDeadzone(int value, int deadzone)
{
  if (std::abs(value) > deadzone) {
    return value;
  } else {
    return 0;
  }
}

void UINPUT_SET_ABS_P(
  uinput_user_dev* dev,
  int axis,
  int min,
  int max,
  int fuzz,
  int flat)
{
  dev->absmax[axis] = max;
  dev->absmin[axis] = min;
  dev->absfuzz[axis] = fuzz;
  dev->absflat[axis] = flat;
}

void emit(int type, int code, int val)
{
  struct input_event ev;

  ev.type = type;
  ev.code = code;
  ev.value = val;
  /* timestamp values below are ignored */
  ev.time.tv_sec = 0;
  ev.time.tv_usec = 0;

  write(uinp_fd, &ev, sizeof(ev));
}

void emitKey(int code, bool is_pressed, int modifier = 0)
{
  if (!(modifier == 0) && is_pressed) {
    emit(EV_KEY, modifier, is_pressed ? 1 : 0);
    emit(EV_SYN, SYN_REPORT, 0);
  }
  emit(EV_KEY, code, is_pressed ? 1 : 0);
  emit(EV_SYN, SYN_REPORT, 0);
  if (!(modifier == 0) && !(is_pressed)) {
    emit(EV_KEY, modifier, is_pressed ? 1 : 0);
    emit(EV_SYN, SYN_REPORT, 0);
  }
}

void emitTextInputKey(int code, bool uppercase)
{
  if (uppercase) { //capitalise capital letters by holding shift
    emitKey(KEY_LEFTSHIFT, true);
  }
  emitKey(code, true);
  SDL_Delay(16);
  emitKey(code, false);
  SDL_Delay(16);
  if (uppercase) { //release shift if held
    emitKey(KEY_LEFTSHIFT, false);
  }
}

void addTextInputCharacter()
{
  emitTextInputKey(character_set[current_key[current_character]],character_set_shift[current_key[current_character]]);
}

void removeTextInputCharacter()
{
  emitTextInputKey(KEY_BACKSPACE,false); //delete one character
}

void confirmTextInputCharacter()
{
  emitTextInputKey(KEY_ENTER,false); //emit ENTER to confirm text input
}

void nextTextInputKey(bool SingleIncrease) // enable fast skipping if SingleIncrease = false
{
  removeTextInputCharacter(); //delete character(s)
  if (SingleIncrease) {
    current_key[current_character]++;
  } else {
    current_key[current_character] = current_key[current_character] + 13; // jump forward by half alphabet
  }
  if (current_key[current_character] >= maxKeys) {
     current_key[current_character] = current_key[current_character] - maxKeys;
  } else if ((current_character == 0) && (character_set[current_key[current_character]] == KEY_SPACE)) {
      current_key[current_character]++; //skip space as first character 
  }

  addTextInputCharacter(); //add new character
}

void prevTextInputKey(bool SingleDecrease)
{
  removeTextInputCharacter(); //delete character(s)
  if (SingleDecrease) {
    current_key[current_character]--;
  } else {
    current_key[current_character] = current_key[current_character] - 13; // jump back by half alphabet  
  }
  if (current_key[current_character] < 0) {
     current_key[current_character] = current_key[current_character] + maxKeys;
  } else if ((current_character == 0) && (character_set[current_key[current_character]] == KEY_SPACE)) {
      current_key[current_character]--; //skip space as first character due to weird graphical issue with Exult
  }
  addTextInputCharacter(); //add new character
}

Uint32 repeatInputCallback(Uint32 interval, void *param)
{
    int key_code = *reinterpret_cast<int*>(param); 
    if (key_code == KEY_UP) {
      prevTextInputKey(true);
      interval = config.key_repeat_interval; // key repeats according to repeat interval
    } else if (key_code == KEY_DOWN) {
      nextTextInputKey(true);
      interval = config.key_repeat_interval; // key repeats according to repeat interval
    } else {
      interval = 0; //turn off timer if invalid keycode
    }
    return(interval);
}
void setInputRepeat(int code, bool is_pressed)
{
  if (is_pressed) {
    state.key_to_repeat = code;
    state.key_repeat_timer_id=SDL_AddTimer(config.key_repeat_interval, repeatInputCallback, &state.key_to_repeat); // for a new repeat, use repeat delay for first time, then switch to repeat interval
  } else {
    SDL_RemoveTimer( state.key_repeat_timer_id );
    state.key_repeat_timer_id=0;
    state.key_to_repeat=0;
  }
}


void processKeys()
{
  int lenText = strlen(config.text_input_preset);
  char str[2];
  char lowerstr[2];
  char upperstr[2];
  char lowerchar;
  char upperchar;
  bool uppercase = false;
  for (int ii = 0; ii < lenText; ii++) {  
    if (config.text_input_preset[ii] != '\0') {
        memcpy( str, &config.text_input_preset[ii], 1 );        
        str[1] = '\0';

        lowerchar = std::tolower(config.text_input_preset[ii], std::locale());
        upperchar = std::toupper(config.text_input_preset[ii], std::locale());

        memcpy( upperstr, &upperchar, 1 );        
        upperstr[1] = '\0';
        memcpy( lowerstr, &lowerchar, 1 );        
        lowerstr[1] = '\0';
        uppercase = (strcmp(upperstr,str) == 0);

        int code = char_to_keycode(lowerstr);

        if (strcmp(str, " ") == 0) {
            code = KEY_SPACE;
            uppercase = false;
        } else if (strcmp(str, "_") == 0) {
            code = KEY_MINUS;
            uppercase = true;
        } else if (strcmp(str, "-") == 0) {
            code = KEY_MINUS;
            uppercase = true;
        } else if (strcmp(str, ".") == 0) {
            code = KEY_DOT;
            uppercase = false;
        } else if (strcmp(str, ",") == 0) {
            code = KEY_COMMA;
            uppercase = false;
        }
        
        emitTextInputKey(code, uppercase);
    } // if valid character
  } //for
}

Uint32 repeatKeyCallback(Uint32 interval, void *param)
{
    //timerCallback requires pointer parameter, but passing pointer to key_code for analog sticks doesn't work
    int key_code = *reinterpret_cast<int*>(param); 
    emitKey(key_code, false);
    emitKey(key_code, true); 
    interval = config.key_repeat_interval; // key repeats according to repeat interval; initial interval is set to delay
    return(interval);
}
void setKeyRepeat(int code, bool is_pressed)
{
  if (is_pressed) {
    state.key_to_repeat=code;
    state.key_repeat_timer_id=SDL_AddTimer(config.key_repeat_delay, repeatKeyCallback, &state.key_to_repeat); // for a new repeat, use repeat delay for first time, then switch to repeat interval
  } else {
    SDL_RemoveTimer( state.key_repeat_timer_id );
    state.key_repeat_timer_id=0;
    state.key_to_repeat=0;
  }
}
void emitAxisMotion(int code, int value)
{
  emit(EV_ABS, code, value);
  emit(EV_SYN, SYN_REPORT, 0);
}

void emitMouseMotion(int x, int y)
{
  if (x != 0) {
    emit(EV_REL, REL_X, x);
  }
  if (y != 0) {
    emit(EV_REL, REL_Y, y);
  }

  if (x != 0 || y != 0) {
    emit(EV_SYN, SYN_REPORT, 0);
  }
}

void handleAnalogTrigger(bool is_triggered, bool& was_triggered, int key, int modifier=0)
{
  if (is_triggered && !was_triggered) {
    emitKey(key, true, modifier);
  } else if (!is_triggered && was_triggered) {
    emitKey(key, false, modifier);
  }

  was_triggered = is_triggered;
}

void setupFakeKeyboardMouseDevice(uinput_user_dev& device, int fd)
{
  strncpy(device.name, "Fake Keyboard", UINPUT_MAX_NAME_SIZE);
  device.id.vendor = 0x1234;  /* sample vendor */
  device.id.product = 0x5678; /* sample product */

  for (int i = 0; i < 256; i++) {
    ioctl(fd, UI_SET_KEYBIT, i);
  }

  // Keys or Buttons
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_SYN);

  // Fake mouse
  ioctl(fd, UI_SET_EVBIT, EV_REL);
  ioctl(fd, UI_SET_RELBIT, REL_X);
  ioctl(fd, UI_SET_RELBIT, REL_Y);
  ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
}

void setupFakeXbox360Device(uinput_user_dev& device, int fd)
{
  strncpy(device.name, "Microsoft X-Box 360 pad", UINPUT_MAX_NAME_SIZE);
  device.id.vendor = 0x045e;  /* sample vendor */
  device.id.product = 0x028e; /* sample product */

  if (
    ioctl(fd, UI_SET_EVBIT, EV_KEY) || ioctl(fd, UI_SET_EVBIT, EV_SYN) ||
    ioctl(fd, UI_SET_EVBIT, EV_ABS) ||
    // X-Box 360 pad buttons
    ioctl(fd, UI_SET_KEYBIT, BTN_A) || ioctl(fd, UI_SET_KEYBIT, BTN_B) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_X) || ioctl(fd, UI_SET_KEYBIT, BTN_Y) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_TL) || ioctl(fd, UI_SET_KEYBIT, BTN_TR) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBL) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBR) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_SELECT) ||
    ioctl(fd, UI_SET_KEYBIT, BTN_START) || ioctl(fd, UI_SET_KEYBIT, BTN_MODE) ||
    // absolute (sticks)
    ioctl(fd, UI_SET_ABSBIT, ABS_X) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_Y) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_RX) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_RY) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_Z) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_RZ) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X) ||
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y)) {
    printf("Failed to configure fake Xbox 360 controller\n");
    exit(-1);
  }

  UINPUT_SET_ABS_P(&device, ABS_X, -32768, 32767, 16, 128);
  UINPUT_SET_ABS_P(&device, ABS_Y, -32768, 32767, 16, 128);
  UINPUT_SET_ABS_P(&device, ABS_RX, -32768, 32767, 16, 128);
  UINPUT_SET_ABS_P(&device, ABS_RY, -32768, 32767, 16, 128);
  UINPUT_SET_ABS_P(&device, ABS_HAT0X, -1, 1, 0, 0);
  UINPUT_SET_ABS_P(&device, ABS_HAT0Y, -1, 1, 0, 0);
  UINPUT_SET_ABS_P(&device, ABS_Z, 0, 255, 0, 0);
  UINPUT_SET_ABS_P(&device, ABS_RZ, 0, 255, 0, 0);
}

bool handleEvent(const SDL_Event& event)
{
  switch (event.type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
      const bool is_pressed = event.type == SDL_CONTROLLERBUTTONDOWN;

        if (state.textinputinteractive_mode_active) {
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT: //move back one character
            if (is_pressed) {
              removeTextInputCharacter();
              if (current_character > 0) {
                current_character--;
              } else if (current_character == 0) {
                removeTextInputCharacter();
                initialiseCharacters();
                addTextInputCharacter();
              }
            }
            break; // SDL_CONTROLLER_BUTTON_DPAD_LEFT
            
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: //add one more character
            if (is_pressed) {
              if ((character_set[current_key[current_character]] == KEY_SPACE) && (!(textinputinteractive_noautocapitals))) {
                current_key[++current_character] = 0; // use capitals after a space
              } else {
                current_character++;
              }
              if (current_character < maxChars) {
                addTextInputCharacter();
              } else { // reached limit of characters
                confirmTextInputCharacter();
                state.textinputinteractive_mode_active = false;
                printf("text input interactive mode no longer active\n");
              }
            }
            break; //SDL_CONTROLLER_BUTTON_DPAD_RIGHT
            
          case SDL_CONTROLLER_BUTTON_DPAD_UP: //select previous key
            if (is_pressed) {
                prevTextInputKey(true);  
                setInputRepeat(KEY_UP, true);        
            } else {
                setInputRepeat(KEY_UP, false);
            }
            break; //SDL_CONTROLLER_BUTTON_DPAD_UP
            
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  //select next key
            if (is_pressed) {
                nextTextInputKey(true);
                setInputRepeat(KEY_DOWN, true);        
            } else {
                setInputRepeat(KEY_DOWN, false);
            }
            break; //SDL_CONTROLLER_BUTTON_DPAD_DOWN
            
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: //jump back by 13 letters
            if (is_pressed) {
                prevTextInputKey(false); //jump back by 13 letters
                setInputRepeat(KEY_UP, false); //disable key repeat  
            } else {
                setInputRepeat(KEY_UP, false);
            }
            break; //SDL_CONTROLLER_BUTTON_LEFTSHOULDER
            
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  //jump forward by 13 letters
            if (is_pressed) {
                nextTextInputKey(false); //jump forward by 13 letters
                setInputRepeat(KEY_DOWN, false); //disable key repeat        
            } else {
                setInputRepeat(KEY_DOWN, false);
            }
            break; //SDL_CONTROLLER_BUTTON_RIGHTSHOULDER

          case SDL_CONTROLLER_BUTTON_A: //A buttons sends ENTER KEY
            if (is_pressed) {
              confirmTextInputCharacter();
              //disable interactive mode
              state.textinputinteractive_mode_active = false;
              printf("text input interactive mode no longer active\n");
            }
            break; //SDL_CONTROLLER_BUTTON_A

          case SDL_CONTROLLER_BUTTON_LEFTSTICK: // hotkey override
          case SDL_CONTROLLER_BUTTON_BACK: // aka select
            if (is_pressed) { // cancel key input and disable interactive input mode
              for( int ii = 0; ii <= current_character; ii++ ) {
                removeTextInputCharacter(); // delete all characters
                if ((character_set[current_key[current_character]] == KEY_SPACE) && app_exult_adjust) {
                  removeTextInputCharacter(); //remove extra spaces            
                }
              }
              initialiseCharacters(); //reset the character selections ready for new text to be added later
              state.textinputinteractive_mode_active = false;
              printf("text input interactive mode no longer active\n");
            }
            break; //SDL_CONTROLLER_BUTTON_BACK
            
          case SDL_CONTROLLER_BUTTON_START:
            if (is_pressed) { 
              confirmTextInputCharacter(); // send ENTER key to confirm text entry
              //disable interactive mode
              state.textinputinteractive_mode_active = false;
              printf("text input interactive mode no longer active\n");
            }
            break; //SDL_CONTROLLER_BUTTON_START
            
          }   //switch (event.cbutton.button) for textinputinteractive_mode_active     
      } else if (xbox360_mode) {
        // Fake Xbox360 mode
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_A:
            emitKey(BTN_A, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_B:
            emitKey(BTN_B, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_X:
            emitKey(BTN_X, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_Y:
            emitKey(BTN_Y, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            emitKey(BTN_TL, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            emitKey(BTN_TR, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            emitKey(BTN_THUMBL, is_pressed);
            if (kill_mode && hotkey_override && (strcmp(hotkey_code, "l3") == 0)) {
                state.hotkey_jsdevice = event.cdevice.which;
                state.hotkey_pressed = is_pressed;
            }
            break;

          case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            emitKey(BTN_THUMBR, is_pressed);
            break;

          case SDL_CONTROLLER_BUTTON_BACK: // aka select
            emitKey(BTN_SELECT, is_pressed);
            if (!emuelec_override) {
            if ((kill_mode && !(hotkey_override)) || (kill_mode && hotkey_override && (strcmp(hotkey_code, "back") == 0))) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
           }
       }
            break;

          case SDL_CONTROLLER_BUTTON_GUIDE:
            emitKey(BTN_MODE, is_pressed);
            if ((kill_mode && !(hotkey_override)) || (kill_mode && hotkey_override && (strcmp(hotkey_code, "guide") == 0))) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
            }
            break;

          case SDL_CONTROLLER_BUTTON_START:
            emitKey(BTN_START, is_pressed);
            if ((kill_mode) || (textinputpreset_mode) || (textinputinteractive_mode)) {
              state.start_jsdevice = event.cdevice.which;
              state.start_pressed = is_pressed;
            }
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            emitAxisMotion(ABS_HAT0Y, is_pressed ? -1 : 0);
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            emitAxisMotion(ABS_HAT0Y, is_pressed ? 1 : 0);
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            emitAxisMotion(ABS_HAT0X, is_pressed ? -1 : 0);
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            emitAxisMotion(ABS_HAT0X, is_pressed ? 1 : 0);
            break;
        }
         if ((kill_mode) && (state.start_pressed && state.hotkey_pressed)) {      
          if (pckill_mode) {
            emitKey(KEY_F4,true,KEY_LEFTALT);
            SDL_Delay(15);
            emitKey(KEY_F4,false,KEY_LEFTALT);
          }
          if (! sudo_kill) {
             // printf("Killing: %s\n", AppToKill);
             if (state.start_jsdevice == state.hotkey_jsdevice) {
                system((" killall  '" + std::string(AppToKill) + "' ").c_str());
                system("show_splash.sh exit");
               sleep(3);
               if (
                 system((" pgrep '" + std::string(AppToKill) + "' ").c_str()) ==
                 0) {
                 printf("Forcefully Killing: %s\n", AppToKill);
                 system(
                   (" killall  -9 '" + std::string(AppToKill) + "' ").c_str());
               }
            exit(0); 
            }             
          } else {
             if (state.start_jsdevice == state.hotkey_jsdevice) {
                system((" kill -9 $(pidof '" + std::string(AppToKill) + "') ").c_str());
               sleep(3);
               exit(0);
             }
           } // sudo kill
        } //kill mode
      // xbox360 mode
      } else { //config mode (i.e. not textinputinteractive_mode_active)
        switch (event.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (textinputpreset_mode) { //check if input preset mode is triggered
                state.textinputpresettrigger_jsdevice = event.cdevice.which;
                state.textinputpresettrigger_pressed = is_pressed;
                if (state.start_pressed && state.textinputpresettrigger_pressed) break; //hotkey combo triggered
            }
            emitKey(config.left, is_pressed, config.left_modifier);
            if ((config.left_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.left))) {
                setKeyRepeat(config.left, is_pressed);
            }
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            emitKey(config.up, is_pressed, config.up_modifier); 
            if ((config.up_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.up))){
                setKeyRepeat(config.up, is_pressed);
            }
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (textinputpreset_mode) { //check if input preset enter_press is triggered
                state.textinputconfirmtrigger_jsdevice = event.cdevice.which;
                state.textinputconfirmtrigger_pressed = is_pressed;
                if (state.start_pressed && state.textinputconfirmtrigger_pressed) break; //hotkey combo triggered
            }
            emitKey(config.right, is_pressed, config.right_modifier);
            if ((config.right_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.right))){
                setKeyRepeat(config.right, is_pressed);
            }
            break;

          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (textinputinteractive_mode) {
                state.textinputinteractivetrigger_jsdevice = event.cdevice.which;
                state.textinputinteractivetrigger_pressed = is_pressed;
                if (state.start_pressed && state.textinputinteractivetrigger_pressed) break; //hotkey combo triggered
            }
            emitKey(config.down, is_pressed, config.down_modifier);
            if ((config.down_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.down))){
                setKeyRepeat(config.down, is_pressed);
            }
            break;

          case SDL_CONTROLLER_BUTTON_A:
            if (state.hotkey_pressed) {
              emitKey(config.a_hk, is_pressed, config.a_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.a_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.a_hk_was_pressed = false;
              }
            } else if (state.a_hk_was_pressed && !(is_pressed)) {
              emitKey(config.a_hk, is_pressed, config.a_hk_modifier);              
              state.a_hk_was_pressed = false;
            } else {
              emitKey(config.a, is_pressed, config.a_modifier);
              if ((config.a_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.a))){
                  setKeyRepeat(config.a, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_B:
            if (state.hotkey_pressed) {
              emitKey(config.b_hk, is_pressed, config.b_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.b_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.b_hk_was_pressed = false;
              }
            } else if (state.b_hk_was_pressed && !(is_pressed)) {
              emitKey(config.b_hk, is_pressed, config.b_hk_modifier);              
              state.b_hk_was_pressed = false;
            } else {
              emitKey(config.b, is_pressed, config.b_modifier);
              if ((config.b_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.b))){
                  setKeyRepeat(config.b, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_X:
            if (state.hotkey_pressed) {
              emitKey(config.x_hk, is_pressed, config.x_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.x_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.x_hk_was_pressed = false;
              }
            } else if (state.x_hk_was_pressed && !(is_pressed)) {
              emitKey(config.x_hk, is_pressed, config.x_hk_modifier);              
              state.x_hk_was_pressed = false;
            } else {
              emitKey(config.x, is_pressed, config.x_modifier);
              if ((config.x_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.x))){
                  setKeyRepeat(config.x, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_Y:
            if (state.hotkey_pressed) {
              emitKey(config.y_hk, is_pressed, config.y_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.y_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.y_hk_was_pressed = false;
              }
            } else if (state.y_hk_was_pressed && !(is_pressed)) {
              emitKey(config.y_hk, is_pressed, config.y_hk_modifier);              
              state.y_hk_was_pressed = false;
            } else {
              emitKey(config.y, is_pressed, config.y_modifier);
              if ((config.y_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.y))){
                  setKeyRepeat(config.y, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            if (state.hotkey_pressed) {
              emitKey(config.l1_hk, is_pressed, config.l1_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.l1_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.l1_hk_was_pressed = false;
              }
            } else if (state.l1_hk_was_pressed && !(is_pressed)) {
              emitKey(config.l1_hk, is_pressed, config.l1_hk_modifier);              
              state.l1_hk_was_pressed = false;
            } else {
              emitKey(config.l1, is_pressed, config.l1_modifier);
              if ((config.l1_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.l1))){
                  setKeyRepeat(config.l1, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            if (state.hotkey_pressed) {
              emitKey(config.r1_hk, is_pressed, config.r1_hk_modifier);
              if (is_pressed) { //keep track of combo button press so it can be released if hotkey is released before this button is released
                state.r1_hk_was_pressed = true;
                state.hotkey_combo_triggered = true;
              } else {
                state.r1_hk_was_pressed = false;
              }
            } else if (state.r1_hk_was_pressed && !(is_pressed)) {
              emitKey(config.r1_hk, is_pressed, config.r1_hk_modifier);              
              state.r1_hk_was_pressed = false;
            } else {
              emitKey(config.r1, is_pressed, config.r1_modifier);
              if ((config.r1_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.r1))){
                  setKeyRepeat(config.r1, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            if ((kill_mode && hotkey_override && (strcmp(hotkey_code, "l3") == 0)) || (textinputpreset_mode && hotkey_override && (strcmp(hotkey_code, "l3") == 0)) || (textinputinteractive_mode && hotkey_override && (strcmp(hotkey_code, "l3") == 0))) {
                state.hotkey_jsdevice = event.cdevice.which;
                state.hotkey_pressed = is_pressed;
            } else if (hotkey_override && (strcmp(hotkey_code, "l3") == 0)) {
                state.hotkey_jsdevice = event.cdevice.which;
                state.hotkey_pressed = is_pressed;            
            }
            if (state.hotkey_pressed && (state.hotkey_jsdevice == event.cdevice.which)) {
              state.hotkey_was_pressed = true; // if hotkey is pressed, note the details of hotkey press in case it is released without triggering a hotkey combo event, since its press will need to be processed
              
            } else if (state.hotkey_combo_triggered && !(is_pressed)) { 
              state.hotkey_combo_triggered = false; //hotkey combo was pressed; ignore hotkey button release
              state.hotkey_was_pressed = false; //reset hotkey
            } else if (state.hotkey_was_pressed && !(is_pressed)) { 
              state.hotkey_was_pressed = false;
              emitKey(config.l3, true, config.l3_modifier); //key pressed and now released without hotkey trigger so process key press then key release
              SDL_Delay(16);
              emitKey(config.l3, is_pressed, config.l3_modifier);            
              if ((config.l3_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.l3))){
                setKeyRepeat(config.l3, is_pressed);
                //note: hotkey cannot be assigned for key repeat; release key repeat for completeness
              }
            } //hotkey state check prior to emitting key, to avoid conflicts with emitkey and hotkey press        
              else {
              emitKey(config.l3, is_pressed, config.l3_modifier);            
              if ((config.l3_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.l3))){
                setKeyRepeat(config.l3, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            emitKey(config.r3, is_pressed, config.r3_modifier);
            if ((config.r3_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.r3))){
                setKeyRepeat(config.r3, is_pressed);
            }
            break;

          case SDL_CONTROLLER_BUTTON_GUIDE:
            if ((kill_mode && !(hotkey_override)) || (kill_mode && hotkey_override && (strcmp(hotkey_code, "guide") == 0)) || (textinputpreset_mode && !(hotkey_override)) || (textinputpreset_mode && (strcmp(hotkey_code, "guide") == 0)) || (textinputinteractive_mode && !(hotkey_override)) || (textinputinteractive_mode && (strcmp(hotkey_code, "guide") == 0))) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
            } else if (!(hotkey_override)) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
            }
            if (state.hotkey_pressed && (state.hotkey_jsdevice == event.cdevice.which)) {
              state.hotkey_was_pressed = true; // if hotkey is pressed, note the details of hotkey press in case it is released without triggering a hotkey combo event, since its press will need to be processed
              
            } else if (state.hotkey_combo_triggered && !(is_pressed)) { 
              state.hotkey_combo_triggered = false; //hotkey combo was pressed; ignore hotkey button release
              state.hotkey_was_pressed = false; //reset hotkey
              
            } else if (state.hotkey_was_pressed && !(is_pressed)) { 
              state.hotkey_was_pressed = false;
              emitKey(config.guide, true, config.guide_modifier); //key pressed and now released without hotkey trigger so process key press then key release
              SDL_Delay(16);
              emitKey(config.guide, is_pressed, config.guide_modifier);
              if ((config.guide_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.guide))){
                setKeyRepeat(config.guide, is_pressed);
                //note: hotkey cannot be assigned for key repeat; release key repeat for completeness
              }
            } //hotkey state check prior to emitting key, to avoid conflicts with emitkey and hotkey press        
              else {
              emitKey(config.guide, is_pressed, config.guide_modifier);
              if ((config.guide_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.guide))){
                setKeyRepeat(config.guide, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_BACK: // aka select
            if (!emuelec_override) {
            if ((kill_mode && !(hotkey_override)) || (kill_mode && hotkey_override && (strcmp(hotkey_code, "back") == 0))) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
            } else if (!(hotkey_override)) {
              state.hotkey_jsdevice = event.cdevice.which;
              state.hotkey_pressed = is_pressed;
            }
            }
            
            if (state.hotkey_pressed && (state.hotkey_jsdevice == event.cdevice.which)) {
              state.hotkey_was_pressed = true; // if hotkey is pressed, note the details of hotkey press in case it is released without triggering a hotkey combo event, since its press will need to be processed
              
            } else if (state.hotkey_combo_triggered && !(is_pressed)) { 
              state.hotkey_combo_triggered = false; //hotkey combo was pressed; ignore hotkey button release
              state.hotkey_was_pressed = false; //reset hotkey
              
            } else if (state.hotkey_was_pressed && !(is_pressed)) { 
              state.hotkey_was_pressed = false;
              emitKey(config.back, true, config.back_modifier); //key pressed and now released without hotkey trigger so process key press then key release
              SDL_Delay(16);
              emitKey(config.back, is_pressed, config.back_modifier);
              if ((config.back_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.back))){
                setKeyRepeat(config.back, is_pressed);
                //note: hotkey cannot be assigned for key repeat; release key repeat for completeness
              }
            } //hotkey state check prior to emitting key, to avoid conflicts with emitkey and hotkey press        
            else {
              emitKey(config.back, is_pressed, config.back_modifier);
              if ((config.back_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.back))){
                setKeyRepeat(config.back, is_pressed);
              }
            }
            break;

          case SDL_CONTROLLER_BUTTON_START:
            if ((kill_mode) || (textinputpreset_mode) || (textinputinteractive_mode)) {
                state.start_jsdevice = event.cdevice.which;
                state.start_pressed = is_pressed;
            } // start pressed - ready for text input modes if trigger is also pressed
            if (state.start_pressed && (state.start_jsdevice == event.cdevice.which)) { 
              state.start_was_pressed = true; // if start as hotkey is pressed, note the details of start key press in case it is released without triggering a hotkey event, since its press will need to be processed
              
            } else if (state.start_combo_triggered && !(is_pressed)) { 
              state.start_combo_triggered = false; //ignore start key release if it acted as hotkey
              state.start_was_pressed = false; //reset hotkey
              
            } else if (state.start_was_pressed && !(is_pressed)) { //key pressed and now released without start trigger so process original key press, pause, then process key release
              state.start_was_pressed = false;
              emitKey(config.start, true, config.start_modifier);
              SDL_Delay(16);
              emitKey(config.start, is_pressed, config.start_modifier);
              //note: start cannot be assigned for key repeat; release key repeat for completeness
              if ((config.start_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.start))){
                setKeyRepeat(config.start, is_pressed);
              }
            } else { //process start key as normal
              emitKey(config.start, is_pressed, config.start_modifier);
              if ((config.start_repeat && is_pressed && (state.key_to_repeat == 0)) || (!(is_pressed) && (state.key_to_repeat == config.start))){
                setKeyRepeat(config.start, is_pressed);
              }
            }
            break;
        } //switch
        if ((kill_mode) && (state.start_pressed && state.hotkey_pressed)) {
          if (pckill_mode) {
            emitKey(KEY_F4,true,KEY_LEFTALT);
            SDL_Delay(15);
            emitKey(KEY_F4,false,KEY_LEFTALT);
          }
          SDL_RemoveTimer( state.key_repeat_timer_id );
          if (! sudo_kill) {
             // printf("Killing: %s\n", AppToKill);
             if (state.start_jsdevice == state.hotkey_jsdevice) {
                system((" killall  '" + std::string(AppToKill) + "' ").c_str());
                system("show_splash.sh exit");
               sleep(3);
               if (
                 system((" pgrep '" + std::string(AppToKill) + "' ").c_str()) ==
                 0) {
                 printf("Forcefully Killing: %s\n", AppToKill);
                 system(
                   (" killall  -9 '" + std::string(AppToKill) + "' ").c_str());
               }
            exit(0); 
            }   
          } else {
             if (state.start_jsdevice == state.hotkey_jsdevice) {
                system((" kill -9 $(pidof '" + std::string(AppToKill) + "') ").c_str());
               sleep(3);
               exit(0);
             }
           } // sudo kill
        } //kill mode 
        else if ((textinputpreset_mode) && (state.textinputpresettrigger_pressed && state.start_pressed)) { //activate input preset mode - send predefined text as a series of keystrokes
            printf("text input preset pressed\n");
            state.start_combo_triggered = true;
            if (state.start_jsdevice == state.textinputpresettrigger_jsdevice) {
                if (config.text_input_preset != NULL) {
                    printf("text input processing %s\n", config.text_input_preset);
                    processKeys();
                }
            }
            state.textinputpresettrigger_pressed = false; //reset textinputpreset trigger
            state.start_pressed = false;
            state.start_jsdevice = 0;
            state.textinputpresettrigger_jsdevice = 0;
         } //input preset trigger mode (i.e. not kill mode)
        else if ((textinputpreset_mode) && (state.textinputconfirmtrigger_pressed && state.start_pressed)) { //activate input preset confirm mode - send ENTER key
            printf("text input confirm pressed\n");
            state.start_combo_triggered = true;
            if (state.start_jsdevice == state.textinputconfirmtrigger_jsdevice) {
                printf("text input Enter key\n");
                emitKey(char_to_keycode("enter"), true);
                SDL_Delay(15);
                emitKey(char_to_keycode("enter"), false);
            }
            state.textinputconfirmtrigger_pressed = false; //reset textinputpreset confirm trigger
            state.start_pressed = false;
            state.start_jsdevice = 0;
            state.textinputconfirmtrigger_jsdevice = 0;
          } //input confirm trigger mode (i.e. not kill mode)         
        else if ((textinputinteractive_mode) && (state.textinputinteractivetrigger_pressed && state.start_pressed)) { //activate interactive text input mode
            printf("text input interactive pressed\n");
            state.start_combo_triggered = true;
            if (state.start_jsdevice == state.textinputinteractivetrigger_jsdevice) {
                printf("text input interactive mode active\n");
                state.textinputinteractive_mode_active = true;
                SDL_RemoveTimer( state.key_repeat_timer_id ); // disable any active key repeat timer
                current_character = 0;

                addTextInputCharacter();
            }
            state.textinputinteractivetrigger_pressed = false; //reset interactive text input mode trigger
            state.start_pressed = false;
            state.textinputinteractivetrigger_jsdevice = 0;
            state.start_jsdevice = 0;
          } //input interactive trigger mode (i.e. not kill mode)
      }  //xbox or config/default
    } break; // case SDL_CONTROLLERBUTTONUP: SDL_CONTROLLERBUTTONDOWN:

    case SDL_CONTROLLERAXISMOTION:
      if (xbox360_mode) {
        switch (event.caxis.axis) {
          case SDL_CONTROLLER_AXIS_LEFTX:
            emitAxisMotion(ABS_X, event.caxis.value);
            break; 

          case SDL_CONTROLLER_AXIS_LEFTY:
            emitAxisMotion(ABS_Y, event.caxis.value);
            break;

          case SDL_CONTROLLER_AXIS_RIGHTX:
            emitAxisMotion(ABS_RX, event.caxis.value);
            break;

          case SDL_CONTROLLER_AXIS_RIGHTY:
            emitAxisMotion(ABS_RY, event.caxis.value);
            break;

          case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            // The target range for the triggers is 0..255 instead of
            // 0..32767, so we shift down by 7 as that does exactly the
            // scaling we need (32767 >> 7 is 255)
            emitAxisMotion(ABS_Z, event.caxis.value >> 7);
            break;

          case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            emitAxisMotion(ABS_RZ, event.caxis.value >> 7);
            break;
        }
      } else {
        // indicate which axis was moved before checking whether it's assigned as mouse
        bool left_axis_movement = false;
        bool right_axis_movement = false;
        
        switch (event.caxis.axis) {
          case SDL_CONTROLLER_AXIS_LEFTX:
            state.current_left_analog_x =
              applyDeadzone(event.caxis.value, config.deadzone_x);
              left_axis_movement = true;
            break;

          case SDL_CONTROLLER_AXIS_LEFTY:
            state.current_left_analog_y =
              applyDeadzone(event.caxis.value, config.deadzone_y);
              left_axis_movement = true;
            break;

          case SDL_CONTROLLER_AXIS_RIGHTX:
            state.current_right_analog_x =
              applyDeadzone(event.caxis.value, config.deadzone_x);
              right_axis_movement = true;
            break;

          case SDL_CONTROLLER_AXIS_RIGHTY:
            state.current_right_analog_y =
              applyDeadzone(event.caxis.value, config.deadzone_y);
              right_axis_movement = true;
            break;

          case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            state.current_l2 = event.caxis.value;
            break;

          case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            state.current_r2 = event.caxis.value;
            break;
        } // switch (event.caxis.axis)

        // fake mouse
        if (config.left_analog_as_mouse && left_axis_movement) {
          state.mouseX = state.current_left_analog_x / config.fake_mouse_scale;
          state.mouseY = state.current_left_analog_y / config.fake_mouse_scale;
        } else if (config.right_analog_as_mouse && right_axis_movement) {
          state.mouseX = state.current_right_analog_x / config.fake_mouse_scale;
          state.mouseY = state.current_right_analog_y / config.fake_mouse_scale;
        } else {
          // Analogs trigger keys
          if (!(state.textinputinteractive_mode_active)) {
            handleAnalogTrigger(
              state.current_left_analog_y < 0,
              state.left_analog_was_up,
              config.left_analog_up,
              config.left_analog_up_modifier);
            if ((state.current_left_analog_y < 0 ) && config.left_analog_up_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.left_analog_up, true);
            } else if ((state.current_left_analog_y == 0 ) && config.left_analog_up_repeat && (state.key_to_repeat == config.left_analog_up)) {
                setKeyRepeat(config.left_analog_up, false);
            }

            handleAnalogTrigger(
              state.current_left_analog_y > 0,
              state.left_analog_was_down,
              config.left_analog_down,
              config.left_analog_down_modifier);
            if ((state.current_left_analog_y > 0 ) && config.left_analog_down_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.left_analog_down, true);
            } else if ((state.current_left_analog_y == 0 ) && config.left_analog_down_repeat && (state.key_to_repeat == config.left_analog_down)) {
                setKeyRepeat(config.left_analog_down, false);
            }

            handleAnalogTrigger(
              state.current_left_analog_x < 0,
              state.left_analog_was_left,
              config.left_analog_left,
              config.left_analog_left_modifier);
            if ((state.current_left_analog_x < 0 ) && config.left_analog_left_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.left_analog_left, true);
            } else if ((state.current_left_analog_x == 0 ) && config.left_analog_left_repeat && (state.key_to_repeat == config.left_analog_left)) {
                setKeyRepeat(config.left_analog_left, false);
            }

            handleAnalogTrigger(
              state.current_left_analog_x > 0,
              state.left_analog_was_right,
              config.left_analog_right,
              config.left_analog_right_modifier);
            if ((state.current_left_analog_x > 0 ) && config.left_analog_right_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.left_analog_right, true);
            } else if ((state.current_left_analog_x == 0 ) && config.left_analog_right_repeat && (state.key_to_repeat == config.left_analog_right)) {
                setKeyRepeat(config.left_analog_right, false);
            }

            handleAnalogTrigger(
              state.current_right_analog_y < 0,
              state.right_analog_was_up,
              config.right_analog_up,
              config.right_analog_up_modifier);
            if ((state.current_right_analog_y < 0 ) && config.right_analog_up_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.right_analog_up, true);
            } else if ((state.current_right_analog_y == 0 ) && config.right_analog_up_repeat && (state.key_to_repeat == config.right_analog_up)) {
                setKeyRepeat(config.right_analog_up, false);
            }

            handleAnalogTrigger(
              state.current_right_analog_y > 0,
              state.right_analog_was_down,
              config.right_analog_down,
              config.right_analog_down_modifier);
            if ((state.current_right_analog_y > 0 ) && config.right_analog_down_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.right_analog_down, true);
            } else if ((state.current_right_analog_y == 0 ) && config.right_analog_down_repeat && (state.key_to_repeat == config.right_analog_down)) {
                setKeyRepeat(config.right_analog_down, false);
            }

            handleAnalogTrigger(
              state.current_right_analog_x < 0,
              state.right_analog_was_left,
              config.right_analog_left,
              config.right_analog_left_modifier);
            if ((state.current_right_analog_x < 0 ) && config.right_analog_left_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.right_analog_left, true);
            } else if ((state.current_right_analog_x == 0 ) && config.right_analog_left_repeat && (state.key_to_repeat == config.right_analog_left)) {
                setKeyRepeat(config.right_analog_left, false);
            }

            handleAnalogTrigger(
              state.current_right_analog_x > 0,
              state.right_analog_was_right,
              config.right_analog_right,
              config.right_analog_right_modifier);
            if ((state.current_right_analog_x > 0 ) && config.right_analog_right_repeat && (state.key_to_repeat == 0)) {
                setKeyRepeat(config.right_analog_right, true);
            } else if ((state.current_right_analog_x == 0 ) && config.right_analog_right_repeat && (state.key_to_repeat == config.right_analog_right)) {
                setKeyRepeat(config.right_analog_right, false);
            }
          } //!(state.textinputinteractive_mode_active)
        } // Analogs trigger keys 

        if (state.hotkey_pressed) {
          handleAnalogTrigger(
            state.current_l2 > config.deadzone_triggers,
            state.l2_hk_was_pressed,
            config.l2_hk,
            config.l2_hk_modifier);
          handleAnalogTrigger(
            state.current_r2 > config.deadzone_triggers,
            state.r2_hk_was_pressed,
            config.r2_hk,
            config.r2_hk_modifier);
          if (state.l2_hk_was_pressed || state.r2_hk_was_pressed) state.hotkey_combo_triggered = true;
        } else if (state.l2_hk_was_pressed || state.r2_hk_was_pressed) {
          handleAnalogTrigger(
            state.current_l2 > config.deadzone_triggers,
            state.l2_hk_was_pressed,
            config.l2_hk,
            config.l2_hk_modifier);
          handleAnalogTrigger(
            state.current_r2 > config.deadzone_triggers,
            state.r2_hk_was_pressed,
            config.r2_hk,
            config.r2_hk_modifier);
        } else {
          handleAnalogTrigger(
            state.current_l2 > config.deadzone_triggers,
            state.l2_was_pressed,
            config.l2,
            config.l2_modifier);
          handleAnalogTrigger(
            state.current_r2 > config.deadzone_triggers,
            state.r2_was_pressed,
            config.r2,
            config.r2_modifier);
        }
      } // end of else for indicating which axis was moved before checking whether it's assigned as mouse
      break;
    case SDL_CONTROLLERDEVICEADDED:
      if (xbox360_mode == true || config_mode == true) {
        SDL_GameControllerOpen(0);
        /* SDL_GameController* controller = SDL_GameControllerOpen(0);
     if (controller) {
                      const char *name = SDL_GameControllerNameForIndex(0);
                          printf("Joystick %i has game controller name '%s'\n", 0, name);
                  }
  */
      } else {
        SDL_GameControllerOpen(event.cdevice.which);
      }
      break;

    case SDL_CONTROLLERDEVICEREMOVED:
      if (
        SDL_GameController* controller =
          SDL_GameControllerFromInstanceID(event.cdevice.which)) {
        SDL_GameControllerClose(controller);
      }
      break;

    case SDL_QUIT:
      return false;
      break;
  }

  return true;
}

int main(int argc, char* argv[])
{
  const char* config_file = nullptr;

  config_mode = true;
  config_file = "/emuelec/configs/gptokeyb/default.gptk";

  // Add hotkey environment variable if available
  if (char* env_hotkey = SDL_getenv("HOTKEY")) {
    hotkey_override = true;
    hotkey_code = env_hotkey;
  }
  // Run in EmuELEC mode
  if (SDL_getenv("EMUELEC")) {
    emuelec_override = true;
  }

  // Add textinput_preset environment variable if available
  if (char* env_textinput = SDL_getenv("TEXTINPUTPRESET")) {
    textinputpreset_mode = true;
    config.text_input_preset = env_textinput;
  }

  // Add textinput_interactive environment variable if available
  if (char* env_textinput_interactive = SDL_getenv("TEXTINPUTINTERACTIVE")) {
    if (strcmp(env_textinput_interactive,"Y") == 0) {
      textinputinteractive_mode = true;
      state.textinputinteractive_mode_active = false;
    }
  }

  // Add pc alt+f4 exit environment variable if available
  if (char* env_pckill_mode = SDL_getenv("PCKILLMODE")) {
    if (strcmp(env_pckill_mode,"Y") == 0) {
      pckill_mode = true;
    }
  }

  if (argc > 1) {
    config_mode = false;
    config_file = "";
  }

  for( int ii = 1; ii < argc; ii++ )
  {      
    if (strcmp(argv[ii], "xbox360") == 0) {
      xbox360_mode = true;
    } else if (strcmp(argv[ii], "textinput") == 0) {
      textinputinteractive_mode = true;
      state.textinputinteractive_mode_active = false;
    } else if (strcmp(argv[ii], "-c") == 0) {
      if (ii + 1 < argc) { 
        config_mode = true;
        config_file = argv[++ii];
      } else {
        config_mode = true;
        config_file = "/emuelec/configs/gptokeyb/default.gptk";
      }
    } else if (strcmp(argv[ii], "-hotkey") == 0) {
      if (ii + 1 < argc) {
        hotkey_override = true;
        hotkey_code = argv[++ii];
      }
    } else if ((strcmp(argv[ii], "1") == 0) || (strcmp(argv[ii], "-1") == 0) || (strcmp(argv[ii], "-k") == 0)) {
      if (ii + 1 < argc) { 
        kill_mode = true;
        AppToKill = argv[++ii];
      }
    } else if ((strcmp(argv[ii], "-sudokill") == 0)) {
      if (ii + 1 < argc) { 
        kill_mode = true;
        sudo_kill = true;
        AppToKill = argv[++ii];
        if (strcmp(AppToKill, "exult") == 0) { // special adjustment for Exult, which adds double spaces during text input
          app_exult_adjust = true;
        }
      }
      
    } 
  }

  // Add textinput_interactive mode, check for extra options via environment variable if available
  if (textinputinteractive_mode) {
    if (char* env_textinput_nocaps = SDL_getenv("TEXTINPUTNOAUTOCAPITALS")) { // don't automatically use capitals for first letter or after space
      if (strcmp(env_textinput_nocaps,"Y") == 0) {
        textinputinteractive_noautocapitals = true;
      }
    }
    if (char* env_textinput_extrasymbols = SDL_getenv("TEXTINPUTADDEXTRASYMBOLS")) { // extended characters set for interactive text input mode
      if (strcmp(env_textinput_extrasymbols,"Y") == 0) {
        textinputinteractive_extrasymbols = true;
      }
    }    
  }


  // Create fake input device (not needed in kill mode)
  //if (!kill_mode) {  
  if (config_mode || xbox360_mode || textinputinteractive_mode) { // initialise device, even in kill mode, now that kill mode will work with config & xbox modes
    uinp_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinp_fd < 0) {
      printf("Unable to open /dev/uinput\n");
      return -1;
    }

    // Intialize the uInput device to NULL
    memset(&uidev, 0, sizeof(uidev));
    uidev.id.version = 1;
    uidev.id.bustype = BUS_USB;

    if (xbox360_mode) {
      printf("Running in Fake Xbox 360 Mode\n");
      setupFakeXbox360Device(uidev, uinp_fd);
    } else {
      printf("Running in Fake Keyboard mode\n");
      setupFakeKeyboardMouseDevice(uidev, uinp_fd);

      // if we are in config mode, read the file
      if (config_mode) {
        printf("Using ConfigFile %s\n", config_file);
        readConfigFile(config_file);
      }
      // if we are in textinput mode, note the text preset
      if (textinputpreset_mode) {
        if (config.text_input_preset != NULL) {
            printf("text input preset is %s\n", config.text_input_preset);
        } else {
            printf("text input preset is not set\n");
            //textinputpreset_mode = false;   removed so that Enter key can be pressed
        }
      } 
    }
            // if we are in textinputinteractive mode, initialise the character set
    if (textinputinteractive_mode) {
        initialiseCharacterSet();
        printf("interactive text input mode available\n");
        if (textinputinteractive_noautocapitals) printf("interactive text input mode without auto-capitals\n");
        if (textinputinteractive_extrasymbols) printf("interactive text input mode includes extra symbols\n");
    
    }
    // Create input device into input sub-system
    write(uinp_fd, &uidev, sizeof(uidev));

    if (ioctl(uinp_fd, UI_DEV_CREATE)) {
      printf("Unable to create UINPUT device.");
      return -1;
    }
  }

  if (const char* db_file = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
    SDL_GameControllerAddMappingsFromFile(db_file);
  }

  // SDL initialization and main loop
  if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
    printf("SDL_Init() failed: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Event event;
  bool running = true;
  while (running) {
    if (state.mouseX != 0 || state.mouseY != 0) {
      while (running && SDL_PollEvent(&event)) {
        running = handleEvent(event);
      }

      emitMouseMotion(state.mouseX, state.mouseY);
      SDL_Delay(config.fake_mouse_delay);
    } else {
      if (!SDL_WaitEvent(&event)) {
        printf("SDL_WaitEvent() failed: %s\n", SDL_GetError());
        return -1;
      }

      running = handleEvent(event);
    }
  }
  SDL_RemoveTimer( state.key_repeat_timer_id );
  SDL_Quit();

  /*
    * Give userspace some time to read the events before we destroy the
    * device with UI_DEV_DESTROY.
    */
  sleep(1);

  /* Clean up */
  ioctl(uinp_fd, UI_DEV_DESTROY);
  close(uinp_fd);
  return 0;
}
