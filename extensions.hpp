#include "esp32-hal-rgb-led.h"
/*
    User Extensions
*/
#include <Arduino.h>
#include "ulisp.hpp"

// Definitions
object* fn_now(object* args, object* env) {
    (void)env;
    static unsigned long Offset;
    unsigned long now = millis() / 1000;
    int nargs = listlength(args);

    // Set time
    if (nargs == 3) {
        Offset = (unsigned long)((checkinteger(first(args)) * 60 + checkinteger(second(args))) * 60
                                 + checkinteger(third(args)) - now);
    } else if (nargs > 0) error2(PSTR("wrong number of arguments"));

    // Return time
    unsigned long secs = Offset + now;
    object* seconds = number(secs % 60);
    object* minutes = number((secs / 60) % 60);
    object* hours = number((secs / 3600) % 24);
    return cons(hours, cons(minutes, cons(seconds, nil)));
}

const char stringnow[] = "now";
const char docnow[] = "(now [hh mm ss])\n"
                      "Sets the current time, or with no arguments returns the current time\n"
                      "as a list of three integers (hh mm ss).";

object* fn_gensym(object* args, object* env) {
    unsigned int counter = 0;
    char buffer[BUFFERSIZE + 10];
    char prefix[BUFFERSIZE];
    if (args != NULL) {
        cstring(checkstring(first(args)), prefix, sizeof(prefix));
    } else {
        strcpy(prefix, "$gensym");
    }
    object* result;
    do {
        snprintf(buffer, sizeof(buffer), "%s%u", prefix, counter);
        result = buftosymbol(buffer);
        counter++;
    } while (boundp(result, env));
    return result;
}

const char stringgensym[] = "gensym";
const char docgensym[] = "(gensym [prefix])\n"
                         "Returns a new symbol, optionally beginning with prefix (which must be a string).\n"
                         "The returned symbol is guaranteed to not conflict with any existing bound symbol.";

object* fn_intern(object* args, object* env) {
    char b[BUFFERSIZE];
    return buftosymbol(cstring(checkstring(first(args)), b, BUFFERSIZE));
}

const char stringintern[] = "intern";
const char docintern[] = "(intern string)\n"
                         "Creates a symbol, with the same name as the string.\n"
                         "Unlike gensym, the returned symbol is not modified from the string in any way,\n"
                         "and so it may be bound.";

object* fn_sizeof(object* args, object* env) {
    int count = 0;
    markobject(first(args));
    for (int i = 0; i < WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (marked(obj)) {
            unmark(obj);
            count++;
        }
    }
    return number(count);
}

const char stringsizeof[] = "sizeof";
const char docsizeof[] = "(sizeof obj)\n"
                         "Returns the number of Lisp cells the object occupies in memory.";

void destructure(object* structure, object* data, object** env) {
    if (structure == nil) return;
    if (symbolp(structure)) push(cons(structure, data), *env);
    else if (consp(structure)) {
        if (!consp(data)) error(canttakecar, data);
        destructure(car(structure), car(data), env);
        destructure(cdr(structure), cdr(data), env);
    } else error(invalidarg, structure);
}

object* sp_destructuring_bind(object* args, object* env) {
    object* structure = first(args);
    object* data_expr = second(args);
    protect(data_expr);
    object* data = eval(data_expr, env);
    unprotect();
    object* body = cddr(args);
    destructure(structure, data, &env);
    protect(body);
    object* result = progn_no_tc(body, env);
    unprotect();
    return result;
}

const char stringdestructuringbind[] = "destructuring-bind";
const char docdestructuringbind[] = "(destructuring-bind structure data [forms*])\n\n"
                                    "Recursively assigns the datums of `data` to the symbols named in `structure`,\n"
                                    "and then evaluates forms in that new environment.";

object* fn_neopixel(object* args, object* env) {
    (void)env;
    int r = 0, g = 0, b = 0;
    if (listlength(args) == 1) {
        int color = checkinteger(first(args));
        if (color > 0xFFFFFF || color < 0) error("color out of range", first(args));
        r = (color >> 16) & 255;
        g = (color >> 8) & 255;
        b = color & 255;
    } else if (listlength(args) == 3) {
        r = checkinteger(first(args));
        g = checkinteger(second(args));
        b = checkinteger(third(args));
        if (r > 255) error("red out of range", first(args));
        if (g > 255) error("green out of range", second(args));
        if (b > 255) error("blue out of range", third(args));
    } else error2("don't take 2 args");
    neopixelWrite(2, r, g, b);
    return nil;
}

const char stringneopixel[] = "neopixel";

// Symbol lookup table
const tbl_entry_t ExtensionsTable[] = {
    { stringnow, fn_now, MINMAX(FUNCTIONS, 0, 3), docnow },
    { stringgensym, fn_gensym, MINMAX(FUNCTIONS, 0, 1), docgensym },
    { stringintern, fn_intern, MINMAX(FUNCTIONS, 1, 1), docintern },
    { stringsizeof, fn_sizeof, MINMAX(FUNCTIONS, 1, 1), docsizeof },
    { stringdestructuringbind, sp_destructuring_bind, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), docdestructuringbind },
    { stringdestructuringbind, sp_destructuring_bind, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), docdestructuringbind },
    { stringneopixel, fn_neopixel, MINMAX(FUNCTIONS, 1, 3), NULL }
};
