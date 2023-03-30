/* uLisp ESP Release 4.4 - www.ulisp.com
     David Johnson-Davies - www.technoblogy.com - 21st March 2023

     Licensed under the MIT license: https://opensource.org/licenses/MIT
*/
// Compile options

#define printfreespace
#define printgcs
#define sdcardsupport
// #define gfxsupport
// #define lisplibrary

// Includes
#include "ulisp.hpp"
#include "extensions.hpp"

const char foo[] PROGMEM =
"(defun load (filename)"
  "(with-sd-card (f filename)"
    "(loop"
      "(let ((form (read f)))"
        "(unless form (return))"
        "(eval form)))))"
"(load \"main.lisp\")"
;
const size_t foolen = arraylength(foo);
     
/*
    sdmain - Run main.lisp on startup
*/
void sdmain () {
    size_t i = 0;
    auto fooread = [i=]() -> int {
        if (i == foolen) return -1;
        char c = (char)pgm_read_byte(&foo[i]);
        i++;
        return c;
    };
    if (setjmp(toplevel_handler)) return;
    object* fooform;
    for(;;) {
        fooform = read(fooread);
        if (fooform == NULL) return;
        push(fooform, GCstack);
        eval(fooform, NULL);
        pop(GCstack);
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
    Serial.println(F("uLisp 4.4!"));
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
