/* uLisp ESP Release 4.6 - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 13th June 2024

   Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

// Compile options

#define printfreespace
// #define printgcs
#define sdcardsupport
// #define gfxsupport
// #define lisplibrary

// Includes
#include "ulisp.hpp"
#include "extensions.hpp"
#include "bignums.hpp"

const char foo[] =
  "(pinmode 13 :output)(dotimes(_ 4)(digitalwrite 13 :high)(delay 75)(digitalwrite 13 :low)(delay 75))"
  "(defvar *loaded* nil)"
  "(defun load(filename)(if(null(search(list filename)*loaded*))(with-sd-card(f filename)(push filename *loaded*)(loop(let((form(read f)))(unless form(return))(eval form))))))"
  "(if(eq'nothing(ignore-errors(load\"main.lisp\")'a))"
  "(progn(princ\"Error trying to run main.lisp\")(neopixel#xff0000)))"
  "(progn(princ\"main.lisp returned, entering REPL...\")(neopixel#x0000ff))";
const size_t foolen = arraysize(foo);
size_t fooi = 0;
int getfoo() {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    if (fooi == foolen) return -1;
    char c = foo[fooi];
    fooi++;
    return c;
}

/*
    sdmain - Run main.lisp on startup
*/
void sdmain() {
    SD.begin();
    if (setjmp(toplevel_handler)) return;
    object* fooform;
    for (;;) {
        fooform = read(getfoo);
        if (fooform == NULL) return;
        protect(fooform);
        eval(fooform, NULL);
        unprotect();
    }
}

/*
    setup - entry point from the Arduino IDE
*/
void setup() {
    Serial.begin(115200);
    int start = millis();
    while ((millis() - start) < 5000) {
        if (Serial) break;
    }
    ulispinit();
    addtable(ExtensionsTable);
    addtable(BignumsTable);
    Serial.println(F("\n\n\nuLisp 4.6-mod!"));
    sdmain();
}

/*
    loop - the Arduino IDE main execution loop
*/
void loop() {
    if (!setjmp(toplevel_handler)) {
        ;  // noop
    }
    ulisperrcleanup();
    repl(NULL);
}
