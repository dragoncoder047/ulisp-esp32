/* uLisp ESP Release 4.4b - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 31st March 2023

   Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

#include <ESP32Servo.h>
#include <analogWrite.h>
#include <ESP32Tone.h>
#include <ESP32PWM.h>

// Compile options

#define printfreespace
#define printgcs
#define sdcardsupport
// #define gfxsupport
// #define lisplibrary
#define toneimplemented

// Includes
#include "ulisp.hpp"
#include "extensions.hpp"
#include "bignums.hpp"

const char foo[] PROGMEM =
"(defun load(filename)(with-sd-card(f filename)(loop(let((form(read f)))(unless form(return))(eval form)))))"
"(load \"main.lisp\")"
;
const size_t foolen = arraysize(foo);
size_t fooi = 0;
int getfoo() {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    if (fooi == foolen) return -1;
    char c = (char)pgm_read_byte(&foo[fooi]);
    fooi++;
    return c;
}

/*
    sdmain - Run main.lisp on startup
*/
void sdmain () {
    SD.begin();
    if (setjmp(toplevel_handler)) return;
    object* fooform;
    for(;;) {
        fooform = read(getfoo);
        if (fooform == NULL) return;
        push(fooform, GCStack);
        eval(fooform, NULL);
        popandfree(GCStack);
    }
}

/*
    setup - entry point from the Arduino IDE
*/
void setup () {
    Serial.begin(115200);
    int start = millis();
    while ((millis() - start) < 5000) { if (Serial) break; }
    ulispinit();
    addtable(ExtensionsTable);
    addtable(BignumsTable);
    Serial.println(F("\n\n\nuLisp 4.4b!"));
    sdmain();
}

/*
    loop - the Arduino IDE main execution loop
*/
void loop () {
    if (!setjmp(toplevel_handler)) {
        ; // noop
    }
    ulisperrcleanup();
    repl(NULL);
}
