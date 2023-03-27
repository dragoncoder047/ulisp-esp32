/*
 User Extensions
*/
#include <Arduino.h>
#define extensions
#include "ulisp.hpp"

// Definitions
object* fn_now (object* args, object* env) {
    (void) env;
    static unsigned long Offset;
    unsigned long now = millis()/1000;
    int nargs = listlength(args);

    // Set time
    if (nargs == 3) {
        Offset = (unsigned long)((checkinteger(first(args))*60 + checkinteger(second(args)))*60
            + checkinteger(third(args)) - now);
    } else if (nargs > 0) error2(PSTR("wrong number of arguments"));

    // Return time
    unsigned long secs = Offset + now;
    object* seconds = number(secs%60);
    object* minutes = number((secs/60)%60);
    object* hours = number((secs/3600)%24);
    return cons(hours, cons(minutes, cons(seconds, nil)));
}

// Symbol names
const char stringnow[] PROGMEM = "now";

// Documentation strings
const char docnow[] PROGMEM = "(now [hh mm ss])\n"
"Sets the current time, or with no arguments returns the current time\n"
"as a list of three integers (hh mm ss).";

// Symbol lookup table
const tbl_entry_t ExtensionsTable[] PROGMEM = {
    { stringnow, fn_now, 0203, docnow },
};

