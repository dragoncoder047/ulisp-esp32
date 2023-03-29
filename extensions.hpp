/*
    User Extensions
*/
#include <Arduino.h>
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

const char stringnow[] PROGMEM = "now";
const char docnow[] PROGMEM = "(now [hh mm ss])\n"
"Sets the current time, or with no arguments returns the current time\n"
"as a list of three integers (hh mm ss).";

object* fn_gensym (object* args, object* env) {
    int counter = 0;
    char buffer[BUFFERSIZE];
    char prefix[BUFFERSIZE];
    if (args != NULL) {
        cstring(checkstring(first(args)), prefix, BUFFERSIZE);
    } else {
        strcpy(prefix, "$gensym");
    }
    object* result;
    do {
        snprintf(buffer, BUFFERSIZE, "%s%i", prefix, counter);
        result = buftosymbol(buffer);
        counter++;
    } while (boundp(result, env) || boundp(result, GlobalEnv));
    return result;
}

const char stringgensym[] PROGMEM = "gensym";
const char docgensym[] PROGMEM = "(gensym [prefix])\n"
"Returns a new symbol, optionally beginning with prefix (which must be a string).\n"
"The returned symbol is guaranteed to not conflict with any existing bound symbol.";

object* fn_intern (object* args, object* env) {
    char b[BUFFERSIZE];
    return buftosymbol(cstring(checkstring(first(args)), b, BUFFERSIZE));
}

const char stringintern[] PROGMEM = "intern";
const char docintern[] PROGMEM = "(intern string)\n"
"Creates a symbol, with the same name as the string.\n"
"Unlike gensym, the returned symbol is not modified from the string in any way,\n"
"and so it may be bound.";

object* fn_sizeof (object* args, object* env) {
    int count = 0;
    markobject(first(args));
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (marked(obj)) {
            unmark(obj);
            count++;
        }
    }
    return number(count);
}

const char stringsizeof[] PROGMEM = "sizeof";
const char docsizeof[] PROGMEM = "(sizeof obj)\n"
"Returns the number of Lisp cells the object occupies in memory.";

// Symbol lookup table
const tbl_entry_t ExtensionsTable[] PROGMEM = {
    { stringnow, fn_now, MINMAX(FUNCTIONS, 0, 3), docnow },
    { stringgensym, fn_gensym, MINMAX(FUNCTIONS, 0, 1), docgensym },
    { stringintern, fn_intern, MINMAX(FUNCTIONS, 1, 1), docintern },
    { stringsizeof, fn_sizeof, MINMAX(FUNCTIONS, 1, 1), docsizeof },
};
