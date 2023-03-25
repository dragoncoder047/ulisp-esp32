/* uLisp ESP Release 4.4 - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 21st March 2023

   Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

// Lisp Library
const char LispLibrary[] PROGMEM = "";

// Compile options

#define printfreespace
#define printgcs
#define sdcardsupport
// #define gfxsupport
// #define lisplibrary
// #define extensions

// Includes
#include "ulisp.h"

/*
  setup - entry point from the Arduino IDE
*/
void setup () {
  Serial.begin(9600);
  int start = millis();
  while ((millis() - start) < 5000) { if (Serial) break; }
  initworkspace();
  initenv();
  initsleep();
  initgfx();
  pfstring(PSTR("uLisp 4.4 "), pserial); pln(pserial);
}

/*
  loop - the Arduino IDE main execution loop
*/
void loop () {
  if (!setjmp(toplevel_handler)) {
    ; // noop
  }
  ulispreset();
  repl(NULL);
}
