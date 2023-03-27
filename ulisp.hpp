/* uLisp ESP Release 4.4 - www.ulisp.com
     David Johnson-Davies - www.technoblogy.com - 21st March 2023

     Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

#ifndef ULISP_HPP
#define ULISP_HPP

// Includes

// #include "LispLibrary.h"
#include <Arduino.h>
#include <setjmp.h>
#include <limits.h>
#include <stdlib.h>
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <WiFi.h>

// Lisp Library
#ifndef LispLibrary
const char LispLibrary[] PROGMEM = "";
#endif

// Compile options

#define printfreespace
#define printgcs
#define sdcardsupport
// #define gfxsupport
// #define lisplibrary

#if defined(gfxsupport)
#define COLOR_WHITE ST77XX_WHITE
#define COLOR_BLACK ST77XX_BLACK
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#if defined(ARDUINO_ESP32_DEV)
Adafruit_ST7789 tft = Adafruit_ST7789(5, 16, 19, 18);
#define TFT_BACKLITE 4
#else
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, MOSI, SCK, TFT_RST);
#endif
#endif

#include <SD.h>
#define SDSIZE 172

// Platform specific settings

#define WORDALIGNED __attribute__((aligned (4)))
#define BUFFERSIZE 260

#define WORKSPACESIZE (9216-SDSIZE)            /* Cells (8*bytes) */
#define LITTLEFS
#include "FS.h"
#include <LittleFS.h>

#ifndef analogWrite
#define analogWrite(x,y) dacWrite((x),(y))
#endif

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif


// C Macros

#define nil                NULL
#define car(x)             (((object*)(x))->car)
#define cdr(x)             (((object*)(x))->cdr)

#define first(x)           (car(x))
#define rest(x)            (cdr(x))
#define second(x)          (first(rest(x)))
#define cddr(x)            (cdr(cdr(x)))
#define third(x)           (first(cddr(x)))

#define push(x, y)         ((y) = cons((x), (y)))
#define pop(y)             ((y) = cdr(y))

#define integerp(x)        ((x) != NULL && (x)->type == NUMBER)
#define floatp(x)          ((x) != NULL && (x)->type == FLOAT)
#define symbolp(x)         ((x) != NULL && (x)->type == SYMBOL)
#define stringp(x)         ((x) != NULL && (x)->type == STRING)
#define characterp(x)      ((x) != NULL && (x)->type == CHARACTER)
#define arrayp(x)          ((x) != NULL && (x)->type == ARRAY)
#define streamp(x)         ((x) != NULL && (x)->type == STREAM)

#define mark(x)            (car(x) = (object*)(((uintptr_t)(car(x))) | MARKBIT))
#define unmark(x)          (car(x) = (object*)(((uintptr_t)(car(x))) & ~MARKBIT))
#define marked(x)          ((((uintptr_t)(car(x))) & MARKBIT) != 0)
#define MARKBIT            1

#define setflag(x)         (Flags |= 1<<(x))
#define clrflag(x)         (Flags &= ~(1<<(x)))
#define tstflag(x)         (Flags & 1<<(x))

#define issp(x)            (x == ' ' || x == '\n' || x == '\r' || x == '\t')
#define isbr(x)            (x == ')' || x == '(' || x == '"' || x == '#')
#define longsymbolp(x)     (((x)->name & 0x03) == 0)
#define twist(x)           ((uint32_t)((x)<<2) | (((x) & 0xC0000000)>>30))
#define untwist(x)         (((x)>>2 & 0x3FFFFFFF) | ((x) & 0x03)<<30)
#define arraysize(x)       (sizeof(x) / sizeof(x[0]))
#define stringifyX(x)      #x
#define stringify(x)       stringifyX(x)
#define PACKEDS            0x43238000
#define BUILTINS           0xF4240000
#define ENDFUNCTIONS       1536

#define fntype(x)          (((uint8_t)(x))>>6)
#define getminargs(x)      ((((uint8_t)(x))>>3)&7)
#define getmaxargs(x)      (((uint8_t)(x))&7)
#define unlimitedp(x)      (getmaxargs(x)==UNLIMITED)
#define UNLIMITED          7

// let's hope the compiler can do constant folding!!
#define MINMAX(fntype, min, max) (((fntype)<<6)|((min)<<3)|(max))

// Constants

#define TRACEMAX 3; // Number of traced functions
enum type { ZZERO=0, SYMBOL=2, CODE=4, NUMBER=6, STREAM=8, CHARACTER=10, FLOAT=12, ARRAY=14, STRING=16, PAIR=18 };  // ARRAY STRING and PAIR must be last
enum token { UNUSED, BRA, KET, QUO, DOT };
enum fntypes_t { OTHER_FORMS, TAIL_FORMS, FUNCTIONS, SPECIAL_FORMS };

// Stream names used by printobject
const char serialstream[] PROGMEM = "serial";
const char i2cstream[] PROGMEM = "i2c";
const char spistream[] PROGMEM = "spi";
const char sdstream[] PROGMEM = "sd";
const char wifistream[] PROGMEM = "wifi";
const char stringstream[] PROGMEM = "string";
const char gfxstream[] PROGMEM = "gfx";
PGM_P const streamname[] PROGMEM = {serialstream, i2cstream, spistream, sdstream, wifistream, stringstream, gfxstream};
enum stream {                       SERIALSTREAM, I2CSTREAM, SPISTREAM, SDSTREAM, WIFISTREAM, STRINGSTREAM, GFXSTREAM};

// Typedefs

typedef uint32_t symbol_t;
typedef uint8_t minmax_t;
typedef uint16_t builtin_t;

typedef struct sobject {
    union {
        struct {
            sobject* car;
            sobject* cdr;
        };
        struct {
            unsigned int type;
            union {
                symbol_t name;
                int integer;
                int chars; // For strings
                float single_float;
            };
        };
    };
} object;

typedef object* (*fn_ptr_type)(object* , object*);
typedef void (*mapfun_t)(object* , object**);

typedef const struct {
    PGM_P string;
    fn_ptr_type fptr;
    minmax_t minmax;
    const char* doc;
} tbl_entry_t;

typedef struct {
    tbl_entry_t** table;
    size_t size;
} mtbl_entry_t;

typedef int (*gfun_t)();
typedef void (*pfun_t)(char);

enum builtins: builtin_t { NIL, TEE, NOTHING, OPTIONAL, INITIALELEMENT, ELEMENTTYPE, BIT, AMPREST, LAMBDA, LET, LETSTAR,
CLOSURE, PSTAR, QUOTE, DEFUN, DEFVAR, CAR, FIRST, CDR, REST, NTH, AREF, STRINGFN, PINMODE, DIGITALWRITE,
ANALOGREAD, REGISTER, FORMAT, 
 };

// Global variables

object Workspace[WORKSPACESIZE] WORDALIGNED;
mtbl_entry_t* Metatable;
size_t NumTables;

jmp_buf toplevel_handler;
jmp_buf *handler = &toplevel_handler;
size_t Freespace = 0;
object* Freelist;
builtin_t Context;

object* GlobalEnv;
object* GCStack = NULL;
object* GlobalString;
object* GlobalStringTail;
int GlobalStringIndex = 0;
uint8_t PrintCount = 0;
uint8_t BreakLevel = 0;
char LastChar = 0;
char LastPrint = 0;

unsigned int I2Ccount;
unsigned int TraceFn[TRACEMAX];
unsigned int TraceDepth[TRACEMAX];

// Flags
enum flag { PRINTREADABLY, RETURNFLAG, ESCAPE, EXITEDITOR, LIBRARYLOADED, NOESC, NOECHO, MUFFLEERRORS };
volatile uint8_t Flags = 0b00001; // PRINTREADABLY set by default

// Forward references
object* tee;
bool keywordp (object*);
void pfstring (PGM_P, pfun_t);
char nthchar (object*, int);
void pfl (pfun_t);
void pln (pfun_t);
void pserial (char);
int gserial ();
int glibrary ();
void pstr (char);
void psymbol (symbol_t, pfun_t);
void printobject (object*, pfun_t);
symbol_t sym (builtin_t);
void indent (uint8_t, char, pfun_t);
object* lispstring (char*);
uint32_t pack40 (char*);
char* cstring (object*, char*, int);
void pint (int, pfun_t);
void pintbase (uint32_t, uint8_t, pfun_t);
void printstring (object*, pfun_t);
int subwidthlist (object*, int);
minmax_t getminmax (builtin_t);
intptr_t lookupfn (builtin_t);
int listlength (object*);
void checkminmax (builtin_t, int);
object* findpair (object*, object*);
object* findvalue (object*, object*);
char* lookupdoc (builtin_t);
void printsymbol (object*, pfun_t);
void psymbol (symbol_t, pfun_t);
size_t tablesize (int);
bool findsubstring (char*, builtin_t);
bool stringcompare (object*, bool, bool, bool);
void pbuiltin (builtin_t, pfun_t);
object* value (symbol_t, object*);
void supersub (object*, int, int, pfun_t);
object* tf_progn (object*, object*);
object* fn_princtostring (object*, object*);
object* read (gfun_t);
object* eval (object*, object*);
void repl (object*);
void prin1object (object*, pfun_t);
void plispstr (symbol_t, pfun_t);
void printstring (object*, pfun_t);
void psymbol (symbol_t, pfun_t);
void testescape ();


// Error handling

/*
    errorsub - used by all the error routines.
    Prints: "Error: 'fname' string", where fname is the name of the Lisp function in which the error occurred.
*/
void errorsub (symbol_t fname, PGM_P string) {
    pfl(pserial); pfstring(PSTR("Error: "), pserial);
    if (fname != sym(NIL)) {
        pserial('\'');
        psymbol(fname, pserial);
        pserial('\''); pserial(' ');
    }
    pfstring(string, pserial);
}

void errorend () { GCStack = NULL; longjmp(*handler, 1); }

/*
    errorsym - prints an error message and reenters the REPL.
    Prints: "Error: 'fname' string: symbol", where fname is the name of the user Lisp function in which the error occurred,
    and symbol is the object generating the error.
*/
void errorsym (symbol_t fname, PGM_P string, object* symbol) {
    if (!tstflag(MUFFLEERRORS)) {
        errorsub(fname, string);
        pserial(':'); pserial(' ');
        printobject(symbol, pserial);
        pln(pserial);
    }
    errorend();
}

/*
    errorsym2 - prints an error message and reenters the REPL.
    Prints: "Error: 'fname' string", where fname is the name of the user Lisp function in which the error occurred.
*/
void errorsym2 (symbol_t fname, PGM_P string) {
    if (!tstflag(MUFFLEERRORS)) {
        errorsub(fname, string);
        pln(pserial);
    }
    errorend();
}

/*
    error - prints an error message and reenters the REPL.
    Prints: "Error: 'Context' string: symbol", where Context is the name of the built-in Lisp function in which the error occurred,
    and symbol is the object generating the error.
*/
void error (PGM_P string, object* symbol) {
    errorsym(sym(Context), string, symbol);
}

/*
    error2 - prints an error message and reenters the REPL.
    Prints: "Error: 'Context' string", where Context is the name of the built-in Lisp function in which the error occurred.
*/
void error2 (PGM_P string) {
    errorsym2(sym(Context), string);
}

/*
    formaterr - displays a format error with a ^ pointing to the error
*/
void formaterr (object* formatstr, PGM_P string, uint8_t p) {
    pln(pserial); indent(4, ' ', pserial); printstring(formatstr, pserial); pln(pserial);
    indent(p+5, ' ', pserial); pserial('^');
    error2(string);
    pln(pserial);
    GCStack = NULL;
    longjmp(*handler, 1);
}

// Save space as these are used multiple times
const char notanumber[] PROGMEM = "argument is not a number";
const char notaninteger[] PROGMEM = "argument is not an integer";
const char notastring[] PROGMEM = "argument is not a string";
const char notalist[] PROGMEM = "argument is not a list";
const char notasymbol[] PROGMEM = "argument is not a symbol";
const char notproper[] PROGMEM = "argument is not a proper list";
const char toomanyargs[] PROGMEM = "too many arguments";
const char toofewargs[] PROGMEM = "too few arguments";
const char noargument[] PROGMEM = "missing argument";
const char nostream[] PROGMEM = "missing stream argument";
const char overflow[] PROGMEM = "arithmetic overflow";
const char divisionbyzero[] PROGMEM = "division by zero";
const char indexnegative[] PROGMEM = "index can't be negative";
const char invalidarg[] PROGMEM = "invalid argument";
const char invalidkey[] PROGMEM = "invalid keyword";
const char illegalclause[] PROGMEM = "illegal clause";
const char invalidpin[] PROGMEM = "invalid pin";
const char oddargs[] PROGMEM = "odd number of arguments";
const char indexrange[] PROGMEM = "index out of range";
const char canttakecar[] PROGMEM = "can't take car";
const char canttakecdr[] PROGMEM = "can't take cdr";
const char unknownstreamtype[] PROGMEM = "unknown stream type";

// Set up workspace

/*
    initworkspace - initialises the workspace into a linked list of free objects
*/
void initworkspace () {
    Freelist = NULL;
    for (int i=WORKSPACESIZE-1; i>=0; i--) {
        object* obj = &Workspace[i];
        car(obj) = NULL;
        cdr(obj) = Freelist;
        Freelist = obj;
        Freespace++;
    }
}

/*
    myalloc - returns the first object from the linked list of free objects
*/
object* myalloc () {
    if (Freespace == 0) error2(PSTR("no room"));
    object* temp = Freelist;
    Freelist = cdr(Freelist);
    Freespace--;
    return temp;
}

/*
    myfree - adds obj to the linked list of free objects.
    inline makes gc significantly faster
*/
inline void myfree (object* obj) {
    car(obj) = NULL;
    cdr(obj) = Freelist;
    Freelist = obj;
    Freespace++;
}

// Make each type of object

/*
    number - make an integer object with value n and return it
    or return the existing one with the same value
*/
object* number (int n) {
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (obj->type == NUMBER && obj->integer == n) return obj;
    }
    object* ptr = myalloc();
    ptr->type = NUMBER;
    ptr->integer = n;
    return ptr;
}

/*
    makefloat - make a floating point object with value f and return it
    or return the existing one with the same value
*/
object* makefloat (float f) {
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (obj->type == FLOAT && obj->single_float == f) return obj;
    }
    object* ptr = myalloc();
    ptr->type = FLOAT;
    ptr->single_float = f;
    return ptr;
}

/*
    character - make a character object with value c and return it
    or return the existing one with the same value
*/
object* character (char c) {
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (obj->type == CHARACTER && obj->chars == c) return obj;
    }
    object* ptr = myalloc();
    ptr->type = CHARACTER;
    ptr->chars = c;
    return ptr;
}

/*
    cons - make a cons with arg1 and arg2 return it
*/
object* cons (object* arg1, object* arg2) {
    object* ptr = myalloc();
    ptr->car = arg1;
    ptr->cdr = arg2;
    return ptr;
}

/*
    symbol - make a symbol object with value name and return it
    or returns the existing one with the same value
*/
object* symbol (symbol_t name) {
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (obj->type == SYMBOL && obj->name == name) return obj;
    }
    object* ptr = myalloc();
    ptr->type = SYMBOL;
    ptr->name = name;
    return ptr;
}

/*
    bsymbol - make a built-in symbol
*/
inline object* bsymbol (builtin_t name) {
    return symbol(twist(name+BUILTINS));
}

/*
    eqsymbols - compares the long string/symbol obj with the string in buffer.
*/
bool eqsymbols (object* obj, char* buffer) {
    object* arg = cdr(obj);
    int i = 0;
    while (!(arg == NULL && buffer[i] == 0)) {
        if (arg == NULL || buffer[i] == 0) return false;
        int test = buffer[i]<<24;
        i++;
        if (buffer[i] != 0) {
            test |= buffer[i]<<16;
            i++;
            if (buffer[i] != 0) {
                test |= buffer[i]<<8;
                i++;
                if (buffer[i] != 0) {
                    test |= buffer[i];
                    i++;
                }
            }
        }
        if (arg->chars != test) return false;
        arg = car(arg);
    }
    return true;
}

/*
    internlong - looks through the workspace for an existing occurrence of the long symbol in buffer and returns it,
    otherwise calls lispstring(buffer) to create a new symbol.
*/
object* internlong (char* buffer) {
    for (int i=0; i<WORKSPACESIZE; i++) {
        object* obj = &Workspace[i];
        if (obj->type == SYMBOL && longsymbolp(obj) && eqsymbols(obj, buffer)) return obj;
    }
    object* obj = lispstring(buffer);
    obj->type = SYMBOL;
    return obj;
}

/*
    buftosymbol - checks the characters in buffer and calls intern() or internlong() to make it a symbol.
*/
object* buftosymbol (char* b) {
    int l = strlen(b);
    if (i <= 6 && valid40(b)) return symbol(twist(pack40(b)));
    else return internlong(b);
}

/*
    stream - makes a stream object defined by streamtype and address, and returns it
*/
object* stream (uint8_t streamtype, uint8_t address) {
    object* ptr = myalloc();
    ptr->type = STREAM;
    ptr->integer = streamtype<<8 | address;
    return ptr;
}

/*
    newstring - makes an empty string object and returns it
*/
object* newstring () {
    object* ptr = myalloc();
    ptr->type = STRING;
    ptr->chars = 0;
    return ptr;
}

// Garbage collection

/*
    markobject - recursively marks reachable objects, starting from obj
*/
void markobject (object* obj) {
    MARK:
    if (obj == NULL) return;
    if (marked(obj)) return;

    object* arg = car(obj);
    unsigned int type = obj->type;
    mark(obj);

    if (type >= PAIR || type == ZZERO) { // cons
        markobject(arg);
        obj = cdr(obj);
        goto MARK;
    }

    if (type == ARRAY) {
        obj = cdr(obj);
        goto MARK;
    }

    if ((type == STRING) || (type == SYMBOL && longsymbolp(obj))) {
        obj = cdr(obj);
        while (obj != NULL) {
            arg = car(obj);
            mark(obj);
            obj = arg;
        }
    }
}

/*
    sweep - goes through the workspace freeing objects that have not been marked,
    and unmarks marked objects
*/
void sweep () {
    Freelist = NULL;
    Freespace = 0;
    for (int i=WORKSPACESIZE-1; i>=0; i--) {
        object* obj = &Workspace[i];
        if (!marked(obj)) myfree(obj); else unmark(obj);
    }
}

/*
    gc - performs garbage collection by calling markobject() on each of the pointers to objects in use,
    followed by sweep() to free unused objects.
*/
void gc (object* form, object* env) {
    #if defined(printgcs)
    int start = Freespace;
    static int GC_Count = 0;
    #endif
    markobject(tee);
    markobject(GlobalEnv);
    markobject(GCStack);
    markobject(form);
    markobject(env);
    sweep();
    #if defined(printgcs)
    GC_Count++;
    pfl(pserial);
    pfstring(PSTR("{GC #"), pserial);
    pint(GC_Count, pserial);
    pfstring(PSTR(": "), pserial);
    pint(Freespace - start, pserial);
    pfstring(PSTR(" freed}"), pserial);
    #endif
}

char *MakeFilename (object *arg, char *buffer) {
    int max = BUFFERSIZE-1;
    buffer[0]='/';
    int i = 1;
    do {
        char c = nthchar(arg, i-1);
        if (c == '\0') break;
        buffer[i++] = c;
    } while (i<max);
    buffer[i] = '\0';
    return buffer;
}

// Tracing

/*
    tracing - returns a number between 1 and TRACEMAX if name is being traced, or 0 otherwise
*/
int tracing (symbol_t name) {
    int i = 0;
    while (i < TRACEMAX) {
        if (TraceFn[i] == name) return i+1;
        i++;
    }
    return 0;
}

/*
    trace - enables tracing of symbol name and adds it to the array TraceFn[].
*/
void trace (symbol_t name) {
    if (tracing(name)) error(PSTR("already being traced"), symbol(name));
    int i = 0;
    while (i < TRACEMAX) {
        if (TraceFn[i] == 0) { TraceFn[i] = name; TraceDepth[i] = 0; return; }
        i++;
    }
    error2(PSTR("already tracing " stringify(TRACEMAX) " functions"));
}

/*
    untrace - disables tracing of symbol name and removes it from the array TraceFn[].
*/
void untrace (symbol_t name) {
    int i = 0;
    while (i < TRACEMAX) {
        if (TraceFn[i] == name) { TraceFn[i] = 0; return; }
        i++;
    }
    error(PSTR("not tracing"), symbol(name));
}

// Helper functions

/*
    consp - implements Lisp consp
*/
bool consp (object* x) {
    if (x == NULL) return false;
    unsigned int type = x->type;
    return type >= PAIR || type == ZZERO;
}

/*
    atom - implements Lisp atom
*/
#define atom(x) (!consp(x))

/*
    listp - implements Lisp listp
*/
bool listp (object* x) {
    if (x == NULL) return true;
    unsigned int type = x->type;
    return type >= PAIR || type == ZZERO;
}

/*
    improperp - tests whether x is an improper list
*/
#define improperp(x) (!listp(x))

/*
    quoteit - quote a symbol with the specified type of quote
*/

object* quoteit (symbol_t q, object* it) {
    return cons(bsymbol(q), cons(it, nil));
}

// Radix 40 encoding

/*
    builtin - converts a symbol name to builtin
*/
builtin_t builtin (symbol_t name) {
    return (builtin_t)(untwist(name) - BUILTINS);
}

/*
    sym - converts a builtin to a symbol name
*/
symbol_t sym (builtin_t x) {
    return twist(x + BUILTINS);
}

const char radix40alphabet[] PROGMEM = "\0000123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-*$"

/*
    toradix40 - returns a number from 0 to 39 if the character can be encoded, or -1 otherwise.
*/
int8_t toradix40 (char ch) {
    ch = toupper(ch);
    for (int8_t i=0; i<40; i++) {
        if ((char)pgm_read_byte(&radix40alphabet[i]) == ch) return i;
    }
    return -1; // Invalid
}

/*
    fromradix40 - returns the character encoded by the number n.
*/
char fromradix40 (char n) {
    if (n < 0 || n >= 40) return 0;
    return (char)pgm_read_byte(radix40alphabet + n);
}

/*
    pack40 - packs six radix40-encoded characters from buffer into a 32-bit number and returns it.
*/
uint32_t pack40 (char* buffer) {
    int x = 0, gz = 0, c = 0;
    for (int i=0; i<6; i++) {
        if (gz) c = 0;
        else c = buffer[i]; // Don't dereference the buffer if we reached the end of the string already
        x *= 40;
        if (c == 0) gz = 1;
        else x += toradix40(c);
    }
    return x;
}

/*
    valid40 - returns true if the symbol in buffer can be encoded as six radix40-encoded characters.
*/
bool valid40 (char* buffer) {
    int t = 11;
    for (int i=0; i<6; i++){
        if (toradix40(buffer[i]) < t) return false;
        if (buffer[i+1] == 0) break;
        t = 0;
    }
    return true;
}

/*
    digitvalue - returns the numerical value of a hexadecimal digit, or 16 if invalid.
*/
int8_t digitvalue (char d) {
    if (d>='0' && d<='9') return d-'0';
    d = d | 0x20;
    if (d>='a' && d<='f') return d-'a'+10;
    return 16;
}

/*
    checkinteger - check that obj is an integer and return it
*/
int checkinteger (object* obj) {
    if (!integerp(obj)) error(notaninteger, obj);
    return obj->integer;
}

/*
    checkbitvalue - check that obj is an integer equal to 0 or 1 and return it
*/
int checkbitvalue (object* obj) {
    if (!integerp(obj)) error(notaninteger, obj);
    int n = obj->integer;
    if (n & ~1) error(PSTR("argument is not a bit value"), obj);
    return n;
}

/*
    checkintfloat - check that obj is an integer or floating-point number and return the number
*/
float checkintfloat (object* obj){
    if (integerp(obj)) return (float)obj->integer;
    if (!floatp(obj)) error(notanumber, obj);
    return obj->single_float;
}

/*
    checkchar - check that obj is a character and return the character
*/
int checkchar (object* obj) {
    if (!characterp(obj)) error(PSTR("argument is not a character"), obj);
    return obj->chars;
}

/*
    checkstring - check that obj is a string
*/
object* checkstring (object* obj) {
    if (!stringp(obj)) error(notastring, obj);
    return obj;
}

int isstream (object* obj){
    if (!streamp(obj)) error(PSTR("not a stream"), obj);
    return obj->integer;
}

int isbuiltin (object* obj, builtin_t n) {
    return symbolp(obj) && obj->name == sym(n);
}

bool builtinp (symbol_t name) {
    return (untwist(name) >= BUILTINS);
}

int checkkeyword (object* obj) {
    if (!keywordp(obj)) error(PSTR("argument is not a keyword"), obj);
    builtin_t kname = builtin(obj->name);
    minmax_t context = getminmax(kname);
    if (context != 0 && context != (minmax_t)Context) error(invalidkey, obj);
    return ((int)lookupfn(kname));
}

/*
    checkargs - checks that the number of objects in the list args
    is within the range specified in the symbol lookup table
*/
void checkargs (object* args) {
    int nargs = listlength(args);
    checkminmax(Context, nargs);
}

/*
    eq - implements Lisp eq
*/
boolean eq (object* arg1, object* arg2) {
    if (arg1 == arg2) return true;  // Same object
    if ((arg1 == nil) || (arg2 == nil)) return false;  // Not both values
    if (arg1->cdr != arg2->cdr) return false;  // Different values
    if (symbolp(arg1) && symbolp(arg2)) return true;  // Same symbol
    if (integerp(arg1) && integerp(arg2)) return true;  // Same integer
    if (floatp(arg1) && floatp(arg2)) return true; // Same float
    if (characterp(arg1) && characterp(arg2)) return true;  // Same character
    return false;
}

/*
    equal - implements Lisp equal
*/
boolean equal (object* arg1, object* arg2) {
    if (stringp(arg1) && stringp(arg2)) return stringcompare(cons(arg1, cons(arg2, nil)), false, false, true);
    if (consp(arg1) && consp(arg2)) return (equal(car(arg1), car(arg2)) && equal(cdr(arg1), cdr(arg2)));
    return eq(arg1, arg2);
}

/*
    listlength - returns the length of a list
*/
int listlength (object* list) {
    int length = 0;
    while (list != NULL) {
        if (improperp(list)) error2(notproper);
        list = cdr(list);
        length++;
    }
    return length;
}

// Mathematical helper functions

/*
    add_floats - used by fn_add
    Converts the numbers in args to floats, adds them to fresult, and returns the sum as a Lisp float.
*/
object* add_floats (object* args, float fresult) {
    while (args != NULL) {
        object* arg = car(args);
        fresult = fresult + checkintfloat(arg);
        args = cdr(args);
    }
    return makefloat(fresult);
}

/*
    subtract_floats - used by fn_subtract with more than one argument
    Converts the numbers in args to floats, subtracts them from fresult, and returns the result as a Lisp float.
*/
object* subtract_floats (object* args, float fresult) {
    while (args != NULL) {
        object* arg = car(args);
        fresult = fresult - checkintfloat(arg);
        args = cdr(args);
    }
    return makefloat(fresult);
}

/*
    negate - used by fn_subtract with one argument
    If the result is an integer, and negating it doesn't overflow, keep the result as an integer.
    Otherwise convert the result to a float, negate it, and return the result as a Lisp float.
*/
object* negate (object* arg) {
    if (integerp(arg)) {
        int result = arg->integer;
        if (result == INT_MIN) return makefloat(-result);
        else return number(-result);
    } else if (floatp(arg)) return makefloat(-(arg->single_float));
    else error(notanumber, arg);
    return nil;
}

/*
    multiply_floats - used by fn_multiply
    Converts the numbers in args to floats, adds them to fresult, and returns the result as a Lisp float.
*/
object* multiply_floats (object* args, float fresult) {
    while (args != NULL) {
     object* arg = car(args);
        fresult = fresult * checkintfloat(arg);
        args = cdr(args);
    }
    return makefloat(fresult);
}

/*
    divide_floats - used by fn_divide
    Converts the numbers in args to floats, divides fresult by them, and returns the result as a Lisp float.
*/
object* divide_floats (object* args, float fresult) {
    while (args != NULL) {
        object* arg = car(args);
        float f = checkintfloat(arg);
        if (f == 0.0) error2(divisionbyzero);
        fresult = fresult / f;
        args = cdr(args);
    }
    return makefloat(fresult);
}

/*
    myround - rounds
    Returns t if the argument is a floating-point number.
*/
int myround (float number) {
    return (number >= 0) ? (int)(number + 0.5) : (int)(number - 0.5);
}

/*
    compare - a generic compare function
    Used to implement the other comparison functions.
    If lt is true the result is true if each argument is less than the next argument.
    If gt is true the result is true if each argument is greater than the next argument.
    If eq is true the result is true if each argument is equal to the next argument.
*/
object* compare (object* args, bool lt, bool gt, bool eq) {
    object* arg1 = first(args);
    args = cdr(args);
    while (args != NULL) {
        object* arg2 = first(args);
        if (integerp(arg1) && integerp(arg2)) {
            if (!lt && ((arg1->integer) < (arg2->integer))) return nil;
            if (!eq && ((arg1->integer) == (arg2->integer))) return nil;
            if (!gt && ((arg1->integer) > (arg2->integer))) return nil;
        } else {
            if (!lt && (checkintfloat(arg1) < checkintfloat(arg2))) return nil;
            if (!eq && (checkintfloat(arg1) == checkintfloat(arg2))) return nil;
            if (!gt && (checkintfloat(arg1) > checkintfloat(arg2))) return nil;
        }
        arg1 = arg2;
        args = cdr(args);
    }
    return tee;
}

/*
    intpower - calculates base to the power exp as an integer
*/
int intpower (int base, int exp) {
    int result = 1;
    while (exp) {
        if (exp & 1) result = result * base;
        exp = exp / 2;
        base = base * base;
    }
    return result;
}

// Association lists

/*
    assoc - looks for key in an association list and returns the matching pair, or nil if not found
*/
object* assoc (object* key, object* list) {
    while (list != NULL) {
        if (improperp(list)) error(notproper, list);
        object* pair = first(list);
        if (!listp(pair)) error(PSTR("element is not a list"), pair);
        if (pair != NULL && eq(key,car(pair))) return pair;
        list = cdr(list);
    }
    return nil;
}

/*
    delassoc - deletes the pair matching key from an association list and returns the key, or nil if not found
*/
object* delassoc (object* key, object** alist) {
    object* list = *alist;
    object* prev = NULL;
    while (list != NULL) {
        object* pair = first(list);
        if (eq(key,car(pair))) {
            if (prev == NULL) *alist = cdr(list);
            else cdr(prev) = cdr(list);
            return key;
        }
        prev = list;
        list = cdr(list);
    }
    return nil;
}

// Array utilities

/*
    nextpower2 - returns the smallest power of 2 that is equal to or greater than n
*/
int nextpower2 (int n) {
    n--; n |= n >> 1; n |= n >> 2; n |= n >> 4;
    n |= n >> 8; n |= n >> 16; n++;
    return n<2 ? 2 : n;
}

/*
    buildarray - builds an array with n elements using a tree of size s which must be a power of 2
    The elements are initialised to the default def
*/
object* buildarray (int n, int s, object* def) {
    int s2 = s>>1;
    if (s2 == 1) {
        if (n == 2) return cons(def, def);
        else if (n == 1) return cons(def, NULL);
        else return NULL;
    } else if (n >= s2) return cons(buildarray(s2, s2, def), buildarray(n - s2, s2, def));
    else return cons(buildarray(n, s2, def), nil);
}

object* makearray (object* dims, object* def, bool bitp) {
    int size = 1;
    object* dimensions = dims;
    while (dims != NULL) {
        int d = car(dims)->integer;
        if (d < 0) error2(PSTR("dimension can't be negative"));
        size = size * d;
        dims = cdr(dims);
    }
    // Bit array identified by making first dimension negative
    if (bitp) {
        size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
        car(dimensions) = number(-(car(dimensions)->integer));
    }
    object* ptr = myalloc();
    ptr->type = ARRAY;
    object* tree = nil;
    if (size != 0) tree = buildarray(size, nextpower2(size), def);
    ptr->cdr = cons(tree, dimensions);
    return ptr;
}

/*
    arrayref - returns a pointer to the element specified by index in the array of size s
*/
object** arrayref (object* array, int index, int size) {
    int mask = nextpower2(size)>>1;
    object** p = &car(cdr(array));
    while (mask) {
        if ((index & mask) == 0) p = &(car(*p)); else p = &(cdr(*p));
        mask = mask>>1;
    }
    return p;
}

/*
    getarray - gets a pointer to an element in a multi-dimensional array, given a list of the subscripts subs
    If the first subscript is negative it's a bit array and bit is set to the bit number
*/
object** getarray (object* array, object* subs, object* env, int *bit) {
    int index = 0, size = 1, s;
    *bit = -1;
    bool bitp = false;
    object* dims = cddr(array);
    while (dims != NULL && subs != NULL) {
        int d = car(dims)->integer;
        if (d < 0) { d = -d; bitp = true; }
        if (env) s = checkinteger(eval(car(subs), env)); else s = checkinteger(car(subs));
        if (s < 0 || s >= d) error(PSTR("subscript out of range"), car(subs));
        size = size * d;
        index = index * d + s;
        dims = cdr(dims); subs = cdr(subs);
    }
    if (dims != NULL) error2(PSTR("too few subscripts"));
    if (subs != NULL) error2(PSTR("too many subscripts"));
    if (bitp) {
        size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
        *bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
        index = index>>(sizeof(int)==4 ? 5 : 4);
    }
    return arrayref(array, index, size);
}

/*
    rslice - reads a slice of an array recursively
*/
void rslice (object* array, int size, int slice, object* dims, object* args) {
    int d = first(dims)->integer;
    for (int i = 0; i < d; i++) {
        int index = slice * d + i;
        if (!consp(args)) error2(PSTR("initial contents don't match array type"));
        if (cdr(dims) == NULL) {
            object** p = arrayref(array, index, size);
            *p = car(args);
        } else rslice(array, size, index, cdr(dims), car(args));
        args = cdr(args);
    }
}

/*
    readarray - reads a list structure from args and converts it to a d-dimensional array.
    Uses rslice for each of the slices of the array.
*/
object* readarray (int d, object* args) {
    object* list = args;
    object* dims = NULL; object* head = NULL;
    int size = 1;
    for (int i = 0; i < d; i++) {
        if (!listp(list)) error2(PSTR("initial contents don't match array type"));
        int l = listlength(list);
        if (dims == NULL) { dims = cons(number(l), NULL); head = dims; }
        else { cdr(dims) = cons(number(l), NULL); dims = cdr(dims); }
        size = size * l;
        if (list != NULL) list = car(list);
    }
    object* array = makearray(head, NULL, false);
    rslice(array, size, 0, head, args);
    return array;
}

/*
    readbitarray - reads an item in the format #*1010101000110 by reading it and returning a list of integers,
    and then converting that to a bit array
*/
object* readbitarray (gfun_t gfun) {
    char ch = gfun();
    object* head = NULL;
    object* tail = NULL;
    while (!issp(ch) && !isbr(ch)) {
        if (ch != '0' && ch != '1') error2(PSTR("illegal character in bit array"));
        object* cell = cons(number(ch - '0'), NULL);
        if (head == NULL) head = cell;
        else tail->cdr = cell;
        tail = cell;
        ch = gfun();
    }
    LastChar = ch;
    int size = listlength(head);
    object* array = makearray(cons(number(size), NULL), number(0), true);
    size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    int index = 0;
    while (head != NULL) {
        object** loc = arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size);
        int bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
        *loc = number((((*loc)->integer) & ~(1<<bit)) | (car(head)->integer)<<bit);
        index++;
        head = cdr(head);
    }
    return array;
}

/*
    pslice - prints a slice of an array recursively
*/
void pslice (object* array, int size, int slice, object* dims, pfun_t pfun, bool bitp) {
    bool spaces = true;
    if (slice == -1) { spaces = false; slice = 0; }
    int d = first(dims)->integer;
    if (d < 0) d = -d;
    for (int i = 0; i < d; i++) {
        if (i && spaces) pfun(' ');
        int index = slice * d + i;
        if (cdr(dims) == NULL) {
            if (bitp) pint(((*arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size))->integer)>>
                (index & (sizeof(int)==4 ? 0x1F : 0x0F)) & 1, pfun);
            else printobject(*arrayref(array, index, size), pfun);
        } else { pfun('('); pslice(array, size, index, cdr(dims), pfun, bitp); pfun(')'); }
    }
}

/*
    printarray - prints an array in the appropriate Lisp format
*/
void printarray (object* array, pfun_t pfun) {
    object* dimensions = cddr(array);
    object* dims = dimensions;
    bool bitp = false;
    int size = 1, n = 0;
    while (dims != NULL) {
        int d = car(dims)->integer;
        if (d < 0) { bitp = true; d = -d; }
        size = size * d;
        dims = cdr(dims); n++;
    }
    if (bitp) size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    pfun('#');
    if (n == 1 && bitp) { pfun('*'); pslice(array, size, -1, dimensions, pfun, bitp); }
    else {
        if (n > 1) { pint(n, pfun); pfun('A'); }
        pfun('('); pslice(array, size, 0, dimensions, pfun, bitp); pfun(')');
    }
}

// String utilities

void indent (uint8_t spaces, char ch, pfun_t pfun) {
    for (uint8_t i=0; i<spaces; i++) pfun(ch);
}

/*
    startstring - starts building a string
*/
object* startstring () {
    object* string = newstring();
    GlobalString = string;
    GlobalStringTail = string;
    return string;
}

/*
    princtostring - implements Lisp princtostring function
*/
object* princtostring (object* arg) {
    object* obj = startstring();
    prin1object(arg, pstr);
    return obj;
}

/*
    buildstring - adds a character on the end of a string
    Handles Lisp strings packed four characters per 32-bit word
*/
void buildstring (char ch, object** tail) {
    object* cell;
    if (cdr(*tail) == NULL) {
        cell = myalloc(); cdr(*tail) = cell;
    } else if (((*tail)->chars & 0xFFFFFF) == 0) {
        (*tail)->chars = (*tail)->chars | ch<<16; return;
    } else if (((*tail)->chars & 0xFFFF) == 0) {
        (*tail)->chars = (*tail)->chars | ch<<8; return;
    } else if (((*tail)->chars & 0xFF) == 0) {
        (*tail)->chars = (*tail)->chars | ch; return;
    } else {
        cell = myalloc(); car(*tail) = cell;
    }
    car(cell) = NULL; cell->chars = ch<<24; *tail = cell;
}

/*
    copystring - returns a copy of a Lisp string
*/
object* copystring (object* arg) {
    object* obj = newstring();
    object* ptr = obj;
    arg = cdr(arg);
    while (arg != NULL) {
        object* cell = myalloc(); car(cell) = NULL;
        if (cdr(obj) == NULL) cdr(obj) = cell; else car(ptr) = cell;
        ptr = cell;
        ptr->chars = arg->chars;
        arg = car(arg);
    }
    return obj;
}

/*
    readstring - reads characters from an input stream up to delimiter delim
    and returns a Lisp string
*/
object* readstring (char delim, gfun_t gfun) {
    object* obj = newstring();
    object* tail = obj;
    int ch = gfun();
    if (ch == -1) return nil;
    while ((ch != delim) && (ch != -1)) {
        if (ch == '\\') ch = gfun();
        buildstring(ch, &tail);
        ch = gfun();
    }
    return obj;
}

/*
    stringlength - returns the length of a Lisp string
    Handles Lisp strings packed two characters per 16-bit word, or four characters per 32-bit word
*/
int stringlength (object* form) {
    int length = 0;
    form = cdr(form);
    while (form != NULL) {
        int chars = form->chars;
        for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
            if (chars>>i & 0xFF) length++;
        }
        form = car(form);
    }
    return length;
}

/*
    nthchar - returns the nth character from a Lisp string
    Handles Lisp strings packed two characters per 16-bit word, or four characters per 32-bit word
*/
char nthchar (object* string, int n) {
    object* arg = cdr(string);
    int top;
    if (sizeof(int) == 4) { top = n>>2; n = 3 - (n&3); }
    else { top = n>>1; n = 1 - (n&1); }
    for (int i=0; i<top; i++) {
        if (arg == NULL) return 0;
        arg = car(arg);
    }
    if (arg == NULL) return 0;
    return (arg->chars)>>(n*8) & 0xFF;
}

/*
    gstr - reads a character from a string stream
*/
int gstr () {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    char c = nthchar(GlobalString, GlobalStringIndex++);
    if (c != 0) return c;
    return '\n'; // -1?
}

/*
    pstr - prints a character to a string stream
*/
void pstr (char c) {
    buildstring(c, &GlobalStringTail);
}

/*
    lispstring - converts a C string to a Lisp string
*/
object* lispstring (char* s) {
    object* obj = newstring();
    object* tail = obj;
    while(1) {
        char ch = *s++;
        if (ch == 0) break;
        if (ch == '\\') ch = *s++;
        buildstring(ch, &tail);
    }
    return obj;
}

/*
    stringcompare - a generic string compare function
    Used to implement the other string comparison functions.
    If lt is true the result is true if each argument is less than the next argument.
    If gt is true the result is true if each argument is greater than the next argument.
    If eq is true the result is true if each argument is equal to the next argument.
*/
bool stringcompare (object* args, bool lt, bool gt, bool eq) {
    object* arg1 = checkstring(first(args));
    object* arg2 = checkstring(second(args));
    arg1 = cdr(arg1);
    arg2 = cdr(arg2);
    while ((arg1 != NULL) || (arg2 != NULL)) {
        if (arg1 == NULL) return lt;
        if (arg2 == NULL) return gt;
        if (arg1->chars < arg2->chars) return lt;
        if (arg1->chars > arg2->chars) return gt;
        arg1 = car(arg1);
        arg2 = car(arg2);
    }
    return eq;
}

/*
    documentation - returns the documentation string of a built-in or user-defined function.
*/
object* documentation (object* arg, object* env) {
    if (arg == NULL) return nil;
    if (!symbolp(arg)) error(notasymbol, arg);
    object* pair = findpair(arg, env);
    if (pair != NULL) {
        object* val = cdr(pair);
        if (listp(val) && first(val)->name == sym(LAMBDA) && cdr(val) != NULL && cddr(val) != NULL) {
            if (stringp(third(val))) return third(val);
        }
    }
    symbol_t docname = arg->name;
    if (!builtinp(docname)) return nil;
    char* docstring = lookupdoc(builtin(docname));
    if (docstring == NULL) return nil;
    object* obj = startstring();
    pfstring(docstring, pstr);
    return obj;
}

/*
    apropos - finds the user-defined and built-in functions whose names contain the specified string or symbol,
    and prints them if print is true, or returns them in a list.
*/
object* apropos (object* arg, bool print) {
    char buf[17], buf2[33];
    char* part = cstring(princtostring(arg), buf, 17);
    object* result = cons(NULL, NULL);
    object* ptr = result;
    // User-defined?
    object* globals = GlobalEnv;
    while (globals != NULL) {
        object* pair = first(globals);
        object* var = car(pair);
        object* val = cdr(pair);
        char* full = cstring(princtostring(var), buf2, 33);
        if (strstr(full, part) != NULL) {
            if (print) {
                printsymbol(var, pserial); pserial(' '); pserial('(');
                if (consp(val) && symbolp(car(val)) && builtin(car(val)->name) == LAMBDA) pfstring(PSTR("user function"), pserial);
                else if (consp(val) && car(val)->type == CODE) pfstring(PSTR("code"), pserial);
                else pfstring(PSTR("user symbol"), pserial);
                pserial(')'); pln(pserial);
            } else {
                cdr(ptr) = cons(var, NULL); ptr = cdr(ptr);
            }
        }
        globals = cdr(globals);
    }
    // Built-in?
    int entries = 0, i;
    for (i = 0; i < NumTables; i++) entries += tablesize(i);
    for (i = 0; i < entries; i++) {
        if (findsubstring(part, (builtin_t)i)) {
            if (print) {
                uint8_t ft = fntype(getminmax(i));
                pbuiltin((builtin_t)i, pserial); pserial(' '); pserial('(');
                if (ft == FUNCTIONS) pfstring(PSTR("function"), pserial);
                else if (ft == SPECIAL_FORMS) pfstring(PSTR("special form"), pserial);
                else pfstring(PSTR("symbol/keyword"), pserial);
                pserial(')'); pln(pserial);
            } else {
                cdr(ptr) = cons(bsymbol(i), NULL); ptr = cdr(ptr);
            }
        }
    }
    return cdr(result);
}

/*
    cstring - converts a Lisp string to a C string in buffer and returns buffer
    Handles Lisp strings packed two characters per 16-bit word, or four characters per 32-bit word
*/
char* cstring (object* form, char* buffer, int buflen) {
    form = cdr(checkstring(form));
    int index = 0;
    while (form != NULL) {
        int chars = form->integer;
        for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
            char ch = chars>>i & 0xFF;
            if (ch) {
                if (index >= buflen-1) error2(PSTR("no room for string"));
                buffer[index++] = ch;
            }
        }
        form = car(form);
    }
    buffer[index] = '\0';
    return buffer;
}

/*
    ipstring - parses an IP address from a Lisp string and returns it as an IPAddress type (uint32_t)
    Handles Lisp strings packed two characters per 16-bit word, or four characters per 32-bit word
*/
uint32_t ipstring (object* form) {
    form = cdr(checkstring(form));
    int p = 0;
    union { uint32_t ipaddress; uint8_t ipbytes[4]; } ;
    ipaddress = 0;
    while (form != NULL) {
        int chars = form->integer;
        for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
            char ch = chars>>i & 0xFF;
            if (ch) {
                if (ch == '.') { p++; if (p > 3) error2(PSTR("illegal IP address")); }
                else ipbytes[p] = (ipbytes[p] * 10) + ch - '0';
            }
        }
        form = car(form);
    }
    return ipaddress;
}

// Lookup variable in environment

object* value (symbol_t n, object* env) {
    while (env != NULL) {
        object* pair = car(env);
        if (pair != NULL && car(pair)->name == n) return pair;
        env = cdr(env);
    }
    return nil;
}

/*
    findpair - returns the (var . value) pair bound to variable var in the local or global environment
*/
object* findpair (object* var, object* env) {
    symbol_t name = var->name;
    object* pair = value(name, env);
    if (pair == NULL) pair = value(name, GlobalEnv);
    return pair;
}

/*
    boundp - tests whether var is bound to a value
*/
bool boundp (object* var, object* env) {
    if (!symbolp(var)) error(notasymbol, var);
    return (findpair(var, env) != NULL);
}

/*
    findvalue - returns the value bound to variable var, or gives an error if unbound
*/
object* findvalue (object* var, object* env) {
    object* pair = findpair(var, env);
    if (pair == NULL) error(PSTR("unknown variable"), var);
    return pair;
}

// Handling closures

object* closure (int tc, symbol_t name, object* function, object* args, object** env) {
    object* state = car(function);
    function = cdr(function);
    int trace = 0;
    if (name) trace = tracing(name);
    if (trace) {
        indent(TraceDepth[trace-1]<<1, ' ', pserial);
        pint(TraceDepth[trace-1]++, pserial);
        pserial(':'); pserial(' '); pserial('('); printsymbol(symbol(name), pserial);
    }
    object* params = first(function);
    if (!listp(params)) errorsym(name, notalist, params);
    function = cdr(function);
    // Dropframe
    if (tc) {
        if (*env != NULL && car(*env) == NULL) {
            pop(*env);
            while (*env != NULL && car(*env) != NULL) pop(*env);
        } else push(nil, *env);
    }
    // Push state
    while (consp(state)) {
        object* pair = first(state);
        push(pair, *env);
        state = cdr(state);
    }
    // Add arguments to environment
    bool optional = false;
    while (params != NULL) {
        object* value;
        object* var = first(params);
        if (isbuiltin(var, OPTIONAL)) optional = true;
        else {
            if (consp(var)) {
                if (!optional) errorsym(name, PSTR("invalid default value"), var);
                if (args == NULL) value = eval(second(var), *env);
                else { value = first(args); args = cdr(args); }
                var = first(var);
                if (!symbolp(var)) errorsym(name, PSTR("illegal optional parameter"), var);
            } else if (!symbolp(var)) {
                errorsym(name, PSTR("illegal function parameter"), var);
            } else if (isbuiltin(var, AMPREST)) {
                params = cdr(params);
                var = first(params);
                value = args;
                args = NULL;
            } else {
                if (args == NULL) {
                    if (optional) value = nil;
                    else errorsym2(name, toofewargs);
                } else { value = first(args); args = cdr(args); }
            }
            push(cons(var,value), *env);
            if (trace) { pserial(' '); printobject(value, pserial); }
        }
        params = cdr(params);
    }
    if (args != NULL) errorsym2(name, toomanyargs);
    if (trace) { pserial(')'); pln(pserial); }
    // Do an implicit progn
    if (tc) push(nil, *env);
    return tf_progn(function, *env);
}

object* apply (object* function, object* args, object* env) {
    if (symbolp(function)) {
        builtin_t fname = builtin(function->name);
        if ((fname < ENDFUNCTIONS) && (fntype(getminmax(fname)) == FUNCTIONS)) {
            Context = fname;
            checkargs(args);
            return ((fn_ptr_type)lookupfn(fname))(args, env);
        } else function = eval(function, env);
    }
    if (consp(function) && isbuiltin(car(function), LAMBDA)) {
        object* result = closure(0, sym(NIL), function, args, &env);
        return eval(result, env);
    }
    if (consp(function) && isbuiltin(car(function), CLOSURE)) {
        function = cdr(function);
        object* result = closure(0, sym(NIL), function, args, &env);
        return eval(result, env);
    }
    error(PSTR("illegal function"), function);
    return NULL;
}

// In-place operations

/*
    place - returns a pointer to an object referenced in the second argument of an
    in-place operation such as setf. bit is used to indicate the bit position in a bit array
*/
object** place (object* args, object* env, int *bit) {
    *bit = -1;
    if (atom(args)) return &cdr(findvalue(args, env));
    object* function = first(args);
    if (symbolp(function)) {
        symbol_t sname = function->name;
        if (sname == sym(CAR) || sname == sym(FIRST)) {
            object* value = eval(second(args), env);
            if (!listp(value)) error(canttakecar, value);
            return &car(value);
        }
        if (sname == sym(CDR) || sname == sym(REST)) {
            object* value = eval(second(args), env);
            if (!listp(value)) error(canttakecdr, value);
            return &cdr(value);
        }
        if (sname == sym(NTH)) {
            int index = checkinteger(eval(second(args), env));
            object* list = eval(third(args), env);
            if (atom(list)) error(PSTR("second argument to nth is not a list"), list);
            while (index > 0) {
                list = cdr(list);
                if (list == NULL) error2(PSTR("index to nth is out of range"));
                index--;
            }
            return &car(list);
        }
        if (sname == sym(AREF)) {
            object* array = eval(second(args), env);
            if (!arrayp(array)) error(PSTR("first argument is not an array"), array);
            return getarray(array, cddr(args), env, bit);
        }
    }
    error2(PSTR("illegal place"));
    return nil;
}

// Checked car and cdr

/*
    carx - car with error checking
*/
object* carx (object* arg) {
    if (!listp(arg)) error(canttakecar, arg);
    if (arg == nil) return nil;
    return car(arg);
}

/*
    cdrx - cdr with error checking
*/
object* cdrx (object* arg) {
    if (!listp(arg)) error(canttakecdr, arg);
    if (arg == nil) return nil;
    return cdr(arg);
}

/*
    cxxxr - implements a general cxxxr function, 
    pattern is a sequence of bits 0b1xxx where x is 0 for a and 1 for d.
*/
object* cxxxr (object* args, uint8_t pattern) {
    object* arg = first(args);
    while (pattern != 1) {
        if ((pattern & 1) == 0) arg = carx(arg); else arg = cdrx(arg);
        pattern = pattern>>1;
    }
    return arg;
}

// Mapping helper functions

/*
    mapcarfun - function specifying how to combine the results in mapcar
*/
void mapcarfun (object* result, object** tail) {
    object* obj = cons(result,NULL);
    cdr(*tail) = obj; *tail = obj;
}

/*
    mapcanfun - function specifying how to combine the results in mapcan
*/
void mapcanfun (object* result, object** tail) {
    if (cdr(*tail) != NULL) error(notproper, *tail);
    while (consp(result)) {
        cdr(*tail) = result; *tail = result;
        result = cdr(result);
    }
}

/*
    mapcarcan - function used by marcar and mapcan
    It takes the arguments, the env, and a function specifying how the results are combined.
*/
object* mapcarcan (object* args, object* env, mapfun_t fun) {
    object* function = first(args);
    args = cdr(args);
    object* params = cons(NULL, NULL);
    push(params,GCStack);
    object* head = cons(NULL, NULL);
    push(head,GCStack);
    object* tail = head;
    // Make parameters
    while (true) {
        object* tailp = params;
        object* lists = args;
        while (lists != NULL) {
            object* list = car(lists);
            if (list == NULL) {
                 pop(GCStack); pop(GCStack);
                 return cdr(head);
            }
            if (improperp(list)) error(notproper, list);
            object* obj = cons(first(list),NULL);
            car(lists) = cdr(list);
            cdr(tailp) = obj; tailp = obj;
            lists = cdr(lists);
        }
        object* result = apply(function, cdr(params), env);
        fun(result, &tail);
    }
}

// I2C interface for one port, using Arduino Wire

void I2Cinit (bool enablePullup) {
    (void) enablePullup;
    Wire.begin();
}

int I2Cread () {
    return Wire.read();
}

void I2Cwrite (uint8_t data) {
    Wire.write(data);
}

bool I2Cstart (uint8_t address, uint8_t read) {
 int ok = true;
 if (read == 0) {
     Wire.beginTransmission(address);
     ok = (Wire.endTransmission(true) == 0);
     Wire.beginTransmission(address);
 }
 else Wire.requestFrom(address, I2Ccount);
 return ok;
}

bool I2Crestart (uint8_t address, uint8_t read) {
    int error = (Wire.endTransmission(false) != 0);
    if (read == 0) Wire.beginTransmission(address);
    else Wire.requestFrom(address, I2Ccount);
    return error ? false : true;
}

void I2Cstop (uint8_t read) {
    if (read == 0) Wire.endTransmission(); // Check for error?
}

// Streams

inline int spiread () { return SPI.transfer(0); }
inline int serial1read () { while (!Serial1.available()) testescape(); return Serial1.read(); }
#if defined(sdcardsupport)
File SDpfile, SDgfile;
inline int SDread () {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    return SDgfile.read();
}
#endif

WiFiClient client;
WiFiServer server(80);

inline int WiFiread () {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    return client.read();
}

void serialbegin (int address, int baud) {
    if (address == 1) Serial1.begin((long)baud*100);
    else error(PSTR("port not supported"), number(address));
}

void serialend (int address) {
    if (address == 1) {Serial1.flush(); Serial1.end(); }
}

gfun_t gstreamfun (object* args) {
    int streamtype = SERIALSTREAM;
    int address = 0;
    gfun_t gfun = gserial;
    if (args != NULL) {
        int stream = isstream(first(args));
        streamtype = stream>>8; address = stream & 0xFF;
    }
    if (streamtype == I2CSTREAM) gfun = (gfun_t)I2Cread;
    else if (streamtype == SPISTREAM) gfun = spiread;
    else if (streamtype == SERIALSTREAM) {
        if (address == 0) gfun = gserial;
        else if (address == 1) gfun = serial1read;
    }
    #if defined(sdcardsupport)
    else if (streamtype == SDSTREAM) gfun = (gfun_t)SDread;
    #endif
    else if (streamtype == WIFISTREAM) gfun = (gfun_t)WiFiread;
    else error2(PSTR("unknown stream type"));
    return gfun;
}

inline void spiwrite (char c) { SPI.transfer(c); }
inline void serial1write (char c) { Serial1.write(c); }
inline void WiFiwrite (char c) { client.write(c); }
#if defined(sdcardsupport)
inline void SDwrite (char c) { SDpfile.write(c); }
#endif
#if defined(gfxsupport)
inline void gfxwrite (char c) { tft.write(c); }
#endif

pfun_t pstreamfun (object* args) {
    int streamtype = SERIALSTREAM;
    int address = 0;
    pfun_t pfun = pserial;
    if (args != NULL && first(args) != NULL) {
        int stream = isstream(first(args));
        streamtype = stream>>8; address = stream & 0xFF;
    }
    if (streamtype == I2CSTREAM) pfun = (pfun_t)I2Cwrite;
    else if (streamtype == SPISTREAM) pfun = spiwrite;
    else if (streamtype == SERIALSTREAM) {
        if (address == 0) pfun = pserial;
        else if (address == 1) pfun = serial1write;
    }
    else if (streamtype == STRINGSTREAM) {
        pfun = pstr;
    }
    #if defined(sdcardsupport)
    else if (streamtype == SDSTREAM) pfun = (pfun_t)SDwrite;
    #endif
    #if defined(gfxsupport)
    else if (streamtype == GFXSTREAM) pfun = (pfun_t)gfxwrite;
    #endif
    else if (streamtype == WIFISTREAM) pfun = (pfun_t)WiFiwrite;
    else error2(PSTR("unknown stream type"));
    return pfun;
}

// Check pins

void checkanalogread (int pin) {
     
//   if (!(pin==0 || pin==2 || pin==4 || (pin>=12 && pin<=15) || (pin>=25 && pin<=27) || (pin>=32 && pin<=36) || pin==39))
//     error(PSTR("invalid pin"), number(pin));
    (void)pin;

}

void checkanalogwrite (int pin) {
//   if (!(pin>=25 && pin<=26)) error(PSTR("invalid pin"), number(pin));
    (void)pin;
}

// Note

#ifndef tone
void tone (int pin, int note) {
    (void) pin, (void) note;
}
#endif

#ifndef noTone
void noTone (int pin) {
    (void) pin;
}
#endif

const int scale[] PROGMEM = {4186,4435,4699,4978,5274,5588,5920,6272,6645,7040,7459,7902};

void playnote (int pin, int note, int octave) {
    int prescaler = 8 - octave - note/12;
    if (prescaler<0 || prescaler>8) error(PSTR("octave out of range"), number(prescaler));
    tone(pin, pgm_read_word(&scale[note%12])>>prescaler);
}

void nonote (int pin) {
    noTone(pin);
}

// Sleep

void initsleep () { }

void doze (int secs) {
    delay(1000 * secs);
}

// Prettyprint

const int PPINDENT = 2;
const int PPWIDTH = 80;
const int GFXPPWIDTH = 52; // 320 pixel wide screen
int ppwidth = PPWIDTH;

void pcount (char c) {
    if (c == '\n') PrintCount++;
    PrintCount++;
}

/*
    atomwidth - calculates the character width of an atom
*/
uint8_t atomwidth (object* obj) {
    PrintCount = 0;
    printobject(obj, pcount);
    return PrintCount;
}

uint8_t basewidth (object* obj, uint8_t base) {
    PrintCount = 0;
    pintbase(obj->integer, base, pcount);
    return PrintCount;
}

bool quoted (object* obj) {
    return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(QUOTE) && consp(cdr(obj)) && cddr(obj) == NULL);
}

int subwidth (object* obj, int w) {
    if (atom(obj)) return w - atomwidth(obj);
    if (quoted(obj)) obj = car(cdr(obj));
    return subwidthlist(obj, w - 1);
}

int subwidthlist (object* form, int w) {
    while (form != NULL && w >= 0) {
        if (atom(form)) return w - (2 + atomwidth(form));
        w = subwidth(car(form), w - 1);
        form = cdr(form);
    }
    return w;
}

/*
    superprint - the main pretty-print subroutine
*/
void superprint (object* form, int lm, pfun_t pfun) {
    if (atom(form)) {
        if (symbolp(form) && form->name == sym(NOTHING)) printsymbol(form, pfun);
        else printobject(form, pfun);
    }
    else if (quoted(form)) { pfun('\''); superprint(car(cdr(form)), lm + 1, pfun); }
    else if (subwidth(form, ppwidth - lm) >= 0) supersub(form, lm + PPINDENT, 0, pfun);
    else supersub(form, lm + PPINDENT, 1, pfun);
}

/*
    supersub - subroutine used by pprint
*/
void supersub (object* form, int lm, int super, pfun_t pfun) {
    int special = 0, separate = 1;
    object* arg = car(form);
    if (symbolp(arg) && builtinp(arg->name)) {
        minmax_t minmax = getminmax(builtin(arg->name));
        if (minmax == MINMAX(SPECIAL_FORMS, 2, UNLIMITED) || minmax == MINMAX(SPECIAL_FORMS, 1, 3)) special = 2; // defun, setq, setf, defvar
        else if (minmax == MINMAX(SPECIAL_FORMS, 1, UNLIMITED) || minmax == MINMAX(OTHER_FORMS, 1, UNLIMITED) || minmax == MINMAX(TAIL_FORMS, 1, UNLIMITED) || minmax == MINMAX(TAIL_FORMS, 2, 3)) special = 1;
    }
    while (form != NULL) {
        if (atom(form)) { pfstring(PSTR(" . "), pfun); printobject(form, pfun); pfun(')'); return; }
        else if (separate) { pfun('('); separate = 0; }
        else if (special) { pfun(' '); special--; }
        else if (!super) pfun(' ');
        else { pln(pfun); indent(lm, ' ', pfun); }
        superprint(car(form), lm, pfun);
        form = cdr(form);
    }
    pfun(')'); return;
}

/*
    edit - the Lisp tree editor
    Steps through a function definition, editing it a bit at a time, using single-key editing commands.
*/
object* edit (object* fun) {
    while (1) {
        if (tstflag(EXITEDITOR)) return fun;
        char c = gserial();
        if (c == 'q') setflag(EXITEDITOR);
        else if (c == 'b') return fun;
        else if (c == 'r') fun = read(gserial);
        else if (c == '\n') { pfl(pserial); superprint(fun, 0, pserial); pln(pserial); }
        else if (c == 'c') fun = cons(read(gserial), fun);
        else if (atom(fun)) pserial('!');
        else if (c == 'd') fun = cons(car(fun), edit(cdr(fun)));
        else if (c == 'a') fun = cons(edit(car(fun)), cdr(fun));
        else if (c == 'x') fun = cdr(fun);
        else pserial('?');
    }
}

// Special forms

object* sp_quote (object* args, object* env) {
    (void) env;
    checkargs(args);
    return first(args);
}

/*
    (or item*)
    Evaluates its arguments until one returns non-nil, and returns its value.
*/
object* sp_or (object* args, object* env) {
    while (args != NULL) {
        object* val = eval(car(args), env);
        if (val != NULL) return val;
        args = cdr(args);
    }
    return nil;
}

/*
    (defun name (parameters) form*)
    Defines a function.
*/
object* sp_defun (object* args, object* env) {
    (void) env;
    checkargs(args);
    object* var = first(args);
    if (!symbolp(var)) error(notasymbol, var);
    object* val = cons(bsymbol(LAMBDA), cdr(args));
    object* pair = value(var->name, GlobalEnv);
    if (pair != NULL) cdr(pair) = val;
    else push(cons(var, val), GlobalEnv);
    return var;
}

/*
    (defvar variable form)
    Defines a global variable.
*/
object* sp_defvar (object* args, object* env) {
    checkargs(args);
    object* var = first(args);
    if (!symbolp(var)) error(notasymbol, var);
    object* val = NULL;
    args = cdr(args);
    if (args != NULL) { setflag(NOESC); val = eval(first(args), env); clrflag(NOESC); }
    object* pair = value(var->name, GlobalEnv);
    if (pair != NULL) cdr(pair) = val;
    else push(cons(var, val), GlobalEnv);
    return var;
}

/*
    (setq symbol value [symbol value]*)
    For each pair of arguments assigns the value of the second argument
    to the variable specified in the first argument.
*/
object* sp_setq (object* args, object* env) {
    object* arg = nil;
    while (args != NULL) {
        if (cdr(args) == NULL) error2(oddargs);
        object* pair = findvalue(first(args), env);
        arg = eval(second(args), env);
        cdr(pair) = arg;
        args = cddr(args);
    }
    return arg;
}

/*
    (loop forms*)
    Executes its arguments repeatedly until one of the arguments calls (return),
    which then causes an exit from the loop.
*/
object* sp_loop (object* args, object* env) {
    object* start = args;
    for (;;) {
        yield();
        args = start;
        while (args != NULL) {
            object* result = eval(car(args),env);
            if (tstflag(RETURNFLAG)) {
                clrflag(RETURNFLAG);
                return result;
            }
            args = cdr(args);
        }
    }
}

/*
    (return [value])
    Exits from a (dotimes ...), (dolist ...), or (loop ...) loop construct and returns value.
*/
object* sp_return (object* args, object* env) {
    object* result = eval(tf_progn(args,env), env);
    setflag(RETURNFLAG);
    return result;
}

/*
    (push item place)
    Modifies the value of place, which should be a list, to add item onto the front of the list,
    and returns the new list.
*/
object* sp_push (object* args, object* env) {
    int bit;
    checkargs(args);
    object* item = eval(first(args), env);
    object** loc = place(second(args), env, &bit);
    push(item, *loc);
    return *loc;
}

/*
    (pop place)
    Modifies the value of place, which should be a list, to remove its first item, and returns that item.
*/
object* sp_pop (object* args, object* env) {
    int bit;
    checkargs(args);
    object** loc = place(first(args), env, &bit);
    object* result = car(*loc);
    pop(*loc);
    return result;
}

// Accessors

/*
    (incf place [number])
    Increments a place, which should have an numeric value, and returns the result.
    The third argument is an optional increment which defaults to 1.
*/
object* sp_incf (object* args, object* env) {
    int bit;
    checkargs(args);
    object** loc = place(first(args), env, &bit);
    args = cdr(args);

    object* x = *loc;
    object* inc = (args != NULL) ? eval(first(args), env) : NULL;

    if (bit != -1) {
        int increment;
        if (inc == NULL) increment = 1; else increment = checkbitvalue(inc);
        int newvalue = (((*loc)->integer)>>bit & 1) + increment;

        if (newvalue & ~1) error2(PSTR("result is not a bit value"));
        *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
        return number(newvalue);
    }

    if (floatp(x) || floatp(inc)) {
        float increment;
        float value = checkintfloat(x);

        if (inc == NULL) increment = 1.0; else increment = checkintfloat(inc);

        *loc = makefloat(value + increment);
    } else if (integerp(x) && (integerp(inc) || inc == NULL)) {
        int increment;
        int value = x->integer;

        if (inc == NULL) increment = 1; else increment = inc->integer;

        if (increment < 1) {
            if (INT_MIN - increment > value) *loc = makefloat((float)value + (float)increment);
            else *loc = number(value + increment);
        } else {
            if (INT_MAX - increment < value) *loc = makefloat((float)value + (float)increment);
            else *loc = number(value + increment);
        }
    } else error2(notanumber);
    return *loc;
}

/*
    (decf place [number])
    Decrements a place, which should have an numeric value, and returns the result.
    The third argument is an optional decrement which defaults to 1.
*/
object* sp_decf (object* args, object* env) {
    int bit;
    checkargs(args);
    object** loc = place(first(args), env, &bit);
    args = cdr(args);

    object* x = *loc;
    object* dec = (args != NULL) ? eval(first(args), env) : NULL;

    if (bit != -1) {
        int decrement;
        if (dec == NULL) decrement = 1; else decrement = checkbitvalue(dec);
        int newvalue = (((*loc)->integer)>>bit & 1) - decrement;

        if (newvalue & ~1) error2(PSTR("result is not a bit value"));
        *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
        return number(newvalue);
    }

    if (floatp(x) || floatp(dec)) {
        float decrement;
        float value = checkintfloat(x);

        if (dec == NULL) decrement = 1.0; else decrement = checkintfloat(dec);

        *loc = makefloat(value - decrement);
    } else if (integerp(x) && (integerp(dec) || dec == NULL)) {
        int decrement;
        int value = x->integer;

        if (dec == NULL) decrement = 1; else decrement = dec->integer;

        if (decrement < 1) {
            if (INT_MAX + decrement < value) *loc = makefloat((float)value - (float)decrement);
            else *loc = number(value - decrement);
        } else {
            if (INT_MIN + decrement > value) *loc = makefloat((float)value - (float)decrement);
            else *loc = number(value - decrement);
        }
    } else error2(notanumber);
    return *loc;
}

/*
    (setf place value [place value]*)
    For each pair of arguments modifies a place to the result of evaluating value.
*/
object* sp_setf (object* args, object* env) {
    int bit;
    object* arg = nil;
    while (args != NULL) {
        if (cdr(args) == NULL) error2(oddargs);
        object** loc = place(first(args), env, &bit);
        arg = eval(second(args), env);
        if (bit == -1) *loc = arg;
        else *loc = number((checkinteger(*loc) & ~(1<<bit)) | checkbitvalue(arg)<<bit);
        args = cddr(args);
    }
    return arg;
}

// Other special forms

/*
    (dolist (var list [result]) form*)
    Sets the local variable var to each element of list in turn, and executes the forms.
    It then returns result, or nil if result is omitted.
*/
object* sp_dolist (object* args, object* env) {
    if (args == NULL || listlength(first(args)) < 2) error2(noargument);
    object* params = first(args);
    object* var = first(params);
    object* list = eval(second(params), env);
    push(list, GCStack); // Don't GC the list
    object* pair = cons(var,nil);
    push(pair,env);
    params = cdr(cdr(params));
    args = cdr(args);
    while (list != NULL) {
        if (improperp(list)) error(notproper, list);
        cdr(pair) = first(list);
        object* forms = args;
        while (forms != NULL) {
            object* result = eval(car(forms), env);
            if (tstflag(RETURNFLAG)) {
                clrflag(RETURNFLAG);
                pop(GCStack);
                return result;
            }
            forms = cdr(forms);
        }
        list = cdr(list);
    }
    cdr(pair) = nil;
    pop(GCStack);
    if (params == NULL) return nil;
    return eval(car(params), env);
}

/*
    (dotimes (var number [result]) form*)
    Executes the forms number times, with the local variable var set to each integer from 0 to number-1 in turn.
    It then returns result, or nil if result is omitted.
*/
object* sp_dotimes (object* args, object* env) {
    if (args == NULL || listlength(first(args)) < 2) error2(noargument);
    object* params = first(args);
    object* var = first(params);
    int count = checkinteger(eval(second(params), env));
    int index = 0;
    params = cdr(cdr(params));
    object* pair = cons(var,number(0));
    push(pair,env);
    args = cdr(args);
    while (index < count) {
        cdr(pair) = number(index);
        object* forms = args;
        while (forms != NULL) {
            object* result = eval(car(forms), env);
            if (tstflag(RETURNFLAG)) {
                clrflag(RETURNFLAG);
                return result;
            }
            forms = cdr(forms);
        }
        index++;
    }
    cdr(pair) = number(index);
    if (params == NULL) return nil;
    return eval(car(params), env);
}

/*
    (trace [function]*)
    Turns on tracing of up to TRACEMAX user-defined functions,
    and returns a list of the functions currently being traced.
*/
object* sp_trace (object* args, object* env) {
    (void) env;
    while (args != NULL) {
        object* var = first(args);
        if (!symbolp(var)) error(notasymbol, var);
        trace(var->name);
        args = cdr(args);
    }
    int i = 0;
    while (i < TRACEMAX) {
        if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
        i++;
    }
    return args;
}

/*
    (untrace [function]*)
    Turns off tracing of up to TRACEMAX user-defined functions, and returns a list of the functions untraced.
    If no functions are specified it untraces all functions.
*/
object* sp_untrace (object* args, object* env) {
    (void) env;
    if (args == NULL) {
        int i = 0;
        while (i < TRACEMAX) {
            if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
            TraceFn[i] = 0;
            i++;
        }
    } else {
        while (args != NULL) {
            object* var = first(args);
            if (!symbolp(var)) error(notasymbol, var);
            untrace(var->name);
            args = cdr(args);
        }
    }
    return args;
}

/*
    (for-millis ([number]) form*)
    Executes the forms and then waits until a total of number milliseconds have elapsed.
    Returns the total number of milliseconds taken.
*/
object* sp_formillis (object* args, object* env) {
    if (args == NULL) error2(noargument);
    object* param = first(args);
    unsigned long start = millis();
    unsigned long now, total = 0;
    if (param != NULL) total = checkinteger(eval(first(param), env));
    eval(tf_progn(cdr(args),env), env);
    do {
        now = millis() - start;
        testescape();
    } while (now < total);
    if (now <= INT_MAX) return number(now);
    return nil;
}

/*
    (time form)
    Prints the value returned by the form, and the time taken to evaluate the form
    in milliseconds or seconds.
*/
object* sp_time (object* args, object* env) {
    unsigned long start = millis();
    object* result = eval(first(args), env);
    unsigned long elapsed = millis() - start;
    printobject(result, pserial);
    pfstring(PSTR("\nTime: "), pserial);
    if (elapsed < 1000) {
        pint(elapsed, pserial);
        pfstring(PSTR(" ms\n"), pserial);
    } else {
        elapsed = elapsed+50;
        pint(elapsed/1000, pserial);
        pserial('.'); pint((elapsed/100)%10, pserial);
        pfstring(PSTR(" s\n"), pserial);
    }
    return bsymbol(NOTHING);
}

/*
    (with-output-to-string (str) form*)
    Returns a string containing the output to the stream variable str.
*/
object* sp_withoutputtostring (object* args, object* env) {
    if (args == NULL) error2(noargument);
    object* params = first(args);
    if (params == NULL) error2(nostream);
    object* var = first(params);
    object* pair = cons(var, stream(STRINGSTREAM, 0));
    push(pair,env);
    object* string = startstring();
    push(string, GCStack);
    object* forms = cdr(args);
    eval(tf_progn(forms,env), env);
    pop(GCStack);
    return string;
}

/*
    (with-serial (str port [baud]) form*)
    Evaluates the forms with str bound to a serial-stream using port.
    The optional baud gives the baud rate divided by 100, default 96.
*/
object* sp_withserial (object* args, object* env) {
    object* params = first(args);
    if (params == NULL) error2(nostream);
    object* var = first(params);
    int address = checkinteger(eval(second(params), env));
    params = cddr(params);
    int baud = 96;
    if (params != NULL) baud = checkinteger(eval(first(params), env));
    object* pair = cons(var, stream(SERIALSTREAM, address));
    push(pair,env);
    serialbegin(address, baud);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    serialend(address);
    return result;
}

/*
    (with-i2c (str [port] address [read-p]) form*)
    Evaluates the forms with str bound to an i2c-stream defined by address.
    If read-p is nil or omitted the stream is written to, otherwise it specifies the number of bytes
    to be read from the stream. The port if specified is ignored.
*/
object* sp_withi2c (object* args, object* env) {
    object* params = first(args);
    if (params == NULL) error2(nostream);
    object* var = first(params);
    int address = checkinteger(eval(second(params), env));
    params = cddr(params);
    if (address == 0 && params != NULL) params = cdr(params); // Ignore port
    int read = 0; // Write
    I2Ccount = 0;
    if (params != NULL) {
        object* rw = eval(first(params), env);
        if (integerp(rw)) I2Ccount = rw->integer;
        read = (rw != NULL);
    }
    I2Cinit(1); // Pullups
    object* pair = cons(var, (I2Cstart(address, read)) ? stream(I2CSTREAM, address) : nil);
    push(pair,env);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    I2Cstop(read);
    return result;
}

/*
    (with-spi (str pin [clock] [bitorder] [mode]) form*)
    Evaluates the forms with str bound to an spi-stream.
    The parameters specify the enable pin, clock in kHz (default 4000),
    bitorder 0 for LSBFIRST and 1 for MSBFIRST (default 1), and SPI mode (default 0).
*/
object* sp_withspi (object* args, object* env) {
    object* params = first(args);
    if (params == NULL) error2(nostream);
    object* var = first(params);
    params = cdr(params);
    if (params == NULL) error2(nostream);
    int pin = checkinteger(eval(car(params), env));
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    params = cdr(params);
    int clock = 4000, mode = SPI_MODE0; // Defaults
    int bitorder = MSBFIRST;
    if (params != NULL) {
        clock = checkinteger(eval(car(params), env));
        params = cdr(params);
        if (params != NULL) {
            bitorder = (checkinteger(eval(car(params), env)) == 0) ? LSBFIRST : MSBFIRST;
            params = cdr(params);
            if (params != NULL) {
                int modeval = checkinteger(eval(car(params), env));
                mode = (modeval == 3) ? SPI_MODE3 : (modeval == 2) ? SPI_MODE2 : (modeval == 1) ? SPI_MODE1 : SPI_MODE0;
            }
        }
    }
    object* pair = cons(var, stream(SPISTREAM, pin));
    push(pair,env);
    SPI.begin();
    SPI.beginTransaction(SPISettings(((unsigned long)clock * 1000), bitorder, mode));
    digitalWrite(pin, LOW);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    digitalWrite(pin, HIGH);
    SPI.endTransaction();
    return result;
}

/*
    (with-sd-card (str filename [mode]) form*)
    Evaluates the forms with str bound to an sd-stream reading from or writing to the file filename.
    If mode is omitted the file is read, otherwise 0 means read, 1 write-append, or 2 write-overwrite.
*/
object* sp_withsdcard (object* args, object* env) {
#if defined(sdcardsupport)
    object* params = first(args);
    if (params == NULL) error2(nostream);
    object* var = first(params);
    params = cdr(params);
    if (params == NULL) error2(PSTR("no filename specified"));
    object* filename = eval(first(params), env);
    params = cdr(params);
    SD.begin();
    int mode = 0;
    if (params != NULL && first(params) != NULL) mode = checkinteger(first(params));
    const char* oflag = FILE_READ;
    if (mode == 1) oflag = FILE_APPEND; else if (mode == 2) oflag = FILE_WRITE;
    if (mode >= 1) {
        char buffer[BUFFERSIZE];
        SDpfile = SD.open(MakeFilename(filename, buffer), oflag);
        if (!SDpfile) error2(PSTR("problem writing to SD card or invalid filename"));
    } else {
        char buffer[BUFFERSIZE];
        SDgfile = SD.open(MakeFilename(filename, buffer), oflag);
        if (!SDgfile) error2(PSTR("problem reading from SD card or invalid filename"));
    }
    object* pair = cons(var, stream(SDSTREAM, 1));
    push(pair,env);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    if (mode >= 1) SDpfile.close(); else SDgfile.close();
    return result;
#else
    (void) args, (void) env;
    error2(PSTR("not supported"));
    return nil;
#endif
}

// Tail-recursive forms

/*
    (progn form*)
    Evaluates several forms grouped together into a block, and returns the result of evaluating the last form.
*/
object* tf_progn (object* args, object* env) {
    if (args == NULL) return nil;
    object* more = cdr(args);
    while (more != NULL) {
        object* result = eval(car(args),env);
        if (tstflag(RETURNFLAG)) return result;
        args = more;
        more = cdr(args);
    }
    return car(args);
}

/*
    (if test then [else])
    Evaluates test. If it's non-nil the form then is evaluated and returned;
    otherwise the form else is evaluated and returned.
*/
object* tf_if (object* args, object* env) {
    if (args == NULL || cdr(args) == NULL) error2(toofewargs);
    if (eval(first(args), env) != nil) return second(args);
    args = cddr(args);
    return (args != NULL) ? first(args) : nil;
}

/*
    (cond ((test form*) (test form*) ... ))
    Each argument is a list consisting of a test optionally followed by one or more forms.
    If the test evaluates to non-nil the forms are evaluated, and the last value is returned as the result of the cond.
    If the test evaluates to nil, none of the forms are evaluated, and the next argument is processed in the same way.
*/
object* tf_cond (object* args, object* env) {
    while (args != NULL) {
        object* clause = first(args);
        if (!consp(clause)) error(illegalclause, clause);
        object* test = eval(first(clause), env);
        object* forms = cdr(clause);
        if (test != nil) {
            if (forms == NULL) return quoteit(QUOTE, test); else return tf_progn(forms, env);
        }
        args = cdr(args);
    }
    return nil;
}

/*
    (when test form*)
    Evaluates the test. If it's non-nil the forms are evaluated and the last value is returned.
*/
object* tf_when (object* args, object* env) {
    if (args == NULL) error2(noargument);
    if (eval(first(args), env) != nil) return tf_progn(cdr(args),env);
    else return nil;
}

/*
    (unless test form*)
    Evaluates the test. If it's nil the forms are evaluated and the last value is returned.
*/
object* tf_unless (object* args, object* env) {
    if (args == NULL) error2(noargument);
    if (eval(first(args), env) != nil) return nil;
    else return tf_progn(cdr(args),env);
}

/*
    (case keyform ((key form*) (key form*) ... ))
    Evaluates a keyform to produce a test key, and then tests this against a series of arguments,
    each of which is a list containing a key optionally followed by one or more forms.
*/
object* tf_case (object* args, object* env) {
    object* test = eval(first(args), env);
    args = cdr(args);
    while (args != NULL) {
        object* clause = first(args);
        if (!consp(clause)) error(illegalclause, clause);
        object* key = car(clause);
        object* forms = cdr(clause);
        if (consp(key)) {
            while (key != NULL) {
                if (eq(test,car(key))) return tf_progn(forms, env);
                key = cdr(key);
            }
        } else if (eq(test,key) || eq(key,tee)) return tf_progn(forms, env);
        args = cdr(args);
    }
    return nil;
}

/*
    (and item*)
    Evaluates its arguments until one returns nil, and returns the last value.
*/
object* tf_and (object* args, object* env) {
    if (args == NULL) return tee;
    object* more = cdr(args);
    while (more != NULL) {
        if (eval(car(args), env) == NULL) return nil;
        args = more;
        more = cdr(args);
    }
    return car(args);
}

// Core functions

/*
    (not item)
    Returns t if its argument is nil, or nil otherwise. Equivalent to null.
*/
object* fn_not (object* args, object* env) {
    (void) env;
    return (first(args) == nil) ? tee : nil;
}

/*
    (cons item item)
    If the second argument is a list, cons returns a new list with item added to the front of the list.
    If the second argument isn't a list cons returns a dotted pair.
*/
object* fn_cons (object* args, object* env) {
    (void) env;
    return cons(first(args), second(args));
}

/*
    (atom item)
    Returns t if its argument is a single number, symbol, or nil.
*/
object* fn_atom (object* args, object* env) {
    (void) env;
    return atom(first(args)) ? tee : nil;
}

/*
    (listp item)
    Returns t if its argument is a list.
*/
object* fn_listp (object* args, object* env) {
    (void) env;
    return listp(first(args)) ? tee : nil;
}

/*
    (consp item)
    Returns t if its argument is a non-null list.
*/
object* fn_consp (object* args, object* env) {
    (void) env;
    return consp(first(args)) ? tee : nil;
}

/*
    (symbolp item)
    Returns t if its argument is a symbol.
*/
object* fn_symbolp (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    return (arg == NULL || symbolp(arg)) ? tee : nil;
}

/*
    (arrayp item)
    Returns t if its argument is an array.
*/
object* fn_arrayp (object* args, object* env) {
    (void) env;
    return arrayp(first(args)) ? tee : nil;
}

/*
    (boundp item)
    Returns t if its argument is a symbol with a value.
*/
object* fn_boundp (object* args, object* env) {
    return boundp(first(args), env) ? tee : nil;
}

/*
    (keywordp item)
    Returns t if its argument is a keyword.
*/
object* fn_keywordp (object* args, object* env) {
    (void) env;
    return keywordp(first(args)) ? tee : nil;
}

/*
    (set symbol value [symbol value]*)
    For each pair of arguments, assigns the value of the second argument to the value of the first argument.
*/
object* fn_setfn (object* args, object* env) {
    object* arg = nil;
    while (args != NULL) {
        if (cdr(args) == NULL) error2(oddargs);
        object* pair = findvalue(first(args), env);
        arg = second(args);
        cdr(pair) = arg;
        args = cddr(args);
    }
    return arg;
}

/*
    (streamp item)
    Returns t if its argument is a stream.
*/
object* fn_streamp (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    return streamp(arg) ? tee : nil;
}

/*
    (eq item item)
    Tests whether the two arguments are the same symbol, same character, equal numbers,
    or point to the same cons, and returns t or nil as appropriate.
*/
object* fn_eq (object* args, object* env) {
    (void) env;
    return eq(first(args), second(args)) ? tee : nil;
}

/*
    (equal item item)
    Tests whether the two arguments are the same symbol, same character, equal numbers,
    or point to the same cons, and returns t or nil as appropriate.
*/
object* fn_equal (object* args, object* env) {
    (void) env;
    return equal(first(args), second(args)) ? tee : nil;
}

// List functions

/*
    (car list)
    Returns the first item in a list. 
*/
object* fn_car (object* args, object* env) {
    (void) env;
    return carx(first(args));
}

/*
    (cdr list)
    Returns a list with the first item removed.
*/
object* fn_cdr (object* args, object* env) {
    (void) env;
    return cdrx(first(args));
}

/*
    (caar list)
*/
object* fn_caar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b100);
}

/*
    (cadr list)
*/
object* fn_cadr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b101);
}

/*
    (cdar list)
    Equivalent to (cdr (car list)).
*/
object* fn_cdar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b110);
}

/*
    (cddr list)
    Equivalent to (cdr (cdr list)).
*/
object* fn_cddr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b111);
}

/*
    (caaar list)
    Equivalent to (car (car (car list))). 
*/
object* fn_caaar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1000);
}

/*
    (caadr list)
    Equivalent to (car (car (cdar list))).
*/
object* fn_caadr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1001);;
}

/*
    (cadar list)
    Equivalent to (car (cdr (car list))).
*/
object* fn_cadar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1010);
}

/*
    (caddr list)
    Equivalent to (car (cdr (cdr list))).
*/
object* fn_caddr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1011);
}

/*
    (cdaar list)
    Equivalent to (cdar (car (car list))).
*/
object* fn_cdaar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1100);
}

/*
    (cdadr list)
    Equivalent to (cdr (car (cdr list))).
*/
object* fn_cdadr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1101);
}

/*
    (cddar list)
    Equivalent to (cdr (cdr (car list))).
*/
object* fn_cddar (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1110);
}

/*
    (cdddr list)
    Equivalent to (cdr (cdr (cdr list))).
*/
object* fn_cdddr (object* args, object* env) {
    (void) env;
    return cxxxr(args, 0b1111);
}

/*
    (length item)
    Returns the number of items in a list, the length of a string, or the length of a one-dimensional array.
*/
object* fn_length (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (listp(arg)) return number(listlength(arg));
    if (stringp(arg)) return number(stringlength(arg));
    if (!(arrayp(arg) && cdr(cddr(arg)) == NULL)) error(PSTR("argument is not a list, 1d array, or string"), arg);
    return number(abs(first(cddr(arg))->integer));
}

/*
    (array-dimensions item)
    Returns a list of the dimensions of an array.
*/
object* fn_arraydimensions (object* args, object* env) {
    (void) env;
    object* array = first(args);
    if (!arrayp(array)) error(PSTR("argument is not an array"), array);
    object* dimensions = cddr(array);
    return (first(dimensions)->integer < 0) ? cons(number(-(first(dimensions)->integer)), cdr(dimensions)) : dimensions;
}

/*
    (list item*)
    Returns a list of the values of its arguments.
*/
object* fn_list (object* args, object* env) {
    (void) env;
    return args;
}

/*
    (make-array size [:initial-element element] [:element-type 'bit])
    If size is an integer it creates a one-dimensional array with elements from 0 to size-1.
    If size is a list of n integers it creates an n-dimensional array with those dimensions.
    If :element-type 'bit is specified the array is a bit array.
*/
object* fn_makearray (object* args, object* env) {
    (void) env;
    object* def = nil;
    bool bitp = false;
    object* dims = first(args);
    if (dims == NULL) error2(PSTR("dimensions can't be nil"));
    else if (atom(dims)) dims = cons(dims, NULL);
    args = cdr(args);
    while (args != NULL && cdr(args) != NULL) {
        object* var = first(args);
        if (isbuiltin(first(args), INITIALELEMENT)) def = second(args);
        else if (isbuiltin(first(args), ELEMENTTYPE) && isbuiltin(second(args), BIT)) bitp = true;
        else error(PSTR("argument not recognised"), var);
        args = cddr(args);
    }
    if (bitp) {
        if (def == nil) def = number(0);
        else def = number(-checkbitvalue(def)); // 1 becomes all ones
    }
    return makearray(dims, def, bitp);
}

/*
    (reverse list)
    Returns a list with the elements of list in reverse order.
*/
object* fn_reverse (object* args, object* env) {
    (void) env;
    object* list = first(args);
    object* result = NULL;
    while (list != NULL) {
        if (improperp(list)) error(notproper, list);
        push(first(list),result);
        list = cdr(list);
    }
    return result;
}

/*
    (nth number list)
    Returns the nth item in list, counting from zero.
*/
object* fn_nth (object* args, object* env) {
    (void) env;
    int n = checkinteger(first(args));
    if (n < 0) error(indexnegative, first(args));
    object* list = second(args);
    while (list != NULL) {
        if (improperp(list)) error(notproper, list);
        if (n == 0) return car(list);
        list = cdr(list);
        n--;
    }
    return nil;
}

/*
    (aref array index [index*])
    Returns an element from the specified array.
*/
object* fn_aref (object* args, object* env) {
    (void) env;
    int bit;
    object* array = first(args);
    if (!arrayp(array)) error(PSTR("first argument is not an array"), array);
    object* loc = *getarray(array, cdr(args), 0, &bit);
    if (bit == -1) return loc;
    else return number((loc->integer)>>bit & 1);
}

/*
    (assoc key list)
    Looks up a key in an association list of (key . value) pairs,
    and returns the matching pair, or nil if no pair is found.
*/
object* fn_assoc (object* args, object* env) {
    (void) env;
    object* key = first(args);
    object* list = second(args);
    return assoc(key,list);
}

/*
    (member item list)
    Searches for an item in a list, using eq, and returns the list starting from the first occurrence of the item,
    or nil if it is not found.
*/
object* fn_member (object* args, object* env) {
    (void) env;
    object* item = first(args);
    object* list = second(args);
    while (list != NULL) {
        if (improperp(list)) error(notproper, list);
        if (eq(item,car(list))) return list;
        list = cdr(list);
    }
    return nil;
}

/*
    (apply function list)
    Returns the result of evaluating function, with the list of arguments specified by the second parameter.
*/
object* fn_apply (object* args, object* env) {
    object* previous = NULL;
    object* last = args;
    while (cdr(last) != NULL) {
        previous = last;
        last = cdr(last);
    }
    object* arg = car(last);
    if (!listp(arg)) error(notalist, arg);
    cdr(previous) = arg;
    return apply(first(args), cdr(args), env);
}

/*
    (funcall function argument*)
    Evaluates function with the specified arguments.
*/
object* fn_funcall (object* args, object* env) {
    return apply(first(args), cdr(args), env);
}

/*
    (append list*)
    Joins its arguments, which should be lists, into a single list.
*/
object* fn_append (object* args, object* env) {
    (void) env;
    object* head = NULL;
    object* tail;
    while (args != NULL) {
        object* list = first(args);
        if (!listp(list)) error(notalist, list);
        while (consp(list)) {
            object* obj = cons(car(list), cdr(list));
            if (head == NULL) head = obj;
            else cdr(tail) = obj;
            tail = obj;
            list = cdr(list);
            if (cdr(args) != NULL && improperp(list)) error(notproper, first(args));
        }
        args = cdr(args);
    }
    return head;
}

/*
    (mapc function list1 [list]*)
    Applies the function to each element in one or more lists, ignoring the results.
    It returns the first list argument.
*/
object* fn_mapc (object* args, object* env) {
    object* function = first(args);
    args = cdr(args);
    object* result = first(args);
    push(result,GCStack);
    object* params = cons(NULL, NULL);
    push(params,GCStack);
    // Make parameters
    while (true) {
        object* tailp = params;
        object* lists = args;
        while (lists != NULL) {
            object* list = car(lists);
            if (list == NULL) {
                 pop(GCStack); pop(GCStack);
                 return result;
            }
            if (improperp(list)) error(notproper, list);
            object* obj = cons(first(list),NULL);
            car(lists) = cdr(list);
            cdr(tailp) = obj; tailp = obj;
            lists = cdr(lists);
        }
        apply(function, cdr(params), env);
    }
}

/*
    (mapcar function list1 [list]*)
    Applies the function to each element in one or more lists, and returns the resulting list.
*/
object* fn_mapcar (object* args, object* env) {
    return mapcarcan(args, env, mapcarfun);
}

/*
    (mapcan function list1 [list]*)
    Applies the function to each element in one or more lists. The results should be lists,
    and these are appended together to give the value returned.
*/
object* fn_mapcan (object* args, object* env) {
    return mapcarcan(args, env, mapcanfun);
}

// Arithmetic functions

/*
    (+ number*)
    Adds its arguments together.
    If each argument is an integer, and the running total doesn't overflow, the result is an integer,
    otherwise a floating-point number.
*/
object* fn_add (object* args, object* env) {
    (void) env;
    int result = 0;
    while (args != NULL) {
        object* arg = car(args);
        if (floatp(arg)) return add_floats(args, (float)result);
        else if (integerp(arg)) {
            int val = arg->integer;
            if (val < 1) { if (INT_MIN - val > result) return add_floats(args, (float)result); }
            else { if (INT_MAX - val < result) return add_floats(args, (float)result); }
            result = result + val;
        } else error(notanumber, arg);
        args = cdr(args);
    }
    return number(result);
}

/*
    (- number*)
    If there is one argument, negates the argument.
    If there are two or more arguments, subtracts the second and subsequent arguments from the first argument.
    If each argument is an integer, and the running total doesn't overflow, returns the result as an integer,
    otherwise a floating-point number.
*/
object* fn_subtract (object* args, object* env) {
    (void) env;
    object* arg = car(args);
    args = cdr(args);
    if (args == NULL) return negate(arg);
    else if (floatp(arg)) return subtract_floats(args, arg->single_float);
    else if (integerp(arg)) {
        int result = arg->integer;
        while (args != NULL) {
            arg = car(args);
            if (floatp(arg)) return subtract_floats(args, result);
            else if (integerp(arg)) {
                int val = (car(args))->integer;
                if (val < 1) { if (INT_MAX + val < result) return subtract_floats(args, result); }
                else { if (INT_MIN + val > result) return subtract_floats(args, result); }
                result = result - val;
            } else error(notanumber, arg);
            args = cdr(args);
        }
        return number(result);
    } else error(notanumber, arg);
    return nil;
}

/*
    (* number*)
    Multiplies its arguments together.
    If each argument is an integer, and the running total doesn't overflow, the result is an integer,
    otherwise it's a floating-point number.
*/
object* fn_multiply (object* args, object* env) {
    (void) env;
    int result = 1;
    while (args != NULL){
        object* arg = car(args);
        if (floatp(arg)) return multiply_floats(args, result);
        else if (integerp(arg)) {
            int64_t val = result * (int64_t)(arg->integer);
            if ((val > INT_MAX) || (val < INT_MIN)) return multiply_floats(args, result);
            result = val;
        } else error(notanumber, arg);
        args = cdr(args);
    }
    return number(result);
}

/*
    (/ number*)
    Divides the first argument by the second and subsequent arguments.
    If each argument is an integer, and each division produces an exact result, the result is an integer;
    otherwise it's a floating-point number.
*/
object* fn_divide (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    args = cdr(args);
    // One argument
    if (args == NULL) {
        if (floatp(arg)) {
            float f = arg->single_float;
            if (f == 0.0) error2(PSTR("division by zero"));
            return makefloat(1.0 / f);
        } else if (integerp(arg)) {
            int i = arg->integer;
            if (i == 0) error2(PSTR("division by zero"));
            else if (i == 1) return number(1);
            else return makefloat(1.0 / i);
        } else error(notanumber, arg);
    }
    // Multiple arguments
    if (floatp(arg)) return divide_floats(args, arg->single_float);
    else if (integerp(arg)) {
        int result = arg->integer;
        while (args != NULL) {
            arg = car(args);
            if (floatp(arg)) {
                return divide_floats(args, result);
            } else if (integerp(arg)) {
                int i = arg->integer;
                if (i == 0) error2(PSTR("division by zero"));
                if ((result % i) != 0) return divide_floats(args, result);
                if ((result == INT_MIN) && (i == -1)) return divide_floats(args, result);
                result = result / i;
                args = cdr(args);
            } else error(notanumber, arg);
        }
        return number(result);
    } else error(notanumber, arg);
    return nil;
}

/*
    (mod number number)
    Returns its first argument modulo the second argument.
    If both arguments are integers the result is an integer; otherwise it's a floating-point number.
*/
object* fn_mod (object* args, object* env) {
    (void) env;
    object* arg1 = first(args);
    object* arg2 = second(args);
    if (integerp(arg1) && integerp(arg2)) {
        int divisor = arg2->integer;
        if (divisor == 0) error2(PSTR("division by zero"));
        int dividend = arg1->integer;
        int remainder = dividend % divisor;
        if ((dividend<0) != (divisor<0)) remainder = remainder + divisor;
        return number(remainder);
    } else {
        float fdivisor = checkintfloat(arg2);
        if (fdivisor == 0.0) error2(PSTR("division by zero"));
        float fdividend = checkintfloat(arg1);
        float fremainder = fmod(fdividend , fdivisor);
        if ((fdividend<0) != (fdivisor<0)) fremainder = fremainder + fdivisor;
        return makefloat(fremainder);
    }
}

/*
    (1+ number)
    Adds one to its argument and returns it.
    If the argument is an integer the result is an integer if possible;
    otherwise it's a floating-point number.
*/
object* fn_oneplus (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return makefloat((arg->single_float) + 1.0);
    else if (integerp(arg)) {
        int result = arg->integer;
        if (result == INT_MAX) return makefloat((arg->integer) + 1.0);
        else return number(result + 1);
    } else error(notanumber, arg);
    return nil;
}

/*
    (1- number)
    Subtracts one from its argument and returns it.
    If the argument is an integer the result is an integer if possible;
    otherwise it's a floating-point number.
*/
object* fn_oneminus (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return makefloat((arg->single_float) - 1.0);
    else if (integerp(arg)) {
        int result = arg->integer;
        if (result == INT_MIN) return makefloat((arg->integer) - 1.0);
        else return number(result - 1);
    } else error(notanumber, arg);
    return nil;
}

/*
    (abs number)
    Returns the absolute, positive value of its argument.
    If the argument is an integer the result will be returned as an integer if possible,
    otherwise a floating-point number.
*/
object* fn_abs (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return makefloat(abs(arg->single_float));
    else if (integerp(arg)) {
        int result = arg->integer;
        if (result == INT_MIN) return makefloat(abs((float)result));
        else return number(abs(result));
    } else error(notanumber, arg);
    return nil;
}

/*
    (random number)
    If number is an integer returns a random number between 0 and one less than its argument.
    Otherwise returns a floating-point number between zero and number.
*/
object* fn_random (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (integerp(arg)) return number(random(arg->integer));
    else if (floatp(arg)) return makefloat((float)rand()/(float)(RAND_MAX/(arg->single_float)));
    else error(notanumber, arg);
    return nil;
}

/*
    (max number*)
    Returns the maximum of one or more arguments.
*/
object* fn_maxfn (object* args, object* env) {
    (void) env;
    object* result = first(args);
    args = cdr(args);
    while (args != NULL) {
        object* arg = car(args);
        if (integerp(result) && integerp(arg)) {
            if ((arg->integer) > (result->integer)) result = arg;
        } else if ((checkintfloat(arg) > checkintfloat(result))) result = arg;
        args = cdr(args);
    }
    return result;
}

/*
    (min number*)
    Returns the minimum of one or more arguments.
*/
object* fn_minfn (object* args, object* env) {
    (void) env;
    object* result = first(args);
    args = cdr(args);
    while (args != NULL) {
        object* arg = car(args);
        if (integerp(result) && integerp(arg)) {
            if ((arg->integer) < (result->integer)) result = arg;
        } else if ((checkintfloat(arg) < checkintfloat(result))) result = arg;
        args = cdr(args);
    }
    return result;
}

// Arithmetic comparisons

/*
    (/= number*)
    Returns t if none of the arguments are equal, or nil if two or more arguments are equal.
*/
object* fn_noteq (object* args, object* env) {
    (void) env;
    while (args != NULL) {
        object* nargs = args;
        object* arg1 = first(nargs);
        nargs = cdr(nargs);
        while (nargs != NULL) {
            object* arg2 = first(nargs);
            if (integerp(arg1) && integerp(arg2)) {
                if ((arg1->integer) == (arg2->integer)) return nil;
            } else if ((checkintfloat(arg1) == checkintfloat(arg2))) return nil;
            nargs = cdr(nargs);
        }
        args = cdr(args);
    }
    return tee;
}

/*
    (= number*)
    Returns t if all the arguments, which must be numbers, are numerically equal, and nil otherwise.
*/
object* fn_numeq (object* args, object* env) {
    (void) env;
    return compare(args, false, false, true);
}

/*
    (< number*)
    Returns t if each argument is less than the next argument, and nil otherwise.
*/
object* fn_less (object* args, object* env) {
    (void) env;
    return compare(args, true, false, false);
}

/*
    (<= number*)
    Returns t if each argument is less than or equal to the next argument, and nil otherwise.
*/
object* fn_lesseq (object* args, object* env) {
    (void) env;
    return compare(args, true, false, true);
}

/*
    (> number*)
    Returns t if each argument is greater than the next argument, and nil otherwise.
*/
object* fn_greater (object* args, object* env) {
    (void) env;
    return compare(args, false, true, false);
}

/*
    (>= number*)
    Returns t if each argument is greater than or equal to the next argument, and nil otherwise.
*/
object* fn_greatereq (object* args, object* env) {
    (void) env;
    return compare(args, false, true, true);
}

/*
    (plusp number)
    Returns t if the argument is greater than zero, or nil otherwise.
*/
object* fn_plusp (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return ((arg->single_float) > 0.0) ? tee : nil;
    else if (integerp(arg)) return ((arg->integer) > 0) ? tee : nil;
    else error(notanumber, arg);
    return nil;
}

/*
    (minusp number)
    Returns t if the argument is less than zero, or nil otherwise.
*/
object* fn_minusp (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return ((arg->single_float) < 0.0) ? tee : nil;
    else if (integerp(arg)) return ((arg->integer) < 0) ? tee : nil;
    else error(notanumber, arg);
    return nil;
}

/*
    (zerop number)
    Returns t if the argument is zero.
*/
object* fn_zerop (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (floatp(arg)) return ((arg->single_float) == 0.0) ? tee : nil;
    else if (integerp(arg)) return ((arg->integer) == 0) ? tee : nil;
    else error(notanumber, arg);
    return nil;
}

/*
    (oddp number)
    Returns t if the integer argument is odd.
*/
object* fn_oddp (object* args, object* env) {
    (void) env;
    int arg = checkinteger(first(args));
    return ((arg & 1) == 1) ? tee : nil;
}

/*
    (evenp number)
    Returns t if the integer argument is even.
*/
object* fn_evenp (object* args, object* env) {
    (void) env;
    int arg = checkinteger(first(args));
    return ((arg & 1) == 0) ? tee : nil;
}

// Number functions

/*
    (integerp number)
    Returns t if the argument is an integer.
*/
object* fn_integerp (object* args, object* env) {
    (void) env;
    return integerp(first(args)) ? tee : nil;
}

/*
    (numberp number)
    Returns t if the argument is a number.
*/
object* fn_numberp (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    return (integerp(arg) || floatp(arg)) ? tee : nil;
}

// Floating-point functions

/*
    (float number)
    Returns its argument converted to a floating-point number.
*/
object* fn_floatfn (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    return (floatp(arg)) ? arg : makefloat((float)(arg->integer));
}

/*
    (floatp number)
    Returns t if the argument is a floating-point number.
*/
object* fn_floatp (object* args, object* env) {
    (void) env;
    return floatp(first(args)) ? tee : nil;
}

/*
    (sin number)
    Returns sin(number).
*/
object* fn_sin (object* args, object* env) {
    (void) env;
    return makefloat(sin(checkintfloat(first(args))));
}

/*
    (cos number)
    Returns cos(number).
*/
object* fn_cos (object* args, object* env) {
    (void) env;
    return makefloat(cos(checkintfloat(first(args))));
}

/*
    (tan number)
    Returns tan(number).
*/
object* fn_tan (object* args, object* env) {
    (void) env;
    return makefloat(tan(checkintfloat(first(args))));
}

/*
    (asin number)
    Returns asin(number).
*/
object* fn_asin (object* args, object* env) {
    (void) env;
    return makefloat(asin(checkintfloat(first(args))));
}

/*
    (acos number)
    Returns acos(number).
*/
object* fn_acos (object* args, object* env) {
    (void) env;
    return makefloat(acos(checkintfloat(first(args))));
}

/*
    (atan number1 [number2])
    Returns the arc tangent of number1/number2, in radians. If number2 is omitted it defaults to 1.
*/
object* fn_atan (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    float div = 1.0;
    args = cdr(args);
    if (args != NULL) div = checkintfloat(first(args));
    return makefloat(atan2(checkintfloat(arg), div));
}

/*
    (sinh number)
    Returns sinh(number).
*/
object* fn_sinh (object* args, object* env) {
    (void) env;
    return makefloat(sinh(checkintfloat(first(args))));
}

/*
    (cosh number)
    Returns cosh(number).
*/
object* fn_cosh (object* args, object* env) {
    (void) env;
    return makefloat(cosh(checkintfloat(first(args))));
}

/*
    (tanh number)
    Returns tanh(number).
*/
object* fn_tanh (object* args, object* env) {
    (void) env;
    return makefloat(tanh(checkintfloat(first(args))));
}

/*
    (exp number)
    Returns exp(number).
*/
object* fn_exp (object* args, object* env) {
    (void) env;
    return makefloat(exp(checkintfloat(first(args))));
}

/*
    (sqrt number)
    Returns sqrt(number).
*/
object* fn_sqrt (object* args, object* env) {
    (void) env;
    return makefloat(sqrt(checkintfloat(first(args))));
}

/*
    (log number [base])
    Returns the logarithm of number to the specified base. If base is omitted it defaults to e.
*/
object* fn_log (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    float fresult = log(checkintfloat(arg));
    args = cdr(args);
    if (args == NULL) return makefloat(fresult);
    else return makefloat(fresult / log(checkintfloat(first(args))));
}

/*
    (expt number power)
    Returns number raised to the specified power.
    Returns the result as an integer if the arguments are integers and the result will be within range,
    otherwise a floating-point number.
*/
object* fn_expt (object* args, object* env) {
    (void) env;
    object* arg1 = first(args); object* arg2 = second(args);
    float float1 = checkintfloat(arg1);
    float value = log(abs(float1)) * checkintfloat(arg2);
    if (integerp(arg1) && integerp(arg2) && ((arg2->integer) >= 0) && (abs(value) < 21.4875))
        return number(intpower(arg1->integer, arg2->integer));
    if (float1 < 0) {
        if (integerp(arg2)) return makefloat((arg2->integer & 1) ? -exp(value) : exp(value));
        else error2(PSTR("imaginary result"));
    }
    return makefloat(exp(value));
}

/*
    (ceiling number [divisor])
    Returns ceil(number/divisor). If omitted, divisor is 1.
*/
object* fn_ceiling (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    args = cdr(args);
    if (args != NULL) return number(ceil(checkintfloat(arg) / checkintfloat(first(args))));
    else return number(ceil(checkintfloat(arg)));
}

/*
    (floor number [divisor])
    Returns floor(number/divisor). If omitted, divisor is 1.
*/
object* fn_floor (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    args = cdr(args);
    if (args != NULL) return number(floor(checkintfloat(arg) / checkintfloat(first(args))));
    else return number(floor(checkintfloat(arg)));
}

/*
    (truncate number [divisor])
    Returns the integer part of number/divisor. If divisor is omitted it defaults to 1.
*/
object* fn_truncate (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    args = cdr(args);
    if (args != NULL) return number((int)(checkintfloat(arg) / checkintfloat(first(args))));
    else return number((int)(checkintfloat(arg)));
}

/*
    (round number [divisor])
    Returns the integer closest to number/divisor. If divisor is omitted it defaults to 1.
*/
object* fn_round (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    args = cdr(args);
    if (args != NULL) return number(myround(checkintfloat(arg) / checkintfloat(first(args))));
    else return number(myround(checkintfloat(arg)));
}

// Characters

/*
    (char string n)
    Returns the nth character in a string, counting from zero.
*/
object* fn_char (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (!stringp(arg)) error(notastring, arg);
    object* n = second(args);
    char c = nthchar(arg, checkinteger(n));
    if (c == 0) error(indexrange, n);
    return character(c);
}

/*
    (char-code character)
    Returns the ASCII code for a character, as an integer.
*/
object* fn_charcode (object* args, object* env) {
    (void) env;
    return number(checkchar(first(args)));
}

/*
    (code-char integer)
    Returns the character for the specified ASCII code.
*/
object* fn_codechar (object* args, object* env) {
    (void) env;
    return character(checkinteger(first(args)));
}

/*
    (characterp item)
    Returns t if the argument is a character and nil otherwise.
*/
object* fn_characterp (object* args, object* env) {
    (void) env;
    return characterp(first(args)) ? tee : nil;
}

// Strings

/*
    (stringp item)
    Returns t if the argument is a string and nil otherwise.
*/
object* fn_stringp (object* args, object* env) {
    (void) env;
    return stringp(first(args)) ? tee : nil;
}

/*
    (string= string string)
    Tests whether two strings are the same.
*/
object* fn_stringeq (object* args, object* env) {
    (void) env;
    return stringcompare(args, false, false, true) ? tee : nil;
}

/*
    (string< string string)
    Returns t if the first string is alphabetically less than the second string, and nil otherwise.
*/
object* fn_stringless (object* args, object* env) {
    (void) env;
    return stringcompare(args, true, false, false) ? tee : nil;
}

/*
    (string> string string)
    Returns t if the first string is alphabetically greater than the second string, and nil otherwise.
*/
object* fn_stringgreater (object* args, object* env) {
    (void) env;
    return stringcompare(args, false, true, false) ? tee : nil;
}

/*
    (sort list test)
    Destructively sorts list according to the test function, using an insertion sort, and returns the sorted list.
*/
object* fn_sort (object* args, object* env) {
    if (first(args) == NULL) return nil;
    object* list = cons(nil,first(args));
    push(list,GCStack);
    object* predicate = second(args);
    object* compare = cons(NULL, cons(NULL, NULL));
    push(compare,GCStack);
    object* ptr = cdr(list);
    while (cdr(ptr) != NULL) {
        object* go = list;
        while (go != ptr) {
            car(compare) = car(cdr(ptr));
            car(cdr(compare)) = car(cdr(go));
            if (apply(predicate, compare, env)) break;
            go = cdr(go);
        }
        if (go != ptr) {
            object* obj = cdr(ptr);
            cdr(ptr) = cdr(obj);
            cdr(obj) = cdr(go);
            cdr(go) = obj;
        } else ptr = cdr(ptr);
    }
    pop(GCStack); pop(GCStack);
    return cdr(list);
}

/*
    (string item)
    Converts its argument to a string.
*/
object* fn_stringfn (object* args, object* env) {
    return fn_princtostring(args, env);
}

/*
    (concatenate 'string string*)
    Joins together the strings given in the second and subsequent arguments, and returns a single string.
*/
object* fn_concatenate (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    if (builtin(arg->name) != STRINGFN) error2(PSTR("only supports strings"));
    args = cdr(args);
    object* result = newstring();
    object* tail = result;
    while (args != NULL) {
        object* obj = checkstring(first(args));
        obj = cdr(obj);
        while (obj != NULL) {
            int quad = obj->chars;
            while (quad != 0) {
                 char ch = quad>>((sizeof(int)-1)*8) & 0xFF;
                 buildstring(ch, &tail);
                 quad = quad<<8;
            }
            obj = car(obj);
        }
        args = cdr(args);
    }
    return result;
}

/*
    (subseq seq start [end])
    Returns a subsequence of a list or string from item start to item end-1.
*/
object* fn_subseq (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    int start = checkinteger(second(args)), end;
    if (start < 0) error(indexnegative, second(args));
    args = cddr(args);
    if (listp(arg)) {
        int length = listlength(arg);
        if (args != NULL) end = checkinteger(car(args)); else end = length;
        if (start > end || end > length) error2(indexrange);
        object* result = cons(NULL, NULL);
        object* ptr = result;
        for (int x = 0; x < end; x++) {
            if (x >= start) { cdr(ptr) = cons(car(arg), NULL); ptr = cdr(ptr); }
            arg = cdr(arg);
        }
        return cdr(result);
    } else if (stringp(arg)) {
        int length = stringlength(arg);
        if (args != NULL) end = checkinteger(car(args)); else end = length;
        if (start > end || end > length) error2(indexrange);
        object* result = newstring();
        object* tail = result;
        for (int i=start; i<end; i++) {
            char ch = nthchar(arg, i);
            buildstring(ch, &tail);
        }
        return result;
    } else error2(PSTR("argument is not a list or string"));
    return nil;
}

/*
    (search pattern target)
    Returns the index of the first occurrence of pattern in target, 
    which can be lists or strings, or nil if it's not found.
*/
object* fn_search (object* args, object* env) {
    (void) env;
    object* pattern = first(args);
    object* target = second(args);
    if (pattern == NULL) return number(0);
    else if (target == NULL) return nil;
    else if (listp(pattern) && listp(target)) {
        int l = listlength(target);
        int m = listlength(pattern);
        for (int i = 0; i <= l-m; i++) {
            object* target1 = target;
            while (pattern != NULL && eq(car(target1), car(pattern))) {
                pattern = cdr(pattern);
                target1 = cdr(target1);
            }
            if (pattern == NULL) return number(i);
            pattern = first(args); target = cdr(target);
        }
        return nil;
    } else if (stringp(pattern) && stringp(target)) {
        int l = stringlength(target);
        int m = stringlength(pattern);
        for (int i = 0; i <= l-m; i++) {
            int j = 0;
            while (j < m && nthchar(target, i+j) == nthchar(pattern, j)) j++;
            if (j == m) return number(i);
        }
        return nil;
    } else error2(PSTR("arguments are not both lists or strings"));
    return nil;
}

/*
    (read-from-string string)
    Reads an atom or list from the specified string and returns it.
*/
object* fn_readfromstring (object* args, object* env) {
    (void) env;
    object* arg = checkstring(first(args));
    GlobalString = arg;
    GlobalStringIndex = 0;
    object* val = read(gstr);
    LastChar = 0;
    return val;
}

/*
    (princ-to-string item)
    Prints its argument to a string, and returns the string.
    Characters and strings are printed without quotation marks or escape characters.
*/
object* fn_princtostring (object* args, object* env) {
    (void) env;
    return princtostring(first(args));
}

/*
    (prin1-to-string item [stream])
    Prints its argument to a string, and returns the string.
    Characters and strings are printed with quotation marks and escape characters,
    in a format that will be suitable for read-from-string.
*/
object* fn_prin1tostring (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    object* obj = startstring();
    printobject(arg, pstr);
    return obj;
}

// Bitwise operators

/*
    (logand [value*])
    Returns the bitwise & of the values.
*/
object* fn_logand (object* args, object* env) {
    (void) env;
    int result = -1;
    while (args != NULL) {
        result = result & checkinteger(first(args));
        args = cdr(args);
    }
    return number(result);
}

/*
    (logior [value*])
    Returns the bitwise | of the values.
*/
object* fn_logior (object* args, object* env) {
    (void) env;
    int result = 0;
    while (args != NULL) {
        result = result | checkinteger(first(args));
        args = cdr(args);
    }
    return number(result);
}

/*
    (logxor [value*])
    Returns the bitwise ^ of the values.
*/
object* fn_logxor (object* args, object* env) {
    (void) env;
    int result = 0;
    while (args != NULL) {
        result = result ^ checkinteger(first(args));
        args = cdr(args);
    }
    return number(result);
}

/*
    (lognot number)
    Returns the bitwise inverse of the number.
*/
object* fn_lognot (object* args, object* env) {
    (void) env;
    int result = checkinteger(car(args));
    return number(~result);
}

/*
    (ash value shift)
    Returns the result of bitwise shifting value by shift bits. If shift is positive, value is shifted to the left.
*/
object* fn_ash (object* args, object* env) {
    (void) env;
    int value = checkinteger(first(args));
    int count = checkinteger(second(args));
    if (count >= 0) return number(value << count);
    else return number(value >> abs(count));
}

/*
    (logbitp bit value)
    Returns t if bit number bit in value is a '1', and nil if it is a '0'.
*/
object* fn_logbitp (object* args, object* env) {
    (void) env;
    int index = checkinteger(first(args));
    int value = checkinteger(second(args));
    return (bitRead(value, index) == 1) ? tee : nil;
}

// System functions

/*
    (eval form*)
    Evaluates its argument an extra time.
*/
object* fn_eval (object* args, object* env) {
    return eval(first(args), env);
}

/*
    (globals)
    Returns a list of global variables.
*/
object* fn_globals (object* args, object* env) {
    (void) args, (void) env;
    object* result = cons(NULL, NULL);
    object* ptr = result;
    object* arg = GlobalEnv;
    while (arg != NULL) {
        cdr(ptr) = cons(car(car(arg)), NULL); ptr = cdr(ptr);
        arg = cdr(arg);
    }
    return cdr(result);
}

/*
    (locals)
    Returns an association list of local variables and their values.
*/
object* fn_locals (object* args, object* env) {
    (void) args;
    return env;
}

/*
    (makunbound symbol)
    Removes the value of the symbol from GlobalEnv and returns the symbol.
*/
object* fn_makunbound (object* args, object* env) {
    (void) env;
    object* var = first(args);
    if (!symbolp(var)) error(notasymbol, var);
    delassoc(var, &GlobalEnv);
    return var;
}

/*
    (break)
    Inserts a breakpoint in the program. When evaluated prints Break! and reenters the REPL.
*/
object* fn_break (object* args, object* env) {
    (void) args;
    pfstring(PSTR("\nBreak!\n"), pserial);
    BreakLevel++;
    repl(env);
    BreakLevel--;
    return nil;
}

/*
    (read [stream])
    Reads an atom or list from the serial input and returns it.
    If stream is specified the item is read from the specified stream.
*/
object* fn_read (object* args, object* env) {
    (void) env;
    gfun_t gfun = gstreamfun(args);
    return read(gfun);
}

/*
    (prin1 item [stream]) 
    Prints its argument, and returns its value.
    Strings are printed with quotation marks and escape characters.
*/
object* fn_prin1 (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    printobject(obj, pfun);
    return obj;
}

/*
    (print item [stream])
    Prints its argument with quotation marks and escape characters, on a new line, and followed by a space.
    If stream is specified the argument is printed to the specified stream.
*/
object* fn_print (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    pln(pfun);
    printobject(obj, pfun);
    pfun(' ');
    return obj;
}

/*
    (princ item [stream]) 
    Prints its argument, and returns its value.
    Characters and strings are printed without quotation marks or escape characters.
*/
object* fn_princ (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    prin1object(obj, pfun);
    return obj;
}

/*
    (terpri [stream])
    Prints a new line, and returns nil.
    If stream is specified the new line is written to the specified stream. 
*/
object* fn_terpri (object* args, object* env) {
    (void) env;
    pfun_t pfun = pstreamfun(args);
    pln(pfun);
    return nil;
}

/*
    (read-byte stream)
    Reads a byte from a stream and returns it.
*/
object* fn_readbyte (object* args, object* env) {
    (void) env;
    gfun_t gfun = gstreamfun(args);
    int c = gfun();
    return (c == -1) ? nil : number(c);
}

/*
    (read-line [stream])
    Reads characters from the serial input up to a newline character, and returns them as a string, excluding the newline.
    If stream is specified the line is read from the specified stream.
*/
object* fn_readline (object* args, object* env) {
    (void) env;
    gfun_t gfun = gstreamfun(args);
    return readstring('\n', gfun);
}

/*
    (write-byte number [stream])
    Writes a byte to a stream.
*/
object* fn_writebyte (object* args, object* env) {
    (void) env;
    int value = checkinteger(first(args));
    pfun_t pfun = pstreamfun(cdr(args));
    (pfun)(value);
    return nil;
}

/*
    (write-string string [stream])
    Writes a string. If stream is specified the string is written to the stream.
*/
object* fn_writestring (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    char temp = Flags;
    clrflag(PRINTREADABLY);
    printstring(obj, pfun);
    Flags = temp;
    return nil;
}

/*
    (write-line string [stream])
    Writes a string terminated by a newline character. If stream is specified the string is written to the stream.
*/
object* fn_writeline (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    char temp = Flags;
    clrflag(PRINTREADABLY);
    printstring(obj, pfun);
    pln(pfun);
    Flags = temp;
    return nil;
}

/*
    (restart-i2c stream [read-p])
    Restarts an i2c-stream.
    If read-p is nil or omitted the stream is written to.
    If read-p is an integer it specifies the number of bytes to be read from the stream.
*/
object* fn_restarti2c (object* args, object* env) {
    (void) env;
    int stream = first(args)->integer;
    args = cdr(args);
    int read = 0; // Write
    I2Ccount = 0;
    if (args != NULL) {
        object* rw = first(args);
        if (integerp(rw)) I2Ccount = rw->integer;
        read = (rw != NULL);
    }
    int address = stream & 0xFF;
    if (stream>>8 != I2CSTREAM) error2(PSTR("not an i2c stream"));
    return I2Crestart(address, read) ? tee : nil;
}

/*
    (gc)
    Forces a garbage collection and prints the number of objects collected, and the time taken.
*/
object* fn_gc (object* obj, object* env) {
    int initial = Freespace;
    unsigned long start = micros();
    gc(obj, env);
    unsigned long elapsed = micros() - start;
    pfstring(PSTR("Space: "), pserial);
    pint(Freespace - initial, pserial);
    pfstring(PSTR(" bytes, Time: "), pserial);
    pint(elapsed, pserial);
    pfstring(PSTR(" us\n"), pserial);
    return nil;
}

/*
    (room)
    Returns the number of free Lisp cells remaining.
*/
object* fn_room (object* args, object* env) {
    (void) args, (void) env;
    return number(Freespace);
}

/*
    (cls)
    Prints a clear-screen character.
*/
object* fn_cls (object* args, object* env) {
    (void) args, (void) env;
    pserial(12);
    return nil;
}

// Arduino procedures

/*
    (pinmode pin mode)
    Sets the input/output mode of an Arduino pin number, and returns nil.
    The mode parameter can be an integer, a keyword, or t or nil.
*/
object* fn_pinmode (object* args, object* env) {
    (void) env; int pin;
    object* arg = first(args);
    if (keywordp(arg)) pin = checkkeyword(arg);
    else pin = checkinteger(first(args));
    int pm = INPUT;
    arg = second(args);
    if (keywordp(arg)) pm = checkkeyword(arg);
    else if (integerp(arg)) {
        int mode = arg->integer;
        if (mode == 1) pm = OUTPUT; else if (mode == 2) pm = INPUT_PULLUP;
        #if defined(INPUT_PULLDOWN)
        else if (mode == 4) pm = INPUT_PULLDOWN;
        #endif
    } else if (arg != nil) pm = OUTPUT;
    pinMode(pin, pm);
    return nil;
}

/*
    (digitalread pin)
    Reads the state of the specified Arduino pin number and returns t (high) or nil (low).
*/
object* fn_digitalread (object* args, object* env) {
    (void) env;
    int pin;
    object* arg = first(args);
    if (keywordp(arg)) pin = checkkeyword(arg);
    else pin = checkinteger(arg);
    if (digitalRead(pin) != 0) return tee; else return nil;
}

/*
    (digitalwrite pin state)
    Sets the state of the specified Arduino pin number.
*/
object* fn_digitalwrite (object* args, object* env) {
    (void) env;
    int pin;
    object* arg = first(args);
    if (keywordp(arg)) pin = checkkeyword(arg);
    else pin = checkinteger(arg);
    arg = second(args);
    int mode;
    if (keywordp(arg)) mode = checkkeyword(arg);
    else if (integerp(arg)) mode = arg->integer ? HIGH : LOW;
    else mode = (arg != nil) ? HIGH : LOW;
    digitalWrite(pin, mode);
    return arg;
}

/*
    (analogread pin)
    Reads the specified Arduino analogue pin number and returns the value.
*/
object* fn_analogread (object* args, object* env) {
    (void) env;
    int pin;
    object* arg = first(args);
    if (keywordp(arg)) pin = checkkeyword(arg);
    else {
        pin = checkinteger(arg);
        checkanalogread(pin);
    }
    return number(analogRead(pin));
}

/*
    (analogreadresolution bits)
    Specifies the resolution for the analogue inputs on platforms that support it.
    The default resolution on all platforms is 10 bits.
*/
object* fn_analogreadresolution (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    analogReadResolution(checkinteger(arg));
    return arg;
}

/*
    (analogwrite pin value)
    Writes the value to the specified Arduino pin number.
*/
object* fn_analogwrite (object* args, object* env) {
    (void) env;
    int pin;
    object* arg = first(args);
    if (keywordp(arg)) pin = checkkeyword(arg);
    else pin = checkinteger(arg);
    checkanalogwrite(pin);
    object* value = second(args);
    analogWrite(pin, checkinteger(value));
    return value;
}

/*
    (delay number)
    Delays for a specified number of milliseconds.
*/
object* fn_delay (object* args, object* env) {
    (void) env;
    object* arg1 = first(args);
    delay(checkinteger(arg1));
    return arg1;
}

/*
    (millis)
    Returns the time in milliseconds that uLisp has been running.
*/
object* fn_millis (object* args, object* env) {
    (void) args, (void) env;
    return number(millis());
}

/*
    (sleep secs)
    Puts the processor into a low-power sleep mode for secs.
    Only supported on some platforms. On other platforms it does delay(1000*secs).
*/
object* fn_sleep (object* args, object* env) {
    (void) env;
    object* arg1 = first(args);
    doze(checkinteger(arg1));
    return arg1;
}

/*
    (note [pin] [note] [octave])
    Generates a square wave on pin.
    The argument note represents the note in the well-tempered scale, from 0 to 11,  
    where 0 represents C, 1 represents C#, and so on.
    The argument octave can be from 3 to 6. If omitted it defaults to 0.
*/
object* fn_note (object* args, object* env) {
    (void) env;
    static int pin = 255;
    if (args != NULL) {
        pin = checkinteger(first(args));
        int note = 0;
        if (cddr(args) != NULL) note = checkinteger(second(args));
        int octave = 0;
        if (cddr(args) != NULL) octave = checkinteger(third(args));
        playnote(pin, note, octave);
    } else nonote(pin);
    return nil;
}

/*
    (register address [value])
    Reads or writes the value of a peripheral register.
    If value is not specified the function returns the value of the register at address.
    If value is specified the value is written to the register at address and the function returns value.
*/
object* fn_register (object* args, object* env) {
    (void) env;
    object* arg = first(args);
    int addr;
    if (keywordp(arg)) addr = checkkeyword(arg);
    else addr = checkinteger(first(args));
    if (cdr(args) == NULL) return number(*(uint32_t *)addr);
    (*(uint32_t *)addr) = checkinteger(second(args));
    return second(args);
}

// Tree Editor

/*
    (edit 'function)
    Calls the Lisp tree editor to allow you to edit a function definition.
*/
object* fn_edit (object* args, object* env) {
    object* fun = first(args);
    object* pair = findvalue(fun, env);
    clrflag(EXITEDITOR);
    object* arg = edit(eval(fun, env));
    cdr(pair) = arg;
    return arg;
}

// Pretty printer

/*
    (pprint item [str])
    Prints its argument, using the pretty printer, to display it formatted in a structured way.
    If str is specified it prints to the specified stream. It returns no value.
*/
object* fn_pprint (object* args, object* env) {
    (void) env;
    object* obj = first(args);
    pfun_t pfun = pstreamfun(cdr(args));
    #if defined(gfxsupport)
    if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
    #endif
    pln(pfun);
    superprint(obj, 0, pfun);
    ppwidth = PPWIDTH;
    return bsymbol(NOTHING);
}

/*
    (pprintall [str])
    Pretty-prints the definition of every function and variable defined in the uLisp workspace.
    If str is specified it prints to the specified stream. It returns no value.
*/
object* fn_pprintall (object* args, object* env) {
    (void) env;
    pfun_t pfun = pstreamfun(args);
    #if defined(gfxsupport)
    if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
    #endif
    object* globals = GlobalEnv;
    while (globals != NULL) {
        object* pair = first(globals);
        object* var = car(pair);
        object* val = cdr(pair);
        pln(pfun);
        if (consp(val) && symbolp(car(val)) && builtin(car(val)->name) == LAMBDA) {
            superprint(cons(bsymbol(DEFUN), cons(var, cdr(val))), 0, pfun);
        } else {
            superprint(cons(bsymbol(DEFVAR), cons(var, cons(quoteit(QUOTE, val), NULL))), 0, pfun);
        }
        pln(pfun);
        testescape();
        globals = cdr(globals);
    }
    ppwidth = PPWIDTH;
    return bsymbol(NOTHING);
}

// Format

/*
    (format output controlstring [arguments]*)
    Outputs its arguments formatted according to the format directives in controlstring.
*/
object* fn_format (object* args, object* env) {
    (void) env;
    pfun_t pfun = pserial;
    object* output = first(args);
    object* obj;
    if (output == nil) { obj = startstring(); pfun = pstr; }
    else if (output != tee) pfun = pstreamfun(args);
    object* formatstr = checkstring(second(args));
    object* save = NULL;
    args = cddr(args);
    int len = stringlength(formatstr);
    uint8_t n = 0, width = 0, w, bra = 0;
    char pad = ' ';
    bool tilde = false, mute = false, comma = false, quote = false;
    while (n < len) {
        char ch = nthchar(formatstr, n);
        char ch2 = ch & ~0x20; // force to upper case
        if (tilde) {
         if (ch == '}') {
                if (save == NULL) formaterr(formatstr, PSTR("no matching ~{"), n);
                if (args == NULL) { args = cdr(save); save = NULL; } else n = bra;
                mute = false; tilde = false;
            }
            else if (!mute) {
                if (comma && quote) { pad = ch; comma = false, quote = false; }
                else if (ch == '\'') {
                    if (comma) quote = true;
                    else formaterr(formatstr, PSTR("quote not valid"), n);
                }
                else if (ch == '~') { pfun('~'); tilde = false; }
                else if (ch >= '0' && ch <= '9') width = width*10 + ch - '0';
                else if (ch == ',') comma = true;
                else if (ch == '%') { pln(pfun); tilde = false; }
                else if (ch == '&') { pfl(pfun); tilde = false; }
                else if (ch == '^') {
                    if (save != NULL && args == NULL) mute = true;
                    tilde = false;
                }
                else if (ch == '{') {
                    if (save != NULL) formaterr(formatstr, PSTR("can't nest ~{"), n);
                    if (args == NULL) formaterr(formatstr, noargument, n);
                    if (!listp(first(args))) formaterr(formatstr, notalist, n);
                    save = args; args = first(args); bra = n; tilde = false;
                    if (args == NULL) mute = true;
                }
                else if (ch2 == 'A' || ch2 == 'S' || ch2 == 'D' || ch2 == 'G' || ch2 == 'X' || ch2 == 'B') {
                    if (args == NULL) formaterr(formatstr, noargument, n);
                    object* arg = first(args); args = cdr(args);
                    uint8_t aw = atomwidth(arg);
                    if (width < aw) w = 0; else w = width-aw;
                    tilde = false;
                    if (ch2 == 'A') { prin1object(arg, pfun); indent(w, pad, pfun); }
                    else if (ch2 == 'S') { printobject(arg, pfun); indent(w, pad, pfun); }
                    else if (ch2 == 'D' || ch2 == 'G') { indent(w, pad, pfun); prin1object(arg, pfun); }
                    else if (ch2 == 'X' || ch2 == 'B') {
                        if (integerp(arg)) {
                            uint8_t base = (ch2 == 'B') ? 2 : 16;
                            uint8_t hw = basewidth(arg, base); if (width < hw) w = 0; else w = width-hw;
                            indent(w, pad, pfun); pintbase(arg->integer, base, pfun);
                        } else {
                            indent(w, pad, pfun); prin1object(arg, pfun);
                        }
                    }
                    tilde = false;
                } else formaterr(formatstr, PSTR("invalid directive"), n);
            }
        } else {
            if (ch == '~') { tilde = true; pad = ' '; width = 0; comma = false; quote = false; }
            else if (!mute) pfun(ch);
        }
        n++;
    }
    if (output == nil) return obj;
    else return nil;
}

// LispLibrary

/*
    (require 'symbol)
    Loads the definition of a function defined with defun, or a variable defined with defvar, from the Lisp Library.
    It returns t if it was loaded, or nil if the symbol is already defined or isn't defined in the Lisp Library.
*/
object* fn_require (object* args, object* env) {
    object* arg = first(args);
    object* globals = GlobalEnv;
    if (!symbolp(arg)) error(notasymbol, arg);
    while (globals != NULL) {
        object* pair = first(globals);
        object* var = car(pair);
        if (symbolp(var) && var == arg) return nil;
        globals = cdr(globals);
    }
    GlobalStringIndex = 0;
    object* line = read(glibrary);
    while (line != NULL) {
        // Is this the definition we want
        symbol_t fname = first(line)->name;
        if ((fname == sym(DEFUN) || fname == sym(DEFVAR)) && symbolp(second(line)) && second(line)->name == arg->name) {
            eval(line, env);
            return tee;
        }
        line = read(glibrary);
    }
    return nil;
}

/*
    (list-library)
    Prints a list of the functions defined in the List Library.
*/
object* fn_listlibrary (object* args, object* env) {
    (void) args, (void) env;
    GlobalStringIndex = 0;
    object* line = read(glibrary);
    while (line != NULL) {
        builtin_t bname = builtin(first(line)->name);
        if (bname == DEFUN || bname == DEFVAR) {
            printsymbol(second(line), pserial); pserial(' ');
        }
        line = read(glibrary);
    }
    return bsymbol(NOTHING);
}

// Documentation

/*
    (? item)
    Prints the documentation string of a built-in or user-defined function.
*/
object* sp_help (object* args, object* env) {
    if (args == NULL) error2(noargument);
    object* docstring = documentation(first(args), env);
    if (docstring) {
        char temp = Flags;
        clrflag(PRINTREADABLY);
        printstring(docstring, pserial);
        Flags = temp;
    }
    return bsymbol(NOTHING);
}

/*
    (documentation 'symbol [type])
    Returns the documentation string of a built-in or user-defined function. The type argument is ignored.
*/
object* fn_documentation (object* args, object* env) {
    return documentation(first(args), env);
}

/*
    (apropos item)
    Prints the user-defined and built-in functions whose names contain the specified string or symbol.
*/
object* fn_apropos (object* args, object* env) {
    (void) env;
    apropos(first(args), true);
    return bsymbol(NOTHING);
}

/*
    (apropos-list item)
    Returns a list of user-defined and built-in functions whose names contain the specified string or symbol.
*/
object* fn_aproposlist (object* args, object* env) {
    (void) env;
    return apropos(first(args), false);
}

// Error handling

/*
    (unwind-protect form1 [forms]*)
    Evaluates form1 and forms in order and returns the value of form1,
    but guarantees to evaluate forms even if an error occurs in form1.
*/
object* sp_unwindprotect (object* args, object* env) {
    if (args == NULL) error2(toofewargs);
    object* current_GCStack = GCStack;
    jmp_buf dynamic_handler;
    jmp_buf *previous_handler = handler;
    handler = &dynamic_handler;
    object* protected_form = first(args);
    object* result;

    bool signaled = false;
    if (!setjmp(dynamic_handler)) {
        result = eval(protected_form, env);
    } else {
        GCStack = current_GCStack;
        signaled = true;
    }
    handler = previous_handler;

    object* protective_forms = cdr(args);
    while (protective_forms != NULL) {
        eval(car(protective_forms), env);
        if (tstflag(RETURNFLAG)) break;
        protective_forms = cdr(protective_forms);
    }

    if (!signaled) return result;
    GCStack = NULL;
    longjmp(*handler, 1);
}

/*
    (ignore-errors [forms]*)
    Evaluates forms ignoring errors.
*/
object* sp_ignoreerrors (object* args, object* env) {
    object* current_GCStack = GCStack;
    jmp_buf dynamic_handler;
    jmp_buf *previous_handler = handler;
    handler = &dynamic_handler;
    object* result = nil;

    bool muffled = tstflag(MUFFLEERRORS);
    setflag(MUFFLEERRORS);
    bool signaled = false;
    if (!setjmp(dynamic_handler)) {
        while (args != NULL) {
            result = eval(car(args), env);
            if (tstflag(RETURNFLAG)) break;
            args = cdr(args);
        }
    } else {
        GCStack = current_GCStack;
        signaled = true;
    }
    handler = previous_handler;
    if (!muffled) clrflag(MUFFLEERRORS);

    if (signaled) return bsymbol(NOTHING);
    else return result;
}

/*
    (error controlstring [arguments]*)
    Signals an error. The message is printed by format using the controlstring and arguments.
*/
object* sp_error (object* args, object* env) {
    object* message = eval(cons(bsymbol(FORMAT), cons(nil, args)), env);
    if (!tstflag(MUFFLEERRORS)) {
        char temp = Flags;
        clrflag(PRINTREADABLY);
        pfstring(PSTR("Error: "), pserial); printstring(message, pserial);
        Flags = temp;
        pln(pserial);
    }
    GCStack = NULL;
    longjmp(*handler, 1);
}

// Wi-Fi

/*
    (with-client (str [address port]) form*)
    Evaluates the forms with str bound to a wifi-stream.
*/
object* sp_withclient (object* args, object* env) {
    object* params = first(args);
    object* var = first(params);
    char buffer[BUFFERSIZE];
    params = cdr(params);
    int n;
    if (params == NULL) {
        client = server.available();
        if (!client) return nil;
        n = 2;
    } else {
        object* address = eval(first(params), env);
        object* port = eval(second(params), env);
        int success;
        if (stringp(address)) success = client.connect(cstring(address, buffer, BUFFERSIZE), checkinteger(port));
        else if (integerp(address)) success = client.connect(address->integer, checkinteger(port));
        else error2(PSTR("invalid address"));
        if (!success) return nil;
        n = 1;
    }
    object* pair = cons(var, stream(WIFISTREAM, n));
    push(pair,env);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    client.stop();
    return result;
}

/*
    (available stream)
    Returns the number of bytes available for reading from the wifi-stream, or zero if no bytes are available.
*/
object* fn_available (object* args, object* env) {
    (void) env;
    if (isstream(first(args))>>8 != WIFISTREAM) error2(PSTR("invalid stream"));
    return number(client.available());
}

/*
    (wifi-server)
    Starts a Wi-Fi server running. It returns nil.
*/
object* fn_wifiserver (object* args, object* env) {
    (void) args, (void) env;
    server.begin();
    return nil;
}

/*
    (wifi-softap ssid [password channel hidden])
    Set up a soft access point to establish a Wi-Fi network.
    Returns the IP address as a string or nil if unsuccessful.
*/
object* fn_wifisoftap (object* args, object* env) {
    (void) env;
    char ssid[33], pass[65];
    if (args == NULL) return WiFi.softAPdisconnect(true) ? tee : nil;
    object* first = first(args); args = cdr(args);
    if (args == NULL) WiFi.softAP(cstring(first, ssid, 33));
    else {
        object* second = first(args);
        args = cdr(args);
        int channel = 1;
        bool hidden = false;
        if (args != NULL) {
            channel = checkinteger(first(args));
            args = cdr(args);
            if (args != NULL) hidden = (first(args) != nil);
        }
        WiFi.softAP(cstring(first, ssid, 33), cstring(second, pass, 65), channel, hidden);
    }
    return lispstring((char*)WiFi.softAPIP().toString().c_str());
}

/*
    (connected stream)
    Returns t or nil to indicate if the client on stream is connected.
*/
object* fn_connected (object* args, object* env) {
    (void) env;
    if (isstream(first(args))>>8 != WIFISTREAM) error2(PSTR("invalid stream"));
    return client.connected() ? tee : nil;
}

/*
    (wifi-localip)
    Returns the IP address of the local network as a string.
*/
object* fn_wifilocalip (object* args, object* env) {
    (void) args, (void) env;
    return lispstring((char*)WiFi.localIP().toString().c_str());
}

/*
    (wifi-connect [ssid pass])
    Connects to the Wi-Fi network ssid using password pass. It returns the IP address as a string.
*/
object* fn_wificonnect (object* args, object* env) {
    (void) env;
    char ssid[33], pass[65];
    if (args == NULL) { WiFi.disconnect(true); return nil; }
    if (cdr(args) == NULL) WiFi.begin(cstring(first(args), ssid, 33));
    else WiFi.begin(cstring(first(args), ssid, 33), cstring(second(args), pass, 65));
    int result = WiFi.waitForConnectResult();
    if (result == WL_CONNECTED) return lispstring((char*)WiFi.localIP().toString().c_str());
    else if (result == WL_NO_SSID_AVAIL) error2(PSTR("network not found"));
    else if (result == WL_CONNECT_FAILED) error2(PSTR("connection failed"));
    else error2(PSTR("unable to connect"));
    return nil;
}

// Graphics functions

/*
    (with-gfx (str) form*)
    Evaluates the forms with str bound to an gfx-stream so you can print text
    to the graphics display using the standard uLisp print commands.
*/
object* sp_withgfx (object* args, object* env) {
#if defined(gfxsupport)
    object* params = first(args);
    object* var = first(params);
    object* pair = cons(var, stream(GFXSTREAM, 1));
    push(pair,env);
    object* forms = cdr(args);
    object* result = eval(tf_progn(forms,env), env);
    return result;
#else
    (void) args, (void) env;
    error2(PSTR("not supported"));
    return nil;
#endif
}

/*
    (draw-pixel x y [colour])
    Draws a pixel at coordinates (x,y) in colour, or white if omitted.
*/
object* fn_drawpixel (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t colour = COLOR_WHITE;
    if (cddr(args) != NULL) colour = checkinteger(third(args));
    tft.drawPixel(checkinteger(first(args)), checkinteger(second(args)), colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-line x0 y0 x1 y1 [colour])
    Draws a line from (x0,y0) to (x1,y1) in colour, or white if omitted.
*/
object* fn_drawline (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[4], colour = COLOR_WHITE;
    for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.drawLine(params[0], params[1], params[2], params[3], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-rect x y w h [colour])
    Draws an outline rectangle with its top left corner at (x,y), with width w,
    and with height h. The outline is drawn in colour, or white if omitted.
*/
object* fn_drawrect (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[4], colour = COLOR_WHITE;
    for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.drawRect(params[0], params[1], params[2], params[3], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (fill-rect x y w h [colour])
    Draws a filled rectangle with its top left corner at (x,y), with width w,
    and with height h. The outline is drawn in colour, or white if omitted.
*/
object* fn_fillrect (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[4], colour = COLOR_WHITE;
    for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.fillRect(params[0], params[1], params[2], params[3], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-circle x y r [colour])
    Draws an outline circle with its centre at (x, y) and with radius r.
    The circle is drawn in colour, or white if omitted.
*/
object* fn_drawcircle (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[3], colour = COLOR_WHITE;
    for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.drawCircle(params[0], params[1], params[2], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (fill-circle x y r [colour])
    Draws a filled circle with its centre at (x, y) and with radius r.
    The circle is drawn in colour, or white if omitted.
*/
object* fn_fillcircle (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[3], colour = COLOR_WHITE;
    for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.fillCircle(params[0], params[1], params[2], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-round-rect x y w h radius [colour])
    Draws an outline rounded rectangle with its top left corner at (x,y), with width w,
    height h, and corner radius radius. The outline is drawn in colour, or white if omitted.
*/
object* fn_drawroundrect (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[5], colour = COLOR_WHITE;
    for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.drawRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (fill-round-rect x y w h radius [colour])
    Draws a filled rounded rectangle with its top left corner at (x,y), with width w,
    height h, and corner radius radius. The outline is drawn in colour, or white if omitted.
*/
object* fn_fillroundrect (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[5], colour = COLOR_WHITE;
    for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.fillRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-triangle x0 y0 x1 y1 x2 y2 [colour])
    Draws an outline triangle between (x1,y1), (x2,y2), and (x3,y3).
    The outline is drawn in colour, or white if omitted.
*/
object* fn_drawtriangle (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[6], colour = COLOR_WHITE;
    for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.drawTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (fill-triangle x0 y0 x1 y1 x2 y2 [colour])
    Draws a filled triangle between (x1,y1), (x2,y2), and (x3,y3).
    The outline is drawn in colour, or white if omitted.
*/
object* fn_filltriangle (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t params[6], colour = COLOR_WHITE;
    for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
    if (args != NULL) colour = checkinteger(car(args));
    tft.fillTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (draw-char x y char [colour background size])
    Draws the character char with its top left corner at (x,y).
    The character is drawn in a 5 x 7 pixel font in colour against background,
    which default to white and black respectively.
    The character can optionally be scaled by size.
*/
object* fn_drawchar (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t colour = COLOR_WHITE, bg = COLOR_BLACK, size = 1;
    object* more = cdr(cddr(args));
    if (more != NULL) {
        colour = checkinteger(car(more));
        more = cdr(more);
        if (more != NULL) {
            bg = checkinteger(car(more));
            more = cdr(more);
            if (more != NULL) size = checkinteger(car(more));
        }
    }
    tft.drawChar(checkinteger(first(args)), checkinteger(second(args)), checkchar(third(args)),
        colour, bg, size);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (set-cursor x y)
    Sets the start point for text plotting to (x, y).
*/
object* fn_setcursor (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    tft.setCursor(checkinteger(first(args)), checkinteger(second(args)));
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (set-text-color colour [background])
    Sets the text colour for text plotted using (with-gfx ...).
*/
object* fn_settextcolor (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    if (cdr(args) != NULL) tft.setTextColor(checkinteger(first(args)), checkinteger(second(args)));
    else tft.setTextColor(checkinteger(first(args)));
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (set-text-size scale)
    Scales text by the specified size, default 1.
*/
object* fn_settextsize (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    tft.setTextSize(checkinteger(first(args)));
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (set-text-wrap boolean)
    Specified whether text wraps at the right-hand edge of the display; the default is t.
*/
object* fn_settextwrap (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    tft.setTextWrap(first(args) != NULL);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (fill-screen [colour])
    Fills or clears the screen with colour, default black.
*/
object* fn_fillscreen (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    uint16_t colour = COLOR_BLACK;
    if (args != NULL) colour = checkinteger(first(args));
    tft.fillScreen(colour);
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (set-rotation option)
    Sets the display orientation for subsequent graphics commands; values are 0, 1, 2, or 3.
*/
object* fn_setrotation (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    tft.setRotation(checkinteger(first(args)));
    #else
    (void) args;
    #endif
    return nil;
}

/*
    (invert-display boolean)
    Mirror-images the display. 
*/
object* fn_invertdisplay (object* args, object* env) {
    (void) env;
    #if defined(gfxsupport)
    tft.invertDisplay(first(args) != NULL);
    #else
    (void) args;
    #endif
    return nil;
}

// Built-in symbol names
const char string0[] PROGMEM = "nil";
const char string1[] PROGMEM = "t";
const char string2[] PROGMEM = "nothing";
const char string3[] PROGMEM = "&optional";
const char string4[] PROGMEM = ":initial-element";
const char string5[] PROGMEM = ":element-type";
const char string6[] PROGMEM = "bit";
const char string7[] PROGMEM = "&rest";
const char string8[] PROGMEM = "lambda";
const char string9[] PROGMEM = "let";
const char string10[] PROGMEM = "let*";
const char string11[] PROGMEM = "closure";
const char string12[] PROGMEM = "*pc*";
const char string13[] PROGMEM = "quote";
const char string14[] PROGMEM = "defun";
const char string15[] PROGMEM = "defvar";
const char string16[] PROGMEM = "car";
const char string17[] PROGMEM = "first";
const char string18[] PROGMEM = "cdr";
const char string19[] PROGMEM = "rest";
const char string20[] PROGMEM = "nth";
const char string21[] PROGMEM = "aref";
const char string22[] PROGMEM = "string";
const char string23[] PROGMEM = "pinmode";
const char string24[] PROGMEM = "digitalwrite";
const char string25[] PROGMEM = "analogread";
const char string26[] PROGMEM = "register";
const char string27[] PROGMEM = "format";
const char string28[] PROGMEM = "or";
const char string29[] PROGMEM = "setq";
const char string30[] PROGMEM = "loop";
const char string31[] PROGMEM = "return";
const char string32[] PROGMEM = "push";
const char string33[] PROGMEM = "pop";
const char string34[] PROGMEM = "incf";
const char string35[] PROGMEM = "decf";
const char string36[] PROGMEM = "setf";
const char string37[] PROGMEM = "dolist";
const char string38[] PROGMEM = "dotimes";
const char string39[] PROGMEM = "trace";
const char string40[] PROGMEM = "untrace";
const char string41[] PROGMEM = "for-millis";
const char string42[] PROGMEM = "time";
const char string43[] PROGMEM = "with-output-to-string";
const char string44[] PROGMEM = "with-serial";
const char string45[] PROGMEM = "with-i2c";
const char string46[] PROGMEM = "with-spi";
const char string47[] PROGMEM = "with-sd-card";
const char string48[] PROGMEM = "progn";
const char string49[] PROGMEM = "if";
const char string50[] PROGMEM = "cond";
const char string51[] PROGMEM = "when";
const char string52[] PROGMEM = "unless";
const char string53[] PROGMEM = "case";
const char string54[] PROGMEM = "and";
const char string55[] PROGMEM = "not";
const char string56[] PROGMEM = "null";
const char string57[] PROGMEM = "cons";
const char string58[] PROGMEM = "atom";
const char string59[] PROGMEM = "listp";
const char string60[] PROGMEM = "consp";
const char string61[] PROGMEM = "symbolp";
const char string62[] PROGMEM = "arrayp";
const char string63[] PROGMEM = "boundp";
const char string64[] PROGMEM = "keywordp";
const char string65[] PROGMEM = "set";
const char string66[] PROGMEM = "streamp";
const char string67[] PROGMEM = "eq";
const char string68[] PROGMEM = "equal";
const char string69[] PROGMEM = "caar";
const char string70[] PROGMEM = "cadr";
const char string71[] PROGMEM = "second";
const char string72[] PROGMEM = "cdar";
const char string73[] PROGMEM = "cddr";
const char string74[] PROGMEM = "caaar";
const char string75[] PROGMEM = "caadr";
const char string76[] PROGMEM = "cadar";
const char string77[] PROGMEM = "caddr";
const char string78[] PROGMEM = "third";
const char string79[] PROGMEM = "cdaar";
const char string80[] PROGMEM = "cdadr";
const char string81[] PROGMEM = "cddar";
const char string82[] PROGMEM = "cdddr";
const char string83[] PROGMEM = "length";
const char string84[] PROGMEM = "array-dimensions";
const char string85[] PROGMEM = "list";
const char string86[] PROGMEM = "make-array";
const char string87[] PROGMEM = "reverse";
const char string88[] PROGMEM = "assoc";
const char string89[] PROGMEM = "member";
const char string90[] PROGMEM = "apply";
const char string91[] PROGMEM = "funcall";
const char string92[] PROGMEM = "append";
const char string93[] PROGMEM = "mapc";
const char string94[] PROGMEM = "mapcar";
const char string95[] PROGMEM = "mapcan";
const char string96[] PROGMEM = "+";
const char string97[] PROGMEM = "-";
const char string98[] PROGMEM = "*";
const char string99[] PROGMEM = "/";
const char string100[] PROGMEM = "mod";
const char string101[] PROGMEM = "1+";
const char string102[] PROGMEM = "1-";
const char string103[] PROGMEM = "abs";
const char string104[] PROGMEM = "random";
const char string105[] PROGMEM = "max";
const char string106[] PROGMEM = "min";
const char string107[] PROGMEM = "/=";
const char string108[] PROGMEM = "=";
const char string109[] PROGMEM = "<";
const char string110[] PROGMEM = "<=";
const char string111[] PROGMEM = ">";
const char string112[] PROGMEM = ">=";
const char string113[] PROGMEM = "plusp";
const char string114[] PROGMEM = "minusp";
const char string115[] PROGMEM = "zerop";
const char string116[] PROGMEM = "oddp";
const char string117[] PROGMEM = "evenp";
const char string118[] PROGMEM = "integerp";
const char string119[] PROGMEM = "numberp";
const char string120[] PROGMEM = "float";
const char string121[] PROGMEM = "floatp";
const char string122[] PROGMEM = "sin";
const char string123[] PROGMEM = "cos";
const char string124[] PROGMEM = "tan";
const char string125[] PROGMEM = "asin";
const char string126[] PROGMEM = "acos";
const char string127[] PROGMEM = "atan";
const char string128[] PROGMEM = "sinh";
const char string129[] PROGMEM = "cosh";
const char string130[] PROGMEM = "tanh";
const char string131[] PROGMEM = "exp";
const char string132[] PROGMEM = "sqrt";
const char string133[] PROGMEM = "log";
const char string134[] PROGMEM = "expt";
const char string135[] PROGMEM = "ceiling";
const char string136[] PROGMEM = "floor";
const char string137[] PROGMEM = "truncate";
const char string138[] PROGMEM = "round";
const char string139[] PROGMEM = "char";
const char string140[] PROGMEM = "char-code";
const char string141[] PROGMEM = "code-char";
const char string142[] PROGMEM = "characterp";
const char string143[] PROGMEM = "stringp";
const char string144[] PROGMEM = "string=";
const char string145[] PROGMEM = "string<";
const char string146[] PROGMEM = "string>";
const char string147[] PROGMEM = "sort";
const char string148[] PROGMEM = "concatenate";
const char string149[] PROGMEM = "subseq";
const char string150[] PROGMEM = "search";
const char string151[] PROGMEM = "read-from-string";
const char string152[] PROGMEM = "princ-to-string";
const char string153[] PROGMEM = "prin1-to-string";
const char string154[] PROGMEM = "logand";
const char string155[] PROGMEM = "logior";
const char string156[] PROGMEM = "logxor";
const char string157[] PROGMEM = "lognot";
const char string158[] PROGMEM = "ash";
const char string159[] PROGMEM = "logbitp";
const char string160[] PROGMEM = "eval";
const char string161[] PROGMEM = "globals";
const char string162[] PROGMEM = "locals";
const char string163[] PROGMEM = "makunbound";
const char string164[] PROGMEM = "break";
const char string165[] PROGMEM = "read";
const char string166[] PROGMEM = "prin1";
const char string167[] PROGMEM = "print";
const char string168[] PROGMEM = "princ";
const char string169[] PROGMEM = "terpri";
const char string170[] PROGMEM = "read-byte";
const char string171[] PROGMEM = "read-line";
const char string172[] PROGMEM = "write-byte";
const char string173[] PROGMEM = "write-string";
const char string174[] PROGMEM = "write-line";
const char string175[] PROGMEM = "restart-i2c";
const char string176[] PROGMEM = "gc";
const char string177[] PROGMEM = "room";
const char string178[] PROGMEM = "save-image";
const char string179[] PROGMEM = "load-image";
const char string180[] PROGMEM = "cls";
const char string181[] PROGMEM = "digitalread";
const char string182[] PROGMEM = "analogreadresolution";
const char string183[] PROGMEM = "analogwrite";
const char string184[] PROGMEM = "delay";
const char string185[] PROGMEM = "millis";
const char string186[] PROGMEM = "sleep";
const char string187[] PROGMEM = "note";
const char string188[] PROGMEM = "edit";
const char string189[] PROGMEM = "pprint";
const char string190[] PROGMEM = "pprintall";
const char string191[] PROGMEM = "require";
const char string192[] PROGMEM = "list-library";
const char string193[] PROGMEM = "?";
const char string194[] PROGMEM = "documentation";
const char string195[] PROGMEM = "apropos";
const char string196[] PROGMEM = "apropos-list";
const char string197[] PROGMEM = "unwind-protect";
const char string198[] PROGMEM = "ignore-errors";
const char string199[] PROGMEM = "error";
const char string200[] PROGMEM = "with-client";
const char string201[] PROGMEM = "available";
const char string202[] PROGMEM = "wifi-server";
const char string203[] PROGMEM = "wifi-softap";
const char string204[] PROGMEM = "connected";
const char string205[] PROGMEM = "wifi-localip";
const char string206[] PROGMEM = "wifi-connect";
const char string207[] PROGMEM = "with-gfx";
const char string208[] PROGMEM = "draw-pixel";
const char string209[] PROGMEM = "draw-line";
const char string210[] PROGMEM = "draw-rect";
const char string211[] PROGMEM = "fill-rect";
const char string212[] PROGMEM = "draw-circle";
const char string213[] PROGMEM = "fill-circle";
const char string214[] PROGMEM = "draw-round-rect";
const char string215[] PROGMEM = "fill-round-rect";
const char string216[] PROGMEM = "draw-triangle";
const char string217[] PROGMEM = "fill-triangle";
const char string218[] PROGMEM = "draw-char";
const char string219[] PROGMEM = "set-cursor";
const char string220[] PROGMEM = "set-text-color";
const char string221[] PROGMEM = "set-text-size";
const char string222[] PROGMEM = "set-text-wrap";
const char string223[] PROGMEM = "fill-screen";
const char string224[] PROGMEM = "set-rotation";
const char string225[] PROGMEM = "invert-display";
const char string226[] PROGMEM = ":led-builtin";
const char string227[] PROGMEM = ":high";
const char string228[] PROGMEM = ":low";
const char string229[] PROGMEM = ":input";
const char string230[] PROGMEM = ":input-pullup";
const char string231[] PROGMEM = ":input-pulldown";
const char string232[] PROGMEM = ":output";

// Documentation strings
const char doc0[] PROGMEM = "nil\n"
"A symbol equivalent to the empty list (). Also represents false.";
const char doc1[] PROGMEM = "t\n"
"A symbol representing true.";
const char doc2[] PROGMEM = "nothing\n"
"A symbol with no value.\n"
"It is useful if you want to suppress printing the result of evaluating a function.";
const char doc3[] PROGMEM = "&optional\n"
"Can be followed by one or more optional parameters in a lambda or defun parameter list.";
const char doc7[] PROGMEM = "&rest\n"
"Can be followed by a parameter in a lambda or defun parameter list,\n"
"and is assigned a list of the corresponding arguments.";
const char doc8[] PROGMEM = "(lambda (parameter*) form*)\n"
"Creates an unnamed function with parameters. The body is evaluated with the parameters as local variables\n"
"whose initial values are defined by the values of the forms after the lambda form.";
const char doc9[] PROGMEM = "(let ((var value) ... ) forms*)\n"
"Declares local variables with values, and evaluates the forms with those local variables.";
const char doc10[] PROGMEM = "(let* ((var value) ... ) forms*)\n"
"Declares local variables with values, and evaluates the forms with those local variables.\n"
"Each declaration can refer to local variables that have been defined earlier in the let*.";
const char doc14[] PROGMEM = "(defun name (parameters) form*)\n"
"Defines a function.";
const char doc15[] PROGMEM = "(defvar variable form)\n"
"Defines a global variable.";
const char doc16[] PROGMEM = "(car list)\n"
"Returns the first item in a list.";
const char doc18[] PROGMEM = "(cdr list)\n"
"Returns a list with the first item removed.";
const char doc20[] PROGMEM = "(nth number list)\n"
"Returns the nth item in list, counting from zero.";
const char doc21[] PROGMEM = "(aref array index [index*])\n"
"Returns an element from the specified array.";
const char doc22[] PROGMEM = "(string item)\n"
"Converts its argument to a string.";
const char doc23[] PROGMEM = "(pinmode pin mode)\n"
"Sets the input/output mode of an Arduino pin number, and returns nil.\n"
"The mode parameter can be an integer, a keyword, or t or nil.";
const char doc24[] PROGMEM = "(digitalwrite pin state)\n"
"Sets the state of the specified Arduino pin number.";
const char doc25[] PROGMEM = "(analogread pin)\n"
"Reads the specified Arduino analogue pin number and returns the value.";
const char doc26[] PROGMEM = "(register address [value])\n"
"Reads or writes the value of a peripheral register.\n"
"If value is not specified the function returns the value of the register at address.\n"
"If value is specified the value is written to the register at address and the function returns value.";
const char doc27[] PROGMEM = "(format output controlstring [arguments]*)\n"
"Outputs its arguments formatted according to the format directives in controlstring.";
const char doc28[] PROGMEM = "(or item*)\n"
"Evaluates its arguments until one returns non-nil, and returns its value.";
const char doc29[] PROGMEM = "(setq symbol value [symbol value]*)\n"
"For each pair of arguments assigns the value of the second argument\n"
"to the variable specified in the first argument.";
const char doc30[] PROGMEM = "(loop forms*)\n"
"Executes its arguments repeatedly until one of the arguments calls (return),\n"
"which then causes an exit from the loop.";
const char doc31[] PROGMEM = "(return [value])\n"
"Exits from a (dotimes ...), (dolist ...), or (loop ...) loop construct and returns value.";
const char doc32[] PROGMEM = "(push item place)\n"
"Modifies the value of place, which should be a list, to add item onto the front of the list,\n"
"and returns the new list.";
const char doc33[] PROGMEM = "(pop place)\n"
"Modifies the value of place, which should be a list, to remove its first item, and returns that item.";
const char doc34[] PROGMEM = "(incf place [number])\n"
"Increments a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional increment which defaults to 1.";
const char doc35[] PROGMEM = "(decf place [number])\n"
"Decrements a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional decrement which defaults to 1.";
const char doc36[] PROGMEM = "(setf place value [place value]*)\n"
"For each pair of arguments modifies a place to the result of evaluating value.";
const char doc37[] PROGMEM = "(dolist (var list [result]) form*)\n"
"Sets the local variable var to each element of list in turn, and executes the forms.\n"
"It then returns result, or nil if result is omitted.";
const char doc38[] PROGMEM = "(dotimes (var number [result]) form*)\n"
"Executes the forms number times, with the local variable var set to each integer from 0 to number-1 in turn.\n"
"It then returns result, or nil if result is omitted.";
const char doc39[] PROGMEM = "(trace [function]*)\n"
"Turns on tracing of up to " stringify(TRACEMAX) " user-defined functions,\n"
"and returns a list of the functions currently being traced.";
const char doc40[] PROGMEM = "(untrace [function]*)\n"
"Turns off tracing of up to " stringify(TRACEMAX) " user-defined functions, and returns a list of the functions untraced.\n"
"If no functions are specified it untraces all functions.";
const char doc41[] PROGMEM = "(for-millis ([number]) form*)\n"
"Executes the forms and then waits until a total of number milliseconds have elapsed.\n"
"Returns the total number of milliseconds taken.";
const char doc42[] PROGMEM = "(time form)\n"
"Prints the value returned by the form, and the time taken to evaluate the form\n"
"in milliseconds or seconds.";
const char doc43[] PROGMEM = "(with-output-to-string (str) form*)\n"
"Returns a string containing the output to the stream variable str.";
const char doc44[] PROGMEM = "(with-serial (str port [baud]) form*)\n"
"Evaluates the forms with str bound to a serial-stream using port.\n"
"The optional baud gives the baud rate divided by 100, default 96.";
const char doc45[] PROGMEM = "(with-i2c (str [port] address [read-p]) form*)\n"
"Evaluates the forms with str bound to an i2c-stream defined by address.\n"
"If read-p is nil or omitted the stream is written to, otherwise it specifies the number of bytes\n"
"to be read from the stream. The port if specified is ignored.";
const char doc46[] PROGMEM = "(with-spi (str pin [clock] [bitorder] [mode]) form*)\n"
"Evaluates the forms with str bound to an spi-stream.\n"
"The parameters specify the enable pin, clock in kHz (default 4000),\n"
"bitorder 0 for LSBFIRST and 1 for MSBFIRST (default 1), and SPI mode (default 0).";
const char doc47[] PROGMEM = "(with-sd-card (str filename [mode]) form*)\n"
"Evaluates the forms with str bound to an sd-stream reading from or writing to the file filename.\n"
"If mode is omitted the file is read, otherwise 0 means read, 1 write-append, or 2 write-overwrite.";
const char doc48[] PROGMEM = "(progn form*)\n"
"Evaluates several forms grouped together into a block, and returns the result of evaluating the last form.";
const char doc49[] PROGMEM = "(if test then [else])\n"
"Evaluates test. If it's non-nil the form then is evaluated and returned;\n"
"otherwise the form else is evaluated and returned.";
const char doc50[] PROGMEM = "(cond ((test form*) (test form*) ... ))\n"
"Each argument is a list consisting of a test optionally followed by one or more forms.\n"
"If the test evaluates to non-nil the forms are evaluated, and the last value is returned as the result of the cond.\n"
"If the test evaluates to nil, none of the forms are evaluated, and the next argument is processed in the same way.";
const char doc51[] PROGMEM = "(when test form*)\n"
"Evaluates the test. If it's non-nil the forms are evaluated and the last value is returned.";
const char doc52[] PROGMEM = "(unless test form*)\n"
"Evaluates the test. If it's nil the forms are evaluated and the last value is returned.";
const char doc53[] PROGMEM = "(case keyform ((key form*) (key form*) ... ))\n"
"Evaluates a keyform to produce a test key, and then tests this against a series of arguments,\n"
"each of which is a list containing a key optionally followed by one or more forms.";
const char doc54[] PROGMEM = "(and item*)\n"
"Evaluates its arguments until one returns nil, and returns the last value.";
const char doc55[] PROGMEM = "(not item)\n"
"Returns t if its argument is nil, or nil otherwise. Equivalent to null.";
const char doc57[] PROGMEM = "(cons item item)\n"
"If the second argument is a list, cons returns a new list with item added to the front of the list.\n"
"If the second argument isn't a list cons returns a dotted pair.";
const char doc58[] PROGMEM = "(atom item)\n"
"Returns t if its argument is a single number, symbol, or nil.";
const char doc59[] PROGMEM = "(listp item)\n"
"Returns t if its argument is a list.";
const char doc60[] PROGMEM = "(consp item)\n"
"Returns t if its argument is a non-null list.";
const char doc61[] PROGMEM = "(symbolp item)\n"
"Returns t if its argument is a symbol.";
const char doc62[] PROGMEM = "(arrayp item)\n"
"Returns t if its argument is an array.";
const char doc63[] PROGMEM = "(boundp item)\n"
"Returns t if its argument is a symbol with a value.";
const char doc64[] PROGMEM = "(keywordp item)\n"
"Returns t if its argument is a keyword.";
const char doc65[] PROGMEM = "(set symbol value [symbol value]*)\n"
"For each pair of arguments, assigns the value of the second argument to the value of the first argument.";
const char doc66[] PROGMEM = "(streamp item)\n"
"Returns t if its argument is a stream.";
const char doc67[] PROGMEM = "(eq item item)\n"
"Tests whether the two arguments are the same symbol, same character, equal numbers,\n"
"or point to the same cons, and returns t or nil as appropriate.";
const char doc68[] PROGMEM = "(equal item item)\n"
"Tests whether the two arguments are the same symbol, same character, equal numbers,\n"
"or point to the same cons, and returns t or nil as appropriate.";
const char doc69[] PROGMEM = "(caar list)";
const char doc70[] PROGMEM = "(cadr list)";
const char doc72[] PROGMEM = "(cdar list)\n"
"Equivalent to (cdr (car list)).";
const char doc73[] PROGMEM = "(cddr list)\n"
"Equivalent to (cdr (cdr list)).";
const char doc74[] PROGMEM = "(caaar list)\n"
"Equivalent to (car (car (car list))).";
const char doc75[] PROGMEM = "(caadr list)\n"
"Equivalent to (car (car (cdar list))).";
const char doc76[] PROGMEM = "(cadar list)\n"
"Equivalent to (car (cdr (car list))).";
const char doc77[] PROGMEM = "(caddr list)\n"
"Equivalent to (car (cdr (cdr list))).";
const char doc79[] PROGMEM = "(cdaar list)\n"
"Equivalent to (cdar (car (car list))).";
const char doc80[] PROGMEM = "(cdadr list)\n"
"Equivalent to (cdr (car (cdr list))).";
const char doc81[] PROGMEM = "(cddar list)\n"
"Equivalent to (cdr (cdr (car list))).";
const char doc82[] PROGMEM = "(cdddr list)\n"
"Equivalent to (cdr (cdr (cdr list))).";
const char doc83[] PROGMEM = "(length item)\n"
"Returns the number of items in a list, the length of a string, or the length of a one-dimensional array.";
const char doc84[] PROGMEM = "(array-dimensions item)\n"
"Returns a list of the dimensions of an array.";
const char doc85[] PROGMEM = "(list item*)\n"
"Returns a list of the values of its arguments.";
const char doc86[] PROGMEM = "(make-array size [:initial-element element] [:element-type 'bit])\n"
"If size is an integer it creates a one-dimensional array with elements from 0 to size-1.\n"
"If size is a list of n integers it creates an n-dimensional array with those dimensions.\n"
"If :element-type 'bit is specified the array is a bit array.";
const char doc87[] PROGMEM = "(reverse list)\n"
"Returns a list with the elements of list in reverse order.";
const char doc88[] PROGMEM = "(assoc key list)\n"
"Looks up a key in an association list of (key . value) pairs,\n"
"and returns the matching pair, or nil if no pair is found.";
const char doc89[] PROGMEM = "(member item list)\n"
"Searches for an item in a list, using eq, and returns the list starting from the first occurrence of the item,\n"
"or nil if it is not found.";
const char doc90[] PROGMEM = "(apply function list)\n"
"Returns the result of evaluating function, with the list of arguments specified by the second parameter.";
const char doc91[] PROGMEM = "(funcall function argument*)\n"
"Evaluates function with the specified arguments.";
const char doc92[] PROGMEM = "(append list*)\n"
"Joins its arguments, which should be lists, into a single list.";
const char doc93[] PROGMEM = "(mapc function list1 [list]*)\n"
"Applies the function to each element in one or more lists, ignoring the results.\n"
"It returns the first list argument.";
const char doc94[] PROGMEM = "(mapcar function list1 [list]*)\n"
"Applies the function to each element in one or more lists, and returns the resulting list.";
const char doc95[] PROGMEM = "(mapcan function list1 [list]*)\n"
"Applies the function to each element in one or more lists. The results should be lists,\n"
"and these are appended together to give the value returned.";
const char doc96[] PROGMEM = "(+ number*)\n"
"Adds its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise a floating-point number.";
const char doc97[] PROGMEM = "(- number*)\n"
"If there is one argument, negates the argument.\n"
"If there are two or more arguments, subtracts the second and subsequent arguments from the first argument.\n"
"If each argument is an integer, and the running total doesn't overflow, returns the result as an integer,\n"
"otherwise a floating-point number.";
const char doc98[] PROGMEM = "(* number*)\n"
"Multiplies its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise it's a floating-point number.";
const char doc99[] PROGMEM = "(/ number*)\n"
"Divides the first argument by the second and subsequent arguments.\n"
"If each argument is an integer, and each division produces an exact result, the result is an integer;\n"
"otherwise it's a floating-point number.";
const char doc100[] PROGMEM = "(mod number number)\n"
"Returns its first argument modulo the second argument.\n"
"If both arguments are integers the result is an integer; otherwise it's a floating-point number.";
const char doc101[] PROGMEM = "(1+ number)\n"
"Adds one to its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc102[] PROGMEM = "(1- number)\n"
"Subtracts one from its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc103[] PROGMEM = "(abs number)\n"
"Returns the absolute, positive value of its argument.\n"
"If the argument is an integer the result will be returned as an integer if possible,\n"
"otherwise a floating-point number.";
const char doc104[] PROGMEM = "(random number)\n"
"If number is an integer returns a random number between 0 and one less than its argument.\n"
"Otherwise returns a floating-point number between zero and number.";
const char doc105[] PROGMEM = "(max number*)\n"
"Returns the maximum of one or more arguments.";
const char doc106[] PROGMEM = "(min number*)\n"
"Returns the minimum of one or more arguments.";
const char doc107[] PROGMEM = "(/= number*)\n"
"Returns t if none of the arguments are equal, or nil if two or more arguments are equal.";
const char doc108[] PROGMEM = "(= number*)\n"
"Returns t if all the arguments, which must be numbers, are numerically equal, and nil otherwise.";
const char doc109[] PROGMEM = "(< number*)\n"
"Returns t if each argument is less than the next argument, and nil otherwise.";
const char doc110[] PROGMEM = "(<= number*)\n"
"Returns t if each argument is less than or equal to the next argument, and nil otherwise.";
const char doc111[] PROGMEM = "(> number*)\n"
"Returns t if each argument is greater than the next argument, and nil otherwise.";
const char doc112[] PROGMEM = "(>= number*)\n"
"Returns t if each argument is greater than or equal to the next argument, and nil otherwise.";
const char doc113[] PROGMEM = "(plusp number)\n"
"Returns t if the argument is greater than zero, or nil otherwise.";
const char doc114[] PROGMEM = "(minusp number)\n"
"Returns t if the argument is less than zero, or nil otherwise.";
const char doc115[] PROGMEM = "(zerop number)\n"
"Returns t if the argument is zero.";
const char doc116[] PROGMEM = "(oddp number)\n"
"Returns t if the integer argument is odd.";
const char doc117[] PROGMEM = "(evenp number)\n"
"Returns t if the integer argument is even.";
const char doc118[] PROGMEM = "(integerp number)\n"
"Returns t if the argument is an integer.";
const char doc119[] PROGMEM = "(numberp number)\n"
"Returns t if the argument is a number.";
const char doc120[] PROGMEM = "(float number)\n"
"Returns its argument converted to a floating-point number.";
const char doc121[] PROGMEM = "(floatp number)\n"
"Returns t if the argument is a floating-point number.";
const char doc122[] PROGMEM = "(sin number)\n"
"Returns sin(number).";
const char doc123[] PROGMEM = "(cos number)\n"
"Returns cos(number).";
const char doc124[] PROGMEM = "(tan number)\n"
"Returns tan(number).";
const char doc125[] PROGMEM = "(asin number)\n"
"Returns asin(number).";
const char doc126[] PROGMEM = "(acos number)\n"
"Returns acos(number).";
const char doc127[] PROGMEM = "(atan number1 [number2])\n"
"Returns the arc tangent of number1/number2, in radians. If number2 is omitted it defaults to 1.";
const char doc128[] PROGMEM = "(sinh number)\n"
"Returns sinh(number).";
const char doc129[] PROGMEM = "(cosh number)\n"
"Returns cosh(number).";
const char doc130[] PROGMEM = "(tanh number)\n"
"Returns tanh(number).";
const char doc131[] PROGMEM = "(exp number)\n"
"Returns exp(number).";
const char doc132[] PROGMEM = "(sqrt number)\n"
"Returns sqrt(number).";
const char doc133[] PROGMEM = "(log number [base])\n"
"Returns the logarithm of number to the specified base. If base is omitted it defaults to e.";
const char doc134[] PROGMEM = "(expt number power)\n"
"Returns number raised to the specified power.\n"
"Returns the result as an integer if the arguments are integers and the result will be within range,\n"
"otherwise a floating-point number.";
const char doc135[] PROGMEM = "(ceiling number [divisor])\n"
"Returns ceil(number/divisor). If omitted, divisor is 1.";
const char doc136[] PROGMEM = "(floor number [divisor])\n"
"Returns floor(number/divisor). If omitted, divisor is 1.";
const char doc137[] PROGMEM = "(truncate number [divisor])\n"
"Returns the integer part of number/divisor. If divisor is omitted it defaults to 1.";
const char doc138[] PROGMEM = "(round number [divisor])\n"
"Returns the integer closest to number/divisor. If divisor is omitted it defaults to 1.";
const char doc139[] PROGMEM = "(char string n)\n"
"Returns the nth character in a string, counting from zero.";
const char doc140[] PROGMEM = "(char-code character)\n"
"Returns the ASCII code for a character, as an integer.";
const char doc141[] PROGMEM = "(code-char integer)\n"
"Returns the character for the specified ASCII code.";
const char doc142[] PROGMEM = "(characterp item)\n"
"Returns t if the argument is a character and nil otherwise.";
const char doc143[] PROGMEM = "(stringp item)\n"
"Returns t if the argument is a string and nil otherwise.";
const char doc144[] PROGMEM = "(string= string string)\n"
"Tests whether two strings are the same.";
const char doc145[] PROGMEM = "(string< string string)\n"
"Returns t if the first string is alphabetically less than the second string, and nil otherwise.";
const char doc146[] PROGMEM = "(string> string string)\n"
"Returns t if the first string is alphabetically greater than the second string, and nil otherwise.";
const char doc147[] PROGMEM = "(sort list test)\n"
"Destructively sorts list according to the test function, using an insertion sort, and returns the sorted list.";
const char doc148[] PROGMEM = "(concatenate 'string string*)\n"
"Joins together the strings given in the second and subsequent arguments, and returns a single string.";
const char doc149[] PROGMEM = "(subseq seq start [end])\n"
"Returns a subsequence of a list or string from item start to item end-1.";
const char doc150[] PROGMEM = "(search pattern target)\n"
"Returns the index of the first occurrence of pattern in target,\n"
"which can be lists or strings, or nil if it's not found.";
const char doc151[] PROGMEM = "(read-from-string string)\n"
"Reads an atom or list from the specified string and returns it.";
const char doc152[] PROGMEM = "(princ-to-string item)\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc153[] PROGMEM = "(prin1-to-string item [stream])\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed with quotation marks and escape characters,\n"
"in a format that will be suitable for read-from-string.";
const char doc154[] PROGMEM = "(logand [value*])\n"
"Returns the bitwise & of the values.";
const char doc155[] PROGMEM = "(logior [value*])\n"
"Returns the bitwise | of the values.";
const char doc156[] PROGMEM = "(logxor [value*])\n"
"Returns the bitwise ^ of the values.";
const char doc157[] PROGMEM = "(lognot number)\n"
"Returns the bitwise inverse of the number.";
const char doc158[] PROGMEM = "(ash value shift)\n"
"Returns the result of bitwise shifting value by shift bits. If shift is positive, value is shifted to the left.";
const char doc159[] PROGMEM = "(logbitp bit value)\n"
"Returns t if bit number bit in value is a '1', and nil if it is a '0'.";
const char doc160[] PROGMEM = "(eval form*)\n"
"Evaluates its argument an extra time.";
const char doc161[] PROGMEM = "(globals)\n"
"Returns a list of global variables.";
const char doc162[] PROGMEM = "(locals)\n"
"Returns an association list of local variables and their values.";
const char doc163[] PROGMEM = "(makunbound symbol)\n"
"Removes the value of the symbol from GlobalEnv and returns the symbol.";
const char doc164[] PROGMEM = "(break)\n"
"Inserts a breakpoint in the program. When evaluated prints Break! and reenters the REPL.";
const char doc165[] PROGMEM = "(read [stream])\n"
"Reads an atom or list from the serial input and returns it.\n"
"If stream is specified the item is read from the specified stream.";
const char doc166[] PROGMEM = "(prin1 item [stream])\n"
"Prints its argument, and returns its value.\n"
"Strings are printed with quotation marks and escape characters.";
const char doc167[] PROGMEM = "(print item [stream])\n"
"Prints its argument with quotation marks and escape characters, on a new line, and followed by a space.\n"
"If stream is specified the argument is printed to the specified stream.";
const char doc168[] PROGMEM = "(princ item [stream])\n"
"Prints its argument, and returns its value.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc169[] PROGMEM = "(terpri [stream])\n"
"Prints a new line, and returns nil.\n"
"If stream is specified the new line is written to the specified stream.";
const char doc170[] PROGMEM = "(read-byte stream)\n"
"Reads a byte from a stream and returns it.";
const char doc171[] PROGMEM = "(read-line [stream])\n"
"Reads characters from the serial input up to a newline character, and returns them as a string, excluding the newline.\n"
"If stream is specified the line is read from the specified stream.";
const char doc172[] PROGMEM = "(write-byte number [stream])\n"
"Writes a byte to a stream.";
const char doc173[] PROGMEM = "(write-string string [stream])\n"
"Writes a string. If stream is specified the string is written to the stream.";
const char doc174[] PROGMEM = "(write-line string [stream])\n"
"Writes a string terminated by a newline character. If stream is specified the string is written to the stream.";
const char doc175[] PROGMEM = "(restart-i2c stream [read-p])\n"
"Restarts an i2c-stream.\n"
"If read-p is nil or omitted the stream is written to.\n"
"If read-p is an integer it specifies the number of bytes to be read from the stream.";
const char doc176[] PROGMEM = "(gc)\n"
"Forces a garbage collection and prints the number of objects collected, and the time taken.";
const char doc177[] PROGMEM = "(room)\n"
"Returns the number of free Lisp cells remaining.";
const char doc180[] PROGMEM = "(cls)\n"
"Prints a clear-screen character.";
const char doc181[] PROGMEM = "(digitalread pin)\n"
"Reads the state of the specified Arduino pin number and returns t (high) or nil (low).";
const char doc182[] PROGMEM = "(analogreadresolution bits)\n"
"Specifies the resolution for the analogue inputs on platforms that support it.\n"
"The default resolution on all platforms is 10 bits.";
const char doc183[] PROGMEM = "(analogwrite pin value)\n"
"Writes the value to the specified Arduino pin number.";
const char doc184[] PROGMEM = "(delay number)\n"
"Delays for a specified number of milliseconds.";
const char doc185[] PROGMEM = "(millis)\n"
"Returns the time in milliseconds that uLisp has been running.";
const char doc186[] PROGMEM = "(sleep secs)\n"
"Puts the processor into a low-power sleep mode for secs.\n"
"Only supported on some platforms. On other platforms it does delay(1000*secs).";
const char doc187[] PROGMEM = "(note [pin] [note] [octave])\n"
"Generates a square wave on pin.\n"
"The argument note represents the note in the well-tempered scale, from 0 to 11,\n"
"where 0 represents C, 1 represents C#, and so on.\n"
"The argument octave can be from 3 to 6. If omitted it defaults to 0.";
const char doc188[] PROGMEM = "(edit 'function)\n"
"Calls the Lisp tree editor to allow you to edit a function definition.";
const char doc189[] PROGMEM = "(pprint item [str])\n"
"Prints its argument, using the pretty printer, to display it formatted in a structured way.\n"
"If str is specified it prints to the specified stream. It returns no value.";
const char doc190[] PROGMEM = "(pprintall [str])\n"
"Pretty-prints the definition of every function and variable defined in the uLisp workspace.\n"
"If str is specified it prints to the specified stream. It returns no value.";
const char doc191[] PROGMEM = "(require 'symbol)\n"
"Loads the definition of a function defined with defun, or a variable defined with defvar, from the Lisp Library.\n"
"It returns t if it was loaded, or nil if the symbol is already defined or isn't defined in the Lisp Library.";
const char doc192[] PROGMEM = "(list-library)\n"
"Prints a list of the functions defined in the List Library.";
const char doc193[] PROGMEM = "(? item)\n"
"Prints the documentation string of a built-in or user-defined function.";
const char doc194[] PROGMEM = "(documentation 'symbol [type])\n"
"Returns the documentation string of a built-in or user-defined function. The type argument is ignored.";
const char doc195[] PROGMEM = "(apropos item)\n"
"Prints the user-defined and built-in functions whose names contain the specified string or symbol.";
const char doc196[] PROGMEM = "(apropos-list item)\n"
"Returns a list of user-defined and built-in functions whose names contain the specified string or symbol.";
const char doc197[] PROGMEM = "(unwind-protect form1 [forms]*)\n"
"Evaluates form1 and forms in order and returns the value of form1,\n"
"but guarantees to evaluate forms even if an error occurs in form1.";
const char doc198[] PROGMEM = "(ignore-errors [forms]*)\n"
"Evaluates forms ignoring errors.";
const char doc199[] PROGMEM = "(error controlstring [arguments]*)\n"
"Signals an error. The message is printed by format using the controlstring and arguments.";
const char doc200[] PROGMEM = "(with-client (str [address port]) form*)\n"
"Evaluates the forms with str bound to a wifi-stream.";
const char doc201[] PROGMEM = "(available stream)\n"
"Returns the number of bytes available for reading from the wifi-stream, or zero if no bytes are available.";
const char doc202[] PROGMEM = "(wifi-server)\n"
"Starts a Wi-Fi server running. It returns nil.";
const char doc203[] PROGMEM = "(wifi-softap ssid [password channel hidden])\n"
"Set up a soft access point to establish a Wi-Fi network.\n"
"Returns the IP address as a string or nil if unsuccessful.";
const char doc204[] PROGMEM = "(connected stream)\n"
"Returns t or nil to indicate if the client on stream is connected.";
const char doc205[] PROGMEM = "(wifi-localip)\n"
"Returns the IP address of the local network as a string.";
const char doc206[] PROGMEM = "(wifi-connect [ssid pass])\n"
"Connects to the Wi-Fi network ssid using password pass. It returns the IP address as a string.";
const char doc207[] PROGMEM = "(with-gfx (str) form*)\n"
"Evaluates the forms with str bound to an gfx-stream so you can print text\n"
"to the graphics display using the standard uLisp print commands.";
const char doc208[] PROGMEM = "(draw-pixel x y [colour])\n"
"Draws a pixel at coordinates (x,y) in colour, or white if omitted.";
const char doc209[] PROGMEM = "(draw-line x0 y0 x1 y1 [colour])\n"
"Draws a line from (x0,y0) to (x1,y1) in colour, or white if omitted.";
const char doc210[] PROGMEM = "(draw-rect x y w h [colour])\n"
"Draws an outline rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc211[] PROGMEM = "(fill-rect x y w h [colour])\n"
"Draws a filled rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc212[] PROGMEM = "(draw-circle x y r [colour])\n"
"Draws an outline circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc213[] PROGMEM = "(fill-circle x y r [colour])\n"
"Draws a filled circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc214[] PROGMEM = "(draw-round-rect x y w h radius [colour])\n"
"Draws an outline rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc215[] PROGMEM = "(fill-round-rect x y w h radius [colour])\n"
"Draws a filled rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc216[] PROGMEM = "(draw-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws an outline triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc217[] PROGMEM = "(fill-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws a filled triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc218[] PROGMEM = "(draw-char x y char [colour background size])\n"
"Draws the character char with its top left corner at (x,y).\n"
"The character is drawn in a 5 x 7 pixel font in colour against background,\n"
"which default to white and black respectively.\n"
"The character can optionally be scaled by size.";
const char doc219[] PROGMEM = "(set-cursor x y)\n"
"Sets the start point for text plotting to (x, y).";
const char doc220[] PROGMEM = "(set-text-color colour [background])\n"
"Sets the text colour for text plotted using (with-gfx ...).";
const char doc221[] PROGMEM = "(set-text-size scale)\n"
"Scales text by the specified size, default 1.";
const char doc222[] PROGMEM = "(set-text-wrap boolean)\n"
"Specified whether text wraps at the right-hand edge of the display; the default is t.";
const char doc223[] PROGMEM = "(fill-screen [colour])\n"
"Fills or clears the screen with colour, default black.";
const char doc224[] PROGMEM = "(set-rotation option)\n"
"Sets the display orientation for subsequent graphics commands; values are 0, 1, 2, or 3.";
const char doc225[] PROGMEM = "(invert-display boolean)\n"
"Mirror-images the display.";

// Built-in symbol lookup table
const tbl_entry_t BuiltinTable[] PROGMEM = {
    { string0, NULL, MINMAX(OTHER_FORMS, 0, 0), doc0 },
    { string1, NULL, MINMAX(OTHER_FORMS, 0, 0), doc1 },
    { string2, NULL, MINMAX(OTHER_FORMS, 0, 0), doc2 },
    { string3, NULL, MINMAX(OTHER_FORMS, 0, 0), doc3 },
    { string4, NULL, MINMAX(OTHER_FORMS, 0, 0), NULL },
    { string5, NULL, MINMAX(OTHER_FORMS, 0, 0), NULL },
    { string6, NULL, MINMAX(OTHER_FORMS, 0, 0), NULL },
    { string7, NULL, MINMAX(OTHER_FORMS, 0, 0), doc7 },
    { string8, NULL, MINMAX(OTHER_FORMS, 1, UNLIMITED), doc8 },
    { string9, NULL, MINMAX(OTHER_FORMS, 1, UNLIMITED), doc9 },
    { string10, NULL, MINMAX(OTHER_FORMS, 1, UNLIMITED), doc10 },
    { string11, NULL, MINMAX(OTHER_FORMS, 1, UNLIMITED), NULL },
    { string12, NULL, MINMAX(OTHER_FORMS, 0, UNLIMITED), NULL },
    { string13, sp_quote, MINMAX(SPECIAL_FORMS, 1, 1), NULL },
    { string14, sp_defun, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), doc14 },
    { string15, sp_defvar, MINMAX(SPECIAL_FORMS, 1, 3), doc15 },
    { string16, fn_car, MINMAX(FUNCTIONS, 1, 1), doc16 },
    { string17, fn_car, MINMAX(FUNCTIONS, 1, 1), NULL },
    { string18, fn_cdr, MINMAX(FUNCTIONS, 1, 1), doc18 },
    { string19, fn_cdr, MINMAX(FUNCTIONS, 1, 1), NULL },
    { string20, fn_nth, MINMAX(FUNCTIONS, 2, 2), doc20 },
    { string21, fn_aref, MINMAX(FUNCTIONS, 2, UNLIMITED), doc21 },
    { string22, fn_stringfn, MINMAX(FUNCTIONS, 1, 1), doc22 },
    { string23, fn_pinmode, MINMAX(FUNCTIONS, 2, 2), doc23 },
    { string24, fn_digitalwrite, MINMAX(FUNCTIONS, 2, 2), doc24 },
    { string25, fn_analogread, MINMAX(FUNCTIONS, 1, 1), doc25 },
    { string26, fn_register, MINMAX(FUNCTIONS, 1, 2), doc26 },
    { string27, fn_format, MINMAX(FUNCTIONS, 2, UNLIMITED), doc27 },
    { string28, sp_or, MINMAX(SPECIAL_FORMS, 0, UNLIMITED), doc28 },
    { string29, sp_setq, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), doc29 },
    { string30, sp_loop, MINMAX(SPECIAL_FORMS, 0, UNLIMITED), doc30 },
    { string31, sp_return, MINMAX(SPECIAL_FORMS, 0, UNLIMITED), doc31 },
    { string32, sp_push, MINMAX(SPECIAL_FORMS, 2, 2), doc32 },
    { string33, sp_pop, MINMAX(SPECIAL_FORMS, 1, 1), doc33 },
    { string34, sp_incf, MINMAX(SPECIAL_FORMS, 1, 2), doc34 },
    { string35, sp_decf, MINMAX(SPECIAL_FORMS, 1, 2), doc35 },
    { string36, sp_setf, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), doc36 },
    { string37, sp_dolist, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc37 },
    { string38, sp_dotimes, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc38 },
    { string39, sp_trace, MINMAX(SPECIAL_FORMS, 0, 1), doc39 },
    { string40, sp_untrace, MINMAX(SPECIAL_FORMS, 0, 1), doc40 },
    { string41, sp_formillis, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc41 },
    { string42, sp_time, MINMAX(SPECIAL_FORMS, 1, 1), doc42 },
    { string43, sp_withoutputtostring, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc43 },
    { string44, sp_withserial, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc44 },
    { string45, sp_withi2c, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc45 },
    { string46, sp_withspi, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc46 },
    { string47, sp_withsdcard, MINMAX(SPECIAL_FORMS, 2, UNLIMITED), doc47 },
    { string48, tf_progn, MINMAX(TAIL_FORMS, 0, UNLIMITED), doc48 },
    { string49, tf_if, MINMAX(TAIL_FORMS, 2, 3), doc49 },
    { string50, tf_cond, MINMAX(TAIL_FORMS, 0, UNLIMITED), doc50 },
    { string51, tf_when, MINMAX(TAIL_FORMS, 1, UNLIMITED), doc51 },
    { string52, tf_unless, MINMAX(TAIL_FORMS, 1, UNLIMITED), doc52 },
    { string53, tf_case, MINMAX(TAIL_FORMS, 1, UNLIMITED), doc53 },
    { string54, tf_and, MINMAX(TAIL_FORMS, 0, UNLIMITED), doc54 },
    { string55, fn_not, MINMAX(FUNCTIONS, 1, 1), doc55 },
    { string56, fn_not, MINMAX(FUNCTIONS, 1, 1), NULL },
    { string57, fn_cons, MINMAX(FUNCTIONS, 2, 2), doc57 },
    { string58, fn_atom, MINMAX(FUNCTIONS, 1, 1), doc58 },
    { string59, fn_listp, MINMAX(FUNCTIONS, 1, 1), doc59 },
    { string60, fn_consp, MINMAX(FUNCTIONS, 1, 1), doc60 },
    { string61, fn_symbolp, MINMAX(FUNCTIONS, 1, 1), doc61 },
    { string62, fn_arrayp, MINMAX(FUNCTIONS, 1, 1), doc62 },
    { string63, fn_boundp, MINMAX(FUNCTIONS, 1, 1), doc63 },
    { string64, fn_keywordp, MINMAX(FUNCTIONS, 1, 1), doc64 },
    { string65, fn_setfn, MINMAX(FUNCTIONS, 2, UNLIMITED), doc65 },
    { string66, fn_streamp, MINMAX(FUNCTIONS, 1, 1), doc66 },
    { string67, fn_eq, MINMAX(FUNCTIONS, 2, 2), doc67 },
    { string68, fn_equal, MINMAX(FUNCTIONS, 2, 2), doc68 },
    { string69, fn_caar, MINMAX(FUNCTIONS, 1, 1), doc69 },
    { string70, fn_cadr, MINMAX(FUNCTIONS, 1, 1), doc70 },
    { string71, fn_cadr, MINMAX(FUNCTIONS, 1, 1), NULL },
    { string72, fn_cdar, MINMAX(FUNCTIONS, 1, 1), doc72 },
    { string73, fn_cddr, MINMAX(FUNCTIONS, 1, 1), doc73 },
    { string74, fn_caaar, MINMAX(FUNCTIONS, 1, 1), doc74 },
    { string75, fn_caadr, MINMAX(FUNCTIONS, 1, 1), doc75 },
    { string76, fn_cadar, MINMAX(FUNCTIONS, 1, 1), doc76 },
    { string77, fn_caddr, MINMAX(FUNCTIONS, 1, 1), doc77 },
    { string78, fn_caddr, MINMAX(FUNCTIONS, 1, 1), NULL },
    { string79, fn_cdaar, MINMAX(FUNCTIONS, 1, 1), doc79 },
    { string80, fn_cdadr, MINMAX(FUNCTIONS, 1, 1), doc80 },
    { string81, fn_cddar, MINMAX(FUNCTIONS, 1, 1), doc81 },
    { string82, fn_cdddr, MINMAX(FUNCTIONS, 1, 1), doc82 },
    { string83, fn_length, MINMAX(FUNCTIONS, 1, 1), doc83 },
    { string84, fn_arraydimensions, MINMAX(FUNCTIONS, 1, 1), doc84 },
    { string85, fn_list, MINMAX(FUNCTIONS, 0, UNLIMITED), doc85 },
    { string86, fn_makearray, MINMAX(FUNCTIONS, 1, 5), doc86 },
    { string87, fn_reverse, MINMAX(FUNCTIONS, 1, 1), doc87 },
    { string88, fn_assoc, MINMAX(FUNCTIONS, 2, 2), doc88 },
    { string89, fn_member, MINMAX(FUNCTIONS, 2, 2), doc89 },
    { string90, fn_apply, MINMAX(FUNCTIONS, 2, UNLIMITED), doc90 },
    { string91, fn_funcall, MINMAX(FUNCTIONS, 1, UNLIMITED), doc91 },
    { string92, fn_append, MINMAX(FUNCTIONS, 0, UNLIMITED), doc92 },
    { string93, fn_mapc, MINMAX(FUNCTIONS, 2, UNLIMITED), doc93 },
    { string94, fn_mapcar, MINMAX(FUNCTIONS, 2, UNLIMITED), doc94 },
    { string95, fn_mapcan, MINMAX(FUNCTIONS, 2, UNLIMITED), doc95 },
    { string96, fn_add, MINMAX(FUNCTIONS, 0, UNLIMITED), doc96 },
    { string97, fn_subtract, MINMAX(FUNCTIONS, 1, UNLIMITED), doc97 },
    { string98, fn_multiply, MINMAX(FUNCTIONS, 0, UNLIMITED), doc98 },
    { string99, fn_divide, MINMAX(FUNCTIONS, 1, UNLIMITED), doc99 },
    { string100, fn_mod, MINMAX(FUNCTIONS, 2, 2), doc100 },
    { string101, fn_oneplus, MINMAX(FUNCTIONS, 1, 1), doc101 },
    { string102, fn_oneminus, MINMAX(FUNCTIONS, 1, 1), doc102 },
    { string103, fn_abs, MINMAX(FUNCTIONS, 1, 1), doc103 },
    { string104, fn_random, MINMAX(FUNCTIONS, 1, 1), doc104 },
    { string105, fn_maxfn, MINMAX(FUNCTIONS, 1, UNLIMITED), doc105 },
    { string106, fn_minfn, MINMAX(FUNCTIONS, 1, UNLIMITED), doc106 },
    { string107, fn_noteq, MINMAX(FUNCTIONS, 1, UNLIMITED), doc107 },
    { string108, fn_numeq, MINMAX(FUNCTIONS, 1, UNLIMITED), doc108 },
    { string109, fn_less, MINMAX(FUNCTIONS, 1, UNLIMITED), doc109 },
    { string110, fn_lesseq, MINMAX(FUNCTIONS, 1, UNLIMITED), doc110 },
    { string111, fn_greater, MINMAX(FUNCTIONS, 1, UNLIMITED), doc111 },
    { string112, fn_greatereq, MINMAX(FUNCTIONS, 1, UNLIMITED), doc112 },
    { string113, fn_plusp, MINMAX(FUNCTIONS, 1, 1), doc113 },
    { string114, fn_minusp, MINMAX(FUNCTIONS, 1, 1), doc114 },
    { string115, fn_zerop, MINMAX(FUNCTIONS, 1, 1), doc115 },
    { string116, fn_oddp, MINMAX(FUNCTIONS, 1, 1), doc116 },
    { string117, fn_evenp, MINMAX(FUNCTIONS, 1, 1), doc117 },
    { string118, fn_integerp, MINMAX(FUNCTIONS, 1, 1), doc118 },
    { string119, fn_numberp, MINMAX(FUNCTIONS, 1, 1), doc119 },
    { string120, fn_floatfn, MINMAX(FUNCTIONS, 1, 1), doc120 },
    { string121, fn_floatp, MINMAX(FUNCTIONS, 1, 1), doc121 },
    { string122, fn_sin, MINMAX(FUNCTIONS, 1, 1), doc122 },
    { string123, fn_cos, MINMAX(FUNCTIONS, 1, 1), doc123 },
    { string124, fn_tan, MINMAX(FUNCTIONS, 1, 1), doc124 },
    { string125, fn_asin, MINMAX(FUNCTIONS, 1, 1), doc125 },
    { string126, fn_acos, MINMAX(FUNCTIONS, 1, 1), doc126 },
    { string127, fn_atan, MINMAX(FUNCTIONS, 1, 2), doc127 },
    { string128, fn_sinh, MINMAX(FUNCTIONS, 1, 1), doc128 },
    { string129, fn_cosh, MINMAX(FUNCTIONS, 1, 1), doc129 },
    { string130, fn_tanh, MINMAX(FUNCTIONS, 1, 1), doc130 },
    { string131, fn_exp, MINMAX(FUNCTIONS, 1, 1), doc131 },
    { string132, fn_sqrt, MINMAX(FUNCTIONS, 1, 1), doc132 },
    { string133, fn_log, MINMAX(FUNCTIONS, 1, 2), doc133 },
    { string134, fn_expt, MINMAX(FUNCTIONS, 2, 2), doc134 },
    { string135, fn_ceiling, MINMAX(FUNCTIONS, 1, 2), doc135 },
    { string136, fn_floor, MINMAX(FUNCTIONS, 1, 2), doc136 },
    { string137, fn_truncate, MINMAX(FUNCTIONS, 1, 2), doc137 },
    { string138, fn_round, MINMAX(FUNCTIONS, 1, 2), doc138 },
    { string139, fn_char, MINMAX(FUNCTIONS, 2, 2), doc139 },
    { string140, fn_charcode, MINMAX(FUNCTIONS, 1, 1), doc140 },
    { string141, fn_codechar, MINMAX(FUNCTIONS, 1, 1), doc141 },
    { string142, fn_characterp, MINMAX(FUNCTIONS, 1, 1), doc142 },
    { string143, fn_stringp, MINMAX(FUNCTIONS, 1, 1), doc143 },
    { string144, fn_stringeq, MINMAX(FUNCTIONS, 2, 2), doc144 },
    { string145, fn_stringless, MINMAX(FUNCTIONS, 2, 2), doc145 },
    { string146, fn_stringgreater, MINMAX(FUNCTIONS, 2, 2), doc146 },
    { string147, fn_sort, MINMAX(FUNCTIONS, 2, 2), doc147 },
    { string148, fn_concatenate, MINMAX(FUNCTIONS, 1, UNLIMITED), doc148 },
    { string149, fn_subseq, MINMAX(FUNCTIONS, 2, 3), doc149 },
    { string150, fn_search, MINMAX(FUNCTIONS, 2, 2), doc150 },
    { string151, fn_readfromstring, MINMAX(FUNCTIONS, 1, 1), doc151 },
    { string152, fn_princtostring, MINMAX(FUNCTIONS, 1, 1), doc152 },
    { string153, fn_prin1tostring, MINMAX(FUNCTIONS, 1, 1), doc153 },
    { string154, fn_logand, MINMAX(FUNCTIONS, 0, UNLIMITED), doc154 },
    { string155, fn_logior, MINMAX(FUNCTIONS, 0, UNLIMITED), doc155 },
    { string156, fn_logxor, MINMAX(FUNCTIONS, 0, UNLIMITED), doc156 },
    { string157, fn_lognot, MINMAX(FUNCTIONS, 1, 1), doc157 },
    { string158, fn_ash, MINMAX(FUNCTIONS, 2, 2), doc158 },
    { string159, fn_logbitp, MINMAX(FUNCTIONS, 2, 2), doc159 },
    { string160, fn_eval, MINMAX(FUNCTIONS, 1, 1), doc160 },
    { string161, fn_globals, MINMAX(FUNCTIONS, 0, 0), doc161 },
    { string162, fn_locals, MINMAX(FUNCTIONS, 0, 0), doc162 },
    { string163, fn_makunbound, MINMAX(FUNCTIONS, 1, 1), doc163 },
    { string164, fn_break, MINMAX(FUNCTIONS, 0, 0), doc164 },
    { string165, fn_read, MINMAX(FUNCTIONS, 0, 1), doc165 },
    { string166, fn_prin1, MINMAX(FUNCTIONS, 1, 2), doc166 },
    { string167, fn_print, MINMAX(FUNCTIONS, 1, 2), doc167 },
    { string168, fn_princ, MINMAX(FUNCTIONS, 1, 2), doc168 },
    { string169, fn_terpri, MINMAX(FUNCTIONS, 0, 1), doc169 },
    { string170, fn_readbyte, MINMAX(FUNCTIONS, 0, 2), doc170 },
    { string171, fn_readline, MINMAX(FUNCTIONS, 0, 1), doc171 },
    { string172, fn_writebyte, MINMAX(FUNCTIONS, 1, 2), doc172 },
    { string173, fn_writestring, MINMAX(FUNCTIONS, 1, 2), doc173 },
    { string174, fn_writeline, MINMAX(FUNCTIONS, 1, 2), doc174 },
    { string175, fn_restarti2c, MINMAX(FUNCTIONS, 1, 2), doc175 },
    { string176, fn_gc, MINMAX(FUNCTIONS, 0, 0), doc176 },
    { string177, fn_room, MINMAX(FUNCTIONS, 0, 0), doc177 },
    { string180, fn_cls, MINMAX(FUNCTIONS, 0, 0), doc180 },
    { string181, fn_digitalread, MINMAX(FUNCTIONS, 1, 1), doc181 },
    { string182, fn_analogreadresolution, MINMAX(FUNCTIONS, 1, 1), doc182 },
    { string183, fn_analogwrite, MINMAX(FUNCTIONS, 2, 2), doc183 },
    { string184, fn_delay, MINMAX(FUNCTIONS, 1, 1), doc184 },
    { string185, fn_millis, MINMAX(FUNCTIONS, 0, 0), doc185 },
    { string186, fn_sleep, MINMAX(FUNCTIONS, 0, 1), doc186 },
    { string187, fn_note, MINMAX(FUNCTIONS, 0, 3), doc187 },
    { string188, fn_edit, MINMAX(FUNCTIONS, 1, 1), doc188 },
    { string189, fn_pprint, MINMAX(FUNCTIONS, 1, 2), doc189 },
    { string190, fn_pprintall, MINMAX(FUNCTIONS, 0, 1), doc190 },
    { string191, fn_require, MINMAX(FUNCTIONS, 1, 1), doc191 },
    { string192, fn_listlibrary, MINMAX(FUNCTIONS, 0, 0), doc192 },
    { string193, sp_help, MINMAX(SPECIAL_FORMS, 1, 1), doc193 },
    { string194, fn_documentation, MINMAX(FUNCTIONS, 1, 2), doc194 },
    { string195, fn_apropos, MINMAX(FUNCTIONS, 1, 1), doc195 },
    { string196, fn_aproposlist, MINMAX(FUNCTIONS, 1, 1), doc196 },
    { string197, sp_unwindprotect, MINMAX(SPECIAL_FORMS, 0, UNLIMITED), doc197 },
    { string198, sp_ignoreerrors, MINMAX(SPECIAL_FORMS, 0, UNLIMITED), doc198 },
    { string199, sp_error, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc199 },
    { string200, sp_withclient, MINMAX(SPECIAL_FORMS, 1, 2), doc200 },
    { string201, fn_available, MINMAX(FUNCTIONS, 1, 1), doc201 },
    { string202, fn_wifiserver, MINMAX(FUNCTIONS, 0, 0), doc202 },
    { string203, fn_wifisoftap, MINMAX(FUNCTIONS, 0, 4), doc203 },
    { string204, fn_connected, MINMAX(FUNCTIONS, 1, 1), doc204 },
    { string205, fn_wifilocalip, MINMAX(FUNCTIONS, 0, 0), doc205 },
    { string206, fn_wificonnect, MINMAX(FUNCTIONS, 0, 3), doc206 },
    { string207, sp_withgfx, MINMAX(SPECIAL_FORMS, 1, UNLIMITED), doc207 },
    { string208, fn_drawpixel, MINMAX(FUNCTIONS, 2, 3), doc208 },
    { string209, fn_drawline, MINMAX(FUNCTIONS, 4, 5), doc209 },
    { string210, fn_drawrect, MINMAX(FUNCTIONS, 4, 5), doc210 },
    { string211, fn_fillrect, MINMAX(FUNCTIONS, 4, 5), doc211 },
    { string212, fn_drawcircle, MINMAX(FUNCTIONS, 3, 4), doc212 },
    { string213, fn_fillcircle, MINMAX(FUNCTIONS, 3, 4), doc213 },
    { string214, fn_drawroundrect, MINMAX(FUNCTIONS, 5, 6), doc214 },
    { string215, fn_fillroundrect, MINMAX(FUNCTIONS, 5, 6), doc215 },
    { string216, fn_drawtriangle, MINMAX(FUNCTIONS, 6, 7), doc216 },
    { string217, fn_filltriangle, MINMAX(FUNCTIONS, 6, 7), doc217 },
    { string218, fn_drawchar, MINMAX(FUNCTIONS, 3, 6), doc218 },
    { string219, fn_setcursor, MINMAX(FUNCTIONS, 2, 2), doc219 },
    { string220, fn_settextcolor, MINMAX(FUNCTIONS, 1, 2), doc220 },
    { string221, fn_settextsize, MINMAX(FUNCTIONS, 1, 1), doc221 },
    { string222, fn_settextwrap, MINMAX(FUNCTIONS, 1, 1), doc222 },
    { string223, fn_fillscreen, MINMAX(FUNCTIONS, 0, 1), doc223 },
    { string224, fn_setrotation, MINMAX(FUNCTIONS, 1, 1), doc224 },
    { string225, fn_invertdisplay, MINMAX(FUNCTIONS, 1, 1), doc225 },
    { string226, (fn_ptr_type)LED_BUILTIN, 0, NULL },
    { string227, (fn_ptr_type)HIGH, DIGITALWRITE, NULL },
    { string228, (fn_ptr_type)LOW, DIGITALWRITE, NULL },
    { string229, (fn_ptr_type)INPUT, PINMODE, NULL },
    { string230, (fn_ptr_type)INPUT_PULLUP, PINMODE, NULL },
    { string231, (fn_ptr_type)INPUT_PULLDOWN, PINMODE, NULL },
    { string232, (fn_ptr_type)OUTPUT, PINMODE, NULL },
};

// Metatable cross-reference functions

const tbl_entry_t *table (int n) {
    return Metatable[n].table;
}

size_t tablesize (int n) {
    return Metatable[n].size;
}

void inittables () {
    Metatable = (mtbl_entry_t*)calloc(1, sizeof(mtbl_entry_t));
    NumTables = 1;
    Metatable[0].table = BuiltinTable;
    Metatable[0].size = arraysize(BuiltinTable);
}

void addtable (const tbl_entry_t table) {
    NumTables++;
    Metatable = (mtbl_entry_t*)realloc(Metatable, NumTables * sizeof(mtbl_entry_t));
    Metatable[NumTables-1].table = table;
    Metatable[NumTables-1].size = arraysize(table);
}

tbl_entry_t* getentry(builtin_t x) {
    int t = 0;
    while (x >= tablesize(t)) {
        x -= tablesize(t);
        t++;
    }
    return &table(t)[x]
}

// Table lookup functions

/*
    lookupbuiltin - looks up a string in BuiltinTable[], and returns the index of its entry,
    or ENDFUNCTIONS if no match is found
*/
builtin_t lookupbuiltin (char* c) {
    unsigned int end = 0, start;
    for (int n=0; n<NumTables; n++) {
        start = end;
        int entries = tablesize(n);
        end = end + entries;
        for (int i=0; i<entries; i++) {
            if (strcasecmp_P(c, (char*)pgm_read_ptr(&(table(n)[i].string))) == 0) {
                return (builtin_t)(start + i);
            }
        }
    }
    return ENDFUNCTIONS;
}

/*
    lookupfn - looks up the entry for name in BuiltinTable[], and returns the function entry point
*/
intptr_t lookupfn (builtin_t name) {
    return (intptr_t)pgm_read_ptr(getentry(name)->fptr);
}

/*
    getminmax - gets the minmax byte from BuiltinTable[] whose octets specify the type of function
    and minimum and maximum number of arguments for name
*/
minmax_t getminmax (builtin_t name) {
    return pgm_read_byte(getentry(name)->minmax);
}

/*
    checkminmax - checks that the number of arguments nargs for name is within the range specified by minmax
*/
void checkminmax (builtin_t name, int nargs) {
    if (name >= ENDFUNCTIONS) error2(PSTR("internal error: not a builtin"));
    minmax_t minmax = getminmax(name);
    if (nargs < getminargs(minmax)) error2(toofewargs);
    if (!unlimitedp(minmax) && nargs > getmaxargs(minmax)) error2(toomanyargs);
}

/*
    lookupdoc - looks up the documentation string for the built-in function name
*/
char* lookupdoc (builtin_t name) {
    return (char*)pgm_read_ptr(getentry(name)->doc);
}

/*
    findsubstring - tests whether a specified substring occurs in the name of a built-in function
*/
bool findsubstring (char* part, builtin_t name) {
    PGM_P s = (char*)pgm_read_ptr(getentry(name)->string);
    int l = strlen_P(s);
    int m = strlen(part);
    for (int i = 0; i <= l-m; i++) {
        int j = 0;
        while (j < m && pgm_read_byte(&s[i+j]) == part[j]) j++;
        if (j == m) return true;
    }
    return false;
}

/*
    testescape - tests whether the '~' escape character has been typed
*/
void testescape () {
    if (Serial.read() == '~') error2(PSTR("escape!"));
}

/*
    keywordp - check that obj is a keyword
*/
bool keywordp (object* obj) {
    if (!(symbolp(obj) && builtinp(obj->name))) return false;
    builtin_t name = builtin(obj->name);
    PGM_P s = (char*)pgm_read_ptr(getentry(name)->string);
    char c = pgm_read_byte(&s[0]);
    return (c == ':');
}

// Main evaluator

/*
    eval - the main Lisp evaluator
*/
object* eval (object* form, object* env) {
    int TC=0;
    EVAL:
    // Enough space?
    if (Freespace <= WORKSPACESIZE>>4) gc(form, env);
    // Escape
    if (tstflag(ESCAPE)) { clrflag(ESCAPE); error2(PSTR("escape!"));}
    if (!tstflag(NOESC)) testescape();

    if (form == NULL) return nil;

    if (form->type >= NUMBER && form->type <= STRING) return form;

    if (symbolp(form)) {
        symbol_t name = form->name;
        object* pair = value(name, env);
        if (pair != NULL) return cdr(pair);
        pair = value(name, GlobalEnv);
        if (pair != NULL) return cdr(pair);
        else if (builtinp(name)) return form;
        error(PSTR("undefined"), form);
    }

    // It's a list
    object* function = car(form);
    object* args = cdr(form);

    if (function == NULL) error(PSTR("illegal function"), nil);
    if (!listp(args)) error(PSTR("can't evaluate a dotted pair"), args);

    // List starts with a builtin symbol?
    if (symbolp(function) && builtinp(function->name)) {
        builtin_t name = builtin(function->name);

        if ((name == LET) || (name == LETSTAR)) {
            int TCstart = TC;
            if (args == NULL) error2(noargument);
            object* assigns = first(args);
            if (!listp(assigns)) error(notalist, assigns);
            object* forms = cdr(args);
            object* newenv = env;
            push(newenv, GCStack);
            while (assigns != NULL) {
                object* assign = car(assigns);
                if (!consp(assign)) push(cons(assign,nil), newenv);
                else if (cdr(assign) == NULL) push(cons(first(assign),nil), newenv);
                else push(cons(first(assign),eval(second(assign),env)), newenv);
                car(GCStack) = newenv;
                if (name == LETSTAR) env = newenv;
                assigns = cdr(assigns);
            }
            env = newenv;
            pop(GCStack);
            form = tf_progn(forms,env);
            TC = TCstart;
            goto EVAL;
        }

        if (name == LAMBDA) {
            if (env == NULL) return form;
            object* envcopy = NULL;
            while (env != NULL) {
                object* pair = first(env);
                if (pair != NULL) push(pair, envcopy);
                env = cdr(env);
            }
            return cons(bsymbol(CLOSURE), cons(envcopy,args));
        }
        uint8_t ft = fntype(getminmax(name));

        if (ft == SPECIAL_FORMS) {
            Context = name;
            return ((fn_ptr_type)lookupfn(name))(args, env);
        }

        if (ft == TAIL_FORMS) {
            Context = name;
            form = ((fn_ptr_type)lookupfn(name))(args, env);
            TC = 1;
            goto EVAL;
        }
        if (ft == OTHER_FORMS) error(PSTR("can't be used as a function"), function);
    }

    // Evaluate the parameters - result in head
    object* fname = car(form);
    int TCstart = TC;
    object* head = cons(eval(fname, env), NULL);
    push(head, GCStack); // Don't GC the result list
    object* tail = head;
    form = cdr(form);
    int nargs = 0;

    while (form != NULL){
        object* obj = cons(eval(car(form),env),NULL);
        cdr(tail) = obj;
        tail = obj;
        form = cdr(form);
        nargs++;
    }

    function = car(head);
    args = cdr(head);

    if (symbolp(function)) {
        builtin_t bname = builtin(function->name);
        if (!builtinp(function->name)) error(PSTR("not valid here"), fname);
        Context = bname;
        checkminmax(bname, nargs);
        object* result = ((fn_ptr_type)lookupfn(bname))(args, env);
        pop(GCStack);
        return result;
    }

    if (consp(function)) {
        symbol_t name = sym(NIL);
        if (!listp(fname)) name = fname->name;

        if (isbuiltin(car(function), LAMBDA)) {
            form = closure(TCstart, name, function, args, &env);
            pop(GCStack);
            int trace = tracing(fname->name);
            if (trace) {
                object* result = eval(form, env);
                indent((--(TraceDepth[trace-1]))<<1, ' ', pserial);
                pint(TraceDepth[trace-1], pserial);
                pserial(':'); pserial(' ');
                printobject(fname, pserial); pfstring(PSTR(" returned "), pserial);
                printobject(result, pserial); pln(pserial);
                return result;
            } else {
                TC = 1;
                goto EVAL;
            }
        }

        if (isbuiltin(car(function), CLOSURE)) {
            function = cdr(function);
            form = closure(TCstart, name, function, args, &env);
            pop(GCStack);
            TC = 1;
            goto EVAL;
        }

    }
    error(PSTR("illegal function"), fname); return nil;
}

// Print functions

/*
    pserial - prints a character to the serial port
*/
void pserial (char c) {
    LastPrint = c;
    if (c == '\n') Serial.write('\r');
    Serial.write(c);
}

const char ControlCodes[] PROGMEM = "Null\0SOH\0STX\0ETX\0EOT\0ENQ\0ACK\0Bell\0Backspace\0Tab\0Newline\0VT\0"
"Page\0Return\0SO\0SI\0DLE\0DC1\0DC2\0DC3\0DC4\0NAK\0SYN\0ETB\0CAN\0EM\0SUB\0Escape\0FS\0GS\0RS\0US\0Space\0";

/*
    pcharacter - prints a character to a stream, escaping special characters if PRINTREADABLY is false
    If <= 32 prints character name; eg #\Space
    If < 127 prints ASCII; eg #\A
    Otherwise prints decimal; eg #\234
*/
void pcharacter (char c, pfun_t pfun) {
    if (!tstflag(PRINTREADABLY)) pfun(c);
    else {
        pfun('#'); pfun('\\');
        if (c <= 32) {
            PGM_P p = ControlCodes;
            while (c > 0) {p = p + strlen_P(p) + 1; c--; }
            pfstring(p, pfun);
        } else if (c < 127) pfun(c);
        else pint(c, pfun);
    }
}

/*
    pstring - prints a C string to the specified stream
*/
void pstring (char* s, pfun_t pfun) {
    while (*s) pfun(*s++);
}

/*
    plispstring - prints a Lisp string object to the specified stream
*/
void plispstring (object* form, pfun_t pfun) {
    plispstr(form->name, pfun);
}

/*
    plispstr - prints a Lisp string name to the specified stream
*/
void plispstr (symbol_t name, pfun_t pfun) {
    object* form = (object*)name;
    while (form != NULL) {
        int chars = form->chars;
        for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
            char ch = chars>>i & 0xFF;
            if (tstflag(PRINTREADABLY) && (ch == '"' || ch == '\\')) pfun('\\');
            if (ch) pfun(ch);
        }
        form = car(form);
    }
}

/*
    printstring - prints a Lisp string object to the specified stream
    taking account of the PRINTREADABLY flag
*/
void printstring (object* form, pfun_t pfun) {
    if (tstflag(PRINTREADABLY)) pfun('"');
    plispstr(form->name, pfun);
    if (tstflag(PRINTREADABLY)) pfun('"');
}

/*
    pbuiltin - prints a built-in symbol to the specified stream
*/
void pbuiltin (builtin_t name, pfun_t pfun) {
    int p = 0;
    PGM_P s = (char*)pgm_read_ptr(getentry(name)->string); 
    while (1) {
        char c = pgm_read_byte(&s[p++]);
        if (c == 0) return;
        pfun(c);
    }
}

/*
    pradix40 - prints a radix 40 symbol to the specified stream
*/
void pradix40 (symbol_t name, pfun_t pfun) {
    uint32_t x = untwist(name);
    for (int d=102400000; d>0; d = d/40) {
        uint32_t j = x/d;
        char c = fromradix40(j);
        if (c == 0) return;
        pfun(c); x = x - j*d;
    }
}

/*
    printsymbol - prints any symbol from a symbol object to the specified stream
*/
void printsymbol (object* form, pfun_t pfun) {
    psymbol(form->name, pfun);
}

/*
    psymbol - prints any symbol from a symbol name to the specified stream
*/
void psymbol (symbol_t name, pfun_t pfun) {
    if ((name & 0x03) == 0) plispstr(name, pfun);
    else {
        uint32_t value = untwist(name);
        if (value < PACKEDS) error2(PSTR("invalid symbol"));
        else if (value >= BUILTINS) pbuiltin((builtin_t)(value-BUILTINS), pfun);
        else pradix40(name, pfun);
    }
}

/*
    pfstring - prints a string from flash memory to the specified stream
*/
void pfstring (PGM_P s, pfun_t pfun) {
    int p = 0;
    while (1) {
        char c = pgm_read_byte(&s[p++]);
        if (c == 0) return;
        pfun(c);
    }
}

/*
    pint - prints an integer in decimal to the specified stream
*/
void pint (int i, pfun_t pfun) {
    uint32_t j = i;
    if (i<0) { pfun('-'); j=-i; }
    pintbase(j, 10, pfun);
}

/*
    pintbase - prints an integer in base 'base' to the specified stream
*/
void pintbase (uint32_t i, uint8_t base, pfun_t pfun) {
    int lead = 0; uint32_t p = 1000000000;
    if (base == 2) p = 0x80000000; else if (base == 16) p = 0x10000000;
    for (uint32_t d=p; d>0; d=d/base) {
        uint32_t j = i/d;
        if (j!=0 || lead || d==1) { pfun((j<10) ? j+'0' : j+'W'); lead=1;}
        i = i - j*d;
    }
}

/*
    pmantissa - prints the mantissa of a floating-point number to the specified stream
*/
void pmantissa (float f, pfun_t pfun) {
    int sig = floor(log10(f));
    int mul = pow(10, 5 - sig);
    int i = round(f * mul);
    bool point = false;
    if (i == 1000000) { i = 100000; sig++; }
    if (sig < 0) {
        pfun('0'); pfun('.'); point = true;
        for (int j=0; j < - sig - 1; j++) pfun('0');
    }
    mul = 100000;
    for (int j=0; j<7; j++) {
        int d = (int)(i / mul);
        pfun(d + '0');
        i = i - d * mul;
        if (i == 0) {
            if (!point) {
                for (int k=j; k<sig; k++) pfun('0');
                pfun('.'); pfun('0');
            }
            return;
        }
        if (j == sig && sig >= 0) { pfun('.'); point = true; }
        mul = mul / 10;
    }
}

/*
    pfloat - prints a floating-point number to the specified stream
*/
void pfloat (float f, pfun_t pfun) {
    if (isnan(f)) { pfstring(PSTR("NaN"), pfun); return; }
    if (f == 0.0) { pfun('0'); return; }
    if (isinf(f)) { pfstring(PSTR("Inf"), pfun); return; }
    if (f < 0) { pfun('-'); f = -f; }
    // Calculate exponent
    int e = 0;
    if (f < 1e-3 || f >= 1e5) {
        e = floor(log(f) / 2.302585); // log10 gives wrong result
        f = f / pow(10, e);
    }

    pmantissa (f, pfun);

    // Exponent
    if (e != 0) {
        pfun('e');
        pint(e, pfun);
    }
}

/*
    pln - prints a newline to the specified stream
*/
inline void pln (pfun_t pfun) {
    pfun('\n');
}

/*
    pfl - prints a newline to the specified stream if a newline has not just been printed
*/
void pfl (pfun_t pfun) {
    if (LastPrint != '\n') pfun('\n');
}

/*
    plist - prints a list to the specified stream
*/
void plist (object* form, pfun_t pfun) {
    pfun('(');
    printobject(car(form), pfun);
    form = cdr(form);
    while (form != NULL && listp(form)) {
        pfun(' ');
        printobject(car(form), pfun);
        form = cdr(form);
    }
    if (form != NULL) {
        pfstring(PSTR(" . "), pfun);
        printobject(form, pfun);
    }
    pfun(')');
}

/*
    pstream - prints a stream name to the specified stream
*/
void pstream (object* form, pfun_t pfun) {
    pfun('<');
    PGM_P s = (char*)pgm_read_ptr(&streamname[(form->integer)>>8]);
    pfstring(s, pfun);
    pfstring(PSTR("-stream "), pfun);
    pint(form->integer & 0xFF, pfun);
    pfun('>');
}

/*
    printobject - prints any Lisp object to the specified stream
*/
void printobject (object* form, pfun_t pfun) {
    if (form == NULL) pfstring(PSTR("nil"), pfun);
    else if (listp(form) && isbuiltin(car(form), CLOSURE)) pfstring(PSTR("<closure>"), pfun);
    else if (listp(form)) plist(form, pfun);
    else if (integerp(form)) pint(form->integer, pfun);
    else if (floatp(form)) pfloat(form->single_float, pfun);
    else if (symbolp(form)) { if (form->name != sym(NOTHING)) printsymbol(form, pfun); }
    else if (characterp(form)) pcharacter(form->chars, pfun);
    else if (stringp(form)) printstring(form, pfun);
    else if (arrayp(form)) printarray(form, pfun);
    else if (streamp(form)) pstream(form, pfun);
    else error2(PSTR("internal error in print"));
}

/*
    prin1object - prints any Lisp object to the specified stream escaping special characters
*/
void prin1object (object* form, pfun_t pfun) {
    char temp = Flags;
    clrflag(PRINTREADABLY);
    printobject(form, pfun);
    Flags = temp;
}

// Read functions

/*
    glibrary - reads a character from the Lisp Library
*/
int glibrary () {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    char c = pgm_read_byte(&LispLibrary[GlobalStringIndex++]);
    return (c != 0) ? c : -1; // -1?
}

/*
    loadfromlibrary - reads and evaluates a form from the Lisp Library
*/
void loadfromlibrary (object* env) {
    GlobalStringIndex = 0;
    object* line = read(glibrary);
    while (line != NULL) {
        push(line, GCStack);
        eval(line, env);
        pop(GCStack);
        line = read(glibrary);
    }
}

/*
    gserial - gets a character from the serial port
*/
int gserial () {
    if (LastChar) {
        char temp = LastChar;
        LastChar = 0;
        return temp;
    }
    unsigned long start = millis();
    while (!Serial.available()) { delay(1); if (millis() - start > 1000) clrflag(NOECHO); }
    char temp = Serial.read();
    if (temp != '\n' && !tstflag(NOECHO)) pserial(temp);
    return temp;
}

/*
    nextitem - reads the next token from the specified stream
*/
object* nextitem (gfun_t gfun) {
    int ch = gfun();
    while(issp(ch)) ch = gfun();

    if (ch == ';') {
        do { ch = gfun(); if (ch == ';' || ch == '(') setflag(NOECHO); }
        while(ch != '(');
    }
    if (ch == '\n') ch = gfun();
    if (ch == -1) return nil;
    if (ch == ')') return (object*)KET;
    if (ch == '(') return (object*)BRA;
    if (ch == '\'') return (object*)QUO;

    // Parse string
    if (ch == '"') return readstring('"', gfun);

    // Parse symbol, character, or number
    int index = 0, base = 10, sign = 1;
    char buffer[BUFFERSIZE];
    int bufmax = BUFFERSIZE-3; // Max index
    unsigned int result = 0;
    bool isfloat = false;
    float fresult = 0.0;

    if (ch == '+') {
        buffer[index++] = ch;
        ch = gfun();
    } else if (ch == '-') {
        sign = -1;
        buffer[index++] = ch;
        ch = gfun();
    } else if (ch == '.') {
        buffer[index++] = ch;
        ch = gfun();
        if (ch == ' ') return (object*)DOT;
        isfloat = true;
    }

    // Parse reader macros
    else if (ch == '#') {
        ch = gfun();
        char ch2 = ch & ~0x20; // force to upper case
        if (ch == '\\') { // Character
            base = 0; ch = gfun();
            if (issp(ch) || isbr(ch)) return character(ch);
            else LastChar = ch;
        } else if (ch == '|') {
            do { while (gfun() != '|'); }
            while (gfun() != '#');
            return nextitem(gfun);
        } else if (ch2 == 'B') base = 2;
        else if (ch2 == 'O') base = 8;
        else if (ch2 == 'X') base = 16;
        else if (ch == '\'') return nextitem(gfun);
        else if (ch == '.') {
            setflag(NOESC);
            object* result = eval(read(gfun), NULL);
            clrflag(NOESC);
            return result;
        }
        else if (ch == '(') { LastChar = ch; return readarray(1, read(gfun)); }
        else if (ch == '*') return readbitarray(gfun);
        else if (ch >= '1' && ch <= '9' && (gfun() & ~0x20) == 'A') return readarray(ch - '0', read(gfun));
        else error2(PSTR("illegal character after #"));
        ch = gfun();
    }
    int valid; // 0=undecided, -1=invalid, +1=valid
    if (ch == '.') valid = 0; else if (digitvalue(ch)<base) valid = 1; else valid = -1;
    bool isexponent = false;
    int exponent = 0, esign = 1;
    buffer[2] = '\0'; buffer[3] = '\0'; buffer[4] = '\0'; buffer[5] = '\0'; // In case symbol is < 5 letters
    float divisor = 10.0;

    while(!issp(ch) && !isbr(ch) && index < bufmax) {
        buffer[index++] = ch;
        if (base == 10 && ch == '.' && !isexponent) {
            isfloat = true;
            fresult = result;
        } else if (base == 10 && (ch == 'e' || ch == 'E')) {
            if (!isfloat) { isfloat = true; fresult = result; }
            isexponent = true;
            if (valid == 1) valid = 0; else valid = -1;
        } else if (isexponent && ch == '-') {
            esign = -esign;
        } else if (isexponent && ch == '+') {
        } else {
            int digit = digitvalue(ch);
            if (digitvalue(ch)<base && valid != -1) valid = 1; else valid = -1;
            if (isexponent) {
                exponent = exponent * 10 + digit;
            } else if (isfloat) {
                fresult = fresult + digit / divisor;
                divisor = divisor * 10.0;
            } else {
                result = result * base + digit;
            }
        }
        ch = gfun();
    }

    buffer[index] = '\0';
    if (isbr(ch)) LastChar = ch;
    if (isfloat && valid == 1) return makefloat(fresult * sign * pow(10, exponent * esign));
    else if (valid == 1) {
        if (base == 10 && result > ((unsigned int)INT_MAX+(1-sign)/2))
            return makefloat((float)result*sign);
        return number(result*sign);
    } else if (base == 0) {
        if (index == 1) return character(buffer[0]);
        PGM_P p = ControlCodes; char c = 0;
        while (c < 33) {
            if (strcasecmp_P(buffer, p) == 0) return character(c);
            p = p + strlen_P(p) + 1; c++;
        }
        if (index == 3) return character((buffer[0]*10+buffer[1])*10+buffer[2]-5328);
        error2(PSTR("unknown character"));
    }

    builtin_t x = lookupbuiltin(buffer);
    if (x == NIL) return nil;
    if (x != ENDFUNCTIONS) return bsymbol(x);
    object* sym = buftosymbol(buffer);
    if (buffer[0] == ':') { // Keywords quote themselves
        sym = quoteit(QUOTE, sym);
    }
    return sym;
}

/*
    readrest - reads the remaining tokens from the specified stream
*/
object* readrest (gfun_t gfun) {
    object* item = nextitem(gfun);
    object* head = NULL;
    object* tail = NULL;

    while (item != (object*)KET) {
        if (item == (object*)BRA) {
            item = readrest(gfun);
        } else if (item == (object*)QUO) {
            item = cons(bsymbol(QUOTE), cons(read(gfun), NULL));
        } else if (item == (object*)DOT) {
            tail->cdr = read(gfun);
            if (readrest(gfun) != NULL) error2(PSTR("malformed list"));
            return head;
        } else {
            object* cell = cons(item, NULL);
            if (head == NULL) head = cell;
            else tail->cdr = cell;
            tail = cell;
            item = nextitem(gfun);
        }
    }
    return head;
}

/*
    read - recursively reads a Lisp object from the stream gfun and returns it
*/
object* read (gfun_t gfun) {
    object* item = nextitem(gfun);
    if (item == (object*)KET) error2(PSTR("incomplete list"));
    if (item == (object*)BRA) return readrest(gfun);
    if (item == (object*)DOT) return read(gfun);
    if (item == (object*)QUO) return cons(bsymbol(QUOTE), cons(read(gfun), NULL));
    return item;
}

// Setup

/*
    initenv - initialises the uLisp environment
*/
void initenv () {
    GlobalEnv = NULL;
    tee = bsymbol(TEE);
}

/*
    initgfx - initialises the graphics
*/
void initgfx () {
    #if defined(gfxsupport)
    tft.init(135, 240);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
    #endif
}

void ulispinit () {
    initworkspace();
    initenv();
    initsleep();
    initgfx();
}

// Read/Evaluate/Print loop

/*
    repl - the Lisp Read/Evaluate/Print loop
*/
void repl (object* env) {
    for (;;) {
        randomSeed(micros());
        gc(NULL, env);
        #if defined(printfreespace)
        pint(Freespace, pserial);
        #endif
        if (BreakLevel) {
            pfstring(PSTR(" : "), pserial);
            pint(BreakLevel, pserial);
        }
        pserial('>'); pserial(' ');
        Context = 0;
        object* line = read(gserial);
        if (BreakLevel && line == nil) { pln(pserial); return; }
        if (line == (object*)KET) error2(PSTR("unmatched right bracket"));
        push(line, GCStack);
        pfl(pserial);
        line = eval(line, env);
        pfl(pserial);
        printobject(line, pserial);
        pop(GCStack);
        pfl(pserial);
        pln(pserial);
    }
}

void ulisperrcleanup () {
    // Come here after error
    delay(100); while (Serial.available()) Serial.read();
    clrflag(NOESC); BreakLevel = 0;
    for (int i=0; i<TRACEMAX; i++) TraceDepth[i] = 0;
    #if defined(sdcardsupport)
    SDpfile.close(); SDgfile.close();
    #endif
    #if defined(lisplibrary)
    if (!tstflag(LIBRARYLOADED)) { setflag(LIBRARYLOADED); loadfromlibrary(NULL); }
    #endif
    client.stop();
}

#endif
