/* uLisp ESP Version 4.3 (commented) - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 15th September 2022

   Licensed under the MIT license: https://opensource.org/licenses/MIT

   Patches from original:
   * Deleted all code not for ESP32 specifically (shorter and easier to maintain)
   * Run-from-SD on startup
   * Different printgcs message
   * Used actual name instead of 'stringNNN' in lookup table
   * Goheeca and Max-Gerd Retzlaff's error-handling code (https://github.com/Goheeca/redbear_duo-uLisp/commit/4894c13 and http://forum.ulisp.com/t/error-handling-in-ulisp/691/7), but replaced sp_error with fn_throw (sp_error caused a segfault)
   * Dave Astels' macro, intern, and generic :keyword support (http://forum.ulisp.com/t/ive-added-a-few-things-that-might-be-interesting/456)

   New custom functions:
   * battery:voltage, battery:percentage, battery:change-rate (reading from the MAX17048 on a Thing Plus C)

*/
/* Test MACRO with

(defmacro foo (aa bb) `(defmacro ,aa () `(princ ,,bb)))
(foo bar "baz")

*/

// Lisp Library
// const char LispLibrary[] PROGMEM = "";
#include "LispLibrary.h"

// Compile options

// #define gfxsupport

// Includes
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include <setjmp.h>
#include <SPI.h>
#include <Wire.h>
#include <limits.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <analogWrite.h>
#include <ESP32Tone.h>
#include <ESP32PWM.h>


#if defined(gfxsupport)
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_SSD1306.h>
#define COLOR_WHITE 1
#define COLOR_BLACK 0
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     4
Adafruit_SSD1306 tft(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
#endif

#include <SD.h>
#define SDSIZE 172

// Platform specific settings

#define WORDALIGNED __attribute__((aligned (4)))
#define BUFFERSIZE 260  // Number of bits+4

#if !defined(autorunimagepath)
  #define autorunimagepath "/ULISP.IMG"
#endif

#if !defined(sdmainfile)
  #define sdmainfile "main.lisp"
#endif

#if !defined(WORKSPACESIZE)
  #define WORKSPACESIZE (9216-SDSIZE)     /* Cells (8*bytes) */
#endif
#define LITTLEFS
#include "FS.h"
#include <LittleFS.h>
#if !defined(analogWrite)
  #define analogWrite(x,y) dacWrite((x), (y))
#endif
#define SDCARD_SS_PIN 13
#define LED_BUILTIN 13

// C Macros

#define nil                NULL
#define car(x)             (((object *) (x))->car)
#define cdr(x)             (((object *) (x))->cdr)

#define first(x)           (((object *) (x))->car)
#define second(x)          (car(cdr(x)))
#define cddr(x)            (cdr(cdr(x)))
#define third(x)           (car(cdr(cdr(x))))

#define push(x, y)         ((y) = cons((x),(y)))
#define pop(y)             ((y) = cdr(y))

#define integerp(x)        ((x) != NULL && (x)->type == NUMBER)
#define floatp(x)          ((x) != NULL && (x)->type == FLOAT)
#define symbolp(x)         ((x) != NULL && (x)->type == SYMBOL)
#define stringp(x)         ((x) != NULL && (x)->type == STRING)
#define characterp(x)      ((x) != NULL && (x)->type == CHARACTER)
#define arrayp(x)          ((x) != NULL && (x)->type == ARRAY)
#define streamp(x)         ((x) != NULL && (x)->type == STREAM)

#define mark(x)            (car(x) = (object *)(((uintptr_t)(car(x))) | MARKBIT))
#define unmark(x)          (car(x) = (object *)(((uintptr_t)(car(x))) & ~MARKBIT))
#define marked(x)          ((((uintptr_t)(car(x))) & MARKBIT) != 0)
#define MARKBIT            1

#define setflag(x)         (Flags = Flags | 1<<(x))
#define clrflag(x)         (Flags = Flags & ~(1<<(x)))
#define tstflag(x)         (Flags & 1<<(x))

#define issp(x)            (x == ' ' || x == '\n' || x == '\r' || x == '\t')
#define longsymbolp(x)     (((x)->name & 0x03) == 0)
#define twist(x)           ((uint32_t)((x)<<2) | (((x) & 0xC0000000)>>30))
#define untwist(x)         (((x)>>2 & 0x3FFFFFFF) | ((x) & 0x03)<<30)
#define PACKEDS            0x43238000
#define BUILTINS           0xF4240000

// Constants

const int TRACEMAX = 3; // Number of traced functions
enum type { ZZERO=0, SYMBOL=2, CODE=4, NUMBER=6, STREAM=8, CHARACTER=10, FLOAT=12, ARRAY=14, STRING=16, PAIR=18 };  // ARRAY STRING and PAIR must be last
enum token { UNUSED, BRA, KET, QUO, DOT, BACKTICK, COMMA, COMMAAT };
enum stream { SERIALSTREAM, I2CSTREAM, SPISTREAM, SDSTREAM, WIFISTREAM, STRINGSTREAM, GFXSTREAM };

// Stream names used by printobject
const char serialstream[] PROGMEM = "serial";
const char i2cstream[] PROGMEM = "i2c";
const char spistream[] PROGMEM = "spi";
const char sdstream[] PROGMEM = "sd";
const char wifistream[] PROGMEM = "wifi";
const char stringstream[] PROGMEM = "string";
const char gfxstream[] PROGMEM = "gfx";
PGM_P const streamname[] PROGMEM = {serialstream, i2cstream, spistream, sdstream, wifistream, stringstream, gfxstream};

// Typedefs

typedef uint32_t symbol_t;

typedef struct sobject {
  union {
    struct {
      sobject *car;
      sobject *cdr;
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

typedef object *(*fn_ptr_type)(object *, object *);
typedef void (*mapfun_t)(object *, object **);

typedef const struct {
  PGM_P string;
  fn_ptr_type fptr;
  uint8_t minmax;
  const char *doc;
} tbl_entry_t;

typedef int (*gfun_t)();
typedef void (*pfun_t)(char);

enum builtin_t { NIL, TEE, NOTHING, OPTIONAL, INITIALELEMENT, ELEMENTTYPE, BIT, AMPREST, LAMBDA, LET,
LETSTAR, CLOSURE, PSTAR,
SPECIAL_FORMS,
QUOTE, OR, DEFUN, DEFVAR, SETQ, LOOP, RETURN, PUSH, POP, INCF,
DECF, SETF, DOLIST, DOTIMES, TRACE, UNTRACE, FORMILLIS, TIME, WITHOUTPUTTOSTRING, WITHSERIAL, WITHI2C,
WITHSPI, WITHSDCARD, WITHGFX, WITHCLIENT, UNWINDPROTECT, IGNOREERRORS, /*SP_ERROR,*/
MACRO, DEFMACRO, QUASIQUOTE, UNQUOTE, UNQUOTESPLICING, EXPAND,
TAIL_FORMS,
PROGN, IF, COND, WHEN, UNLESS, CASE, AND, HELP,
FUNCTIONS,
GETERROR, THROW, INTERN, NOT, NULLFN, CONS, ATOM, LISTP, CONSP, SYMBOLP, ARRAYP, BOUNDP, SETFN, STREAMP, EQ, EQUAL, CAR, FIRST, CDR, REST,
CAAR, CADR, SECOND, CDAR, CDDR, CAAAR, CAADR, CADAR, CADDR, THIRD, CDAAR, CDADR, CDDAR, CDDDR, LENGTH,
ARRAYDIMENSIONS, LIST, MAKEARRAY, REVERSE, NTH, AREF, ASSOC, MEMBER, APPLY, FUNCALL, APPEND, MAPC, MAPCAR,
MAPCAN, ADD, SUBTRACT, MULTIPLY, DIVIDE, MOD, ONEPLUS, ONEMINUS, ABS, RANDOM, MAXFN, MINFN, NOTEQ, NUMEQ,
LESS, LESSEQ, GREATER, GREATEREQ, PLUSP, MINUSP, ZEROP, ODDP, EVENP, INTEGERP, NUMBERP, FLOATFN, FLOATP,
SIN, COS, TAN, ASIN, ACOS, ATAN, SINH, COSH, TANH, EXP, SQRT, LOG, EXPT, CEILING, FLOOR, TRUNCATE, ROUND,
CHAR, CHARCODE, CODECHAR, CHARACTERP, STRINGP, STRINGEQ, STRINGLESS, STRINGGREATER, SORT, STRINGFN,
CONCATENATE, SUBSEQ, READFROMSTRING, PRINCTOSTRING, PRIN1TOSTRING, LOGAND, LOGIOR, LOGXOR, LOGNOT, ASH,
LOGBITP, EVAL, GLOBALS, LOCALS, MAKUNBOUND, BREAK, READ, PRIN1, PRINT, PRINC, TERPRI, READBYTE, READLINE,
WRITEBYTE, WRITESTRING, WRITELINE, RESTARTI2C, GC, ROOM, /* SAVEIMAGE, LOADIMAGE, */ CLS, PINMODE, DIGITALREAD,
DIGITALWRITE, ANALOGREAD, ANALOGREADRESOLUTION, ANALOGWRITE, DELAY, MILLIS, SLEEP, NOTE, REGISTER, EDIT, PPRINT,
PPRINTALL, FORMAT, REQUIRE, LISTLIBRARY, DOCUMENTATION, AVAILABLE, WIFISERVER, WIFISOFTAP, CONNECTED, WIFILOCALIP,
WIFICONNECT, DRAWPIXEL, DRAWLINE, DRAWRECT, FILLRECT, DRAWCIRCLE, FILLCIRCLE, DRAWROUNDRECT,
FILLROUNDRECT, DRAWTRIANGLE, FILLTRIANGLE, DRAWCHAR, SETCURSOR, SETTEXTCOLOR, SETTEXTSIZE, SETTEXTWRAP,
FILLSCREEN, SETROTATION, INVERTDISPLAY,
KEYWORDS, 
K_LED_BUILTIN, K_HIGH, K_LOW,
K_INPUT, K_INPUT_PULLUP, K_INPUT_PULLDOWN, K_OUTPUT,
USERFUNCTIONS,
// Insert your own enum entries here
BATTERYVOLTAGE, BATTERYPERCENTAGE, BATTERYCHANGERATE,


ENDFUNCTIONS, SET_SIZE = INT_MAX };

// Global variables
SFE_MAX1704X battery(MAX1704X_MAX17048);

object Workspace[WORKSPACESIZE] WORDALIGNED;

jmp_buf toplevel_errorbuffer;
jmp_buf *errorbuffer = &toplevel_errorbuffer;
unsigned int Freespace = 0;
object *Freelist;
unsigned int I2Ccount;
unsigned int TraceFn[TRACEMAX];
unsigned int TraceDepth[TRACEMAX];

object *GlobalEnv;
object *GCStack = NULL;
object *NeverGC = NULL;
object *GlobalString;
object *GlobalErrorString;
object *GlobalStringTail;
int GlobalStringIndex = 0;
uint8_t PrintCount = 0;
uint8_t BreakLevel = 0;
char LastChar = 0;
char LastPrint = 0;

// Flags
enum flag { PRINTREADABLY, RETURNFLAG, ESCAPE, EXITEDITOR, LIBRARYLOADED, NOESC, NOECHO, MUFFLEERRORS, DISABLEGC };
volatile uint16_t Flags = 0b00001; // PRINTREADABLY set by default

// Forward references
object *tee;
void pfstring (PGM_P s, pfun_t pfun);
object *apply (object *function, object *args, object **env);
int glibrary ();
int gserial ();
boolean listp (object *x);
uint8_t nthchar (object *string, int n);
uint8_t getminmax (builtin_t name);
void checkminmax (builtin_t name, int nargs);
void pfl (pfun_t pfun);
inline void pln (pfun_t pfun);
void pint (int i, pfun_t pfun);
void printstring (object *form, pfun_t pfun);
void pserial (char c);
void pstring (char *s, pfun_t pfun);
void superprint (object *form, int lm, pfun_t pfun);
void supersub (object *form, int lm, int super, pfun_t pfun);
void testescape ();
object *read (gfun_t gfun);
object *edit (object *fun);

// Error handling

/*
  errorsub - used by all the error routines.
  Prints: "Error: 'fname' string", where fname is the name of the Lisp function in which the error occurred;
  and returns the Lisp string containing the same message.
*/
object *errorsub (symbol_t fname, PGM_P string) {
  if (!tstflag(MUFFLEERRORS)) {
    pfl(pserial); pfstring(PSTR("Error: "), pserial);
    if (fname != sym(NIL)) {
      pserial('\'');
      psymbol(fname, pserial);
      pserial('\''); pserial(' ');
    }
    pfstring(string, pserial);
  }
  object *obj = startstring(/*SP_ERROR*/THROW);
  if (fname != sym(NIL)) {
    pstr('\'');
    psymbol(fname, pstr);
    pstr('\''); pstr(' ');
  }
  pfstring(string, pstr); // copy to GlobalString
  return obj;
}

/*
  error - prints an error message and reenters the REPL.
  Prints: "Error: 'fname' string: symbol", where fname is the name of the user Lisp function in which the error occurred,
  and symbol is the object generating the error.
*/
void errorsym (symbol_t fname, PGM_P string, object *symbol) {
  object *obj = errorsub(fname, string);
  if (!tstflag(MUFFLEERRORS)) {
    pserial(':'); pserial(' ');
    printobject(symbol, pserial);
  }
  pstr(':'); pstr(' ');
  printobject(symbol, pstr);
  GlobalErrorString = obj;
  errorend();
}

/*
  error - prints an error message and reenters the REPL.
  Prints: "Error: 'fname' string", where fname is the name of the user Lisp function in which the error occurred.
*/
void errorsym2 (symbol_t fname, PGM_P string) {
  GlobalErrorString = errorsub(fname, string);
  errorend();
}

/*
  error - prints an error message and reenters the REPL.
  Prints: "Error: 'fname' string: symbol", where fname is the name of the built-in Lisp function in which the error occurred,
  and symbol is the object generating the error.
*/
void error (builtin_t fname, PGM_P string, object *symbol) {
  errorsym(sym(fname), string, symbol);
}

/*
  error - prints an error message and reenters the REPL.
  Prints: "Error: 'fname' string", where fname is the name of the built-in Lisp function in which the error occurred.
*/
void error2 (builtin_t fname, PGM_P string) {
  errorsym2(sym(fname), string);
}

void errorend () {
  if (!tstflag(MUFFLEERRORS)) pln(pserial);
  GCStack = NULL;
  longjmp(*errorbuffer, 1);
}

/*
  formaterr - displays a format error with a ^ pointing to the error
*/
void formaterr (object *formatstr, PGM_P string, uint8_t p) {
  if (!tstflag(MUFFLEERRORS)) {
    pln(pserial); indent(4, ' ', pserial); printstring(formatstr, pserial); pln(pserial);
    indent(p+5, ' ', pserial); pserial('^');
    errorsub(FORMAT, string);
    pln(pserial);
  }
  GCStack = NULL;
  longjmp(*errorbuffer, 1);
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
    object *obj = &Workspace[i];
    car(obj) = NULL;
    cdr(obj) = Freelist;
    Freelist = obj;
    Freespace++;
  }
}

/*
  myalloc - returns the first object from the linked list of free objects
*/
object *myalloc () {
  if (Freespace == 0) error2(NIL, PSTR("out of memory"));
  object *temp = Freelist;
  Freelist = cdr(Freelist);
  Freespace--;
  return temp;
}

/*
  myfree - adds obj to the linked list of free objects.
  inline makes gc significantly faster
*/
inline void myfree (object *obj) {
  car(obj) = NULL;
  cdr(obj) = Freelist;
  Freelist = obj;
  Freespace++;
}

// Make each type of object

/*
  number - make an integer object with value n and return it
*/
object *number (int n) {
  object *ptr = myalloc();
  ptr->type = NUMBER;
  ptr->integer = n;
  return ptr;
}

/*
  makefloat - make a floating point object with value f and return it
*/
object *makefloat (float f) {
  object *ptr = myalloc();
  ptr->type = FLOAT;
  ptr->single_float = f;
  return ptr;
}

/*
  character - make a character object with value c and return it
*/
object *character (uint8_t c) {
  object *ptr = myalloc();
  ptr->type = CHARACTER;
  ptr->chars = c;
  return ptr;
}

/*
  cons - make a cons with arg1 and arg2 return it
*/
object *cons (object *arg1, object *arg2) {
  object *ptr = myalloc();
  ptr->car = arg1;
  ptr->cdr = arg2;
  return ptr;
}

/*
  symbol - make a symbol object with value name and return it
*/
object *symbol (symbol_t name) {
  object *ptr = myalloc();
  ptr->type = SYMBOL;
  ptr->name = name;
  return ptr;
}

/*
  bsymbol - make a built-in symbol
*/
inline object *bsymbol (builtin_t name) {
  return intern(twist(name+BUILTINS));
}

/*
  intern - looks through the workspace for an existing occurrence of symbol name and returns it,
  otherwise calls symbol(name) to create a new symbol.
*/
object *intern (symbol_t name) {
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && obj->name == name) return obj;
  }
  return symbol(name);
}

/*
  eqsymbols - compares the long string/symbol obj with the string in buffer.
*/
bool eqsymbols (object *obj, char *buffer) {
  object *arg = cdr(obj);
  int i = 0;
  while (!(arg == NULL && buffer[i] == 0)) {
    if (arg == NULL || buffer[i] == 0 ||
      arg->chars != (buffer[i]<<24 | buffer[i+1]<<16 | buffer[i+2]<<8 | buffer[i+3])) return false;
    arg = car(arg);
    i = i + 4;
  }
  return true;
}

/*
  internlong - looks through the workspace for an existing occurrence of the long symbol in buffer and returns it,
  otherwise calls lispstring(buffer) to create a new symbol.
*/
object *internlong (char *buffer) {
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && longsymbolp(obj) && eqsymbols(obj, buffer)) return obj;
  }
  object *obj = lispstring(buffer);
  obj->type = SYMBOL;
  return obj;
}

/*
  stream - makes a stream object defined by streamtype and address, and returns it
*/
object *stream (uint8_t streamtype, uint8_t address) {
  object *ptr = myalloc();
  ptr->type = STREAM;
  ptr->integer = streamtype<<8 | address;
  return ptr;
}

/*
  newstring - makes an empty string object and returns it
*/
object *newstring () {
  object *ptr = myalloc();
  ptr->type = STRING;
  ptr->chars = 0;
  return ptr;
}

// Garbage collection

/*
  markobject - recursively marks reachable objects, starting from obj
*/
void markobject (object *obj) {
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
    object *obj = &Workspace[i];
    if (!marked(obj)) myfree(obj); else unmark(obj);
  }
}

/*
  gc - performs garbage collection by calling markobject() on each of the pointers to objects in use,
  followed by sweep() to free unused objects.
*/
unsigned int GCCount = 0;

void gc (object *form, object *env) {
  if (tstflag(DISABLEGC)) return;
  int start = Freespace;
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  markobject(NeverGC);
  markobject(form);
  markobject(env);
  markobject(GlobalErrorString);
  sweep();
  pfl(pserial); pfstring(PSTR("{GC"), pserial); pint(++GCCount, pserial); pfstring(PSTR(": +"), pserial); pint(Freespace - start, pserial); pserial('}'); pfl(pserial);
  //pfl(pserial); pserial('{'); pint(Freespace - start, pserial); pserial('}');
}

// Make SD card filename

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
  if (tracing(name)) error(TRACE, PSTR("already being traced"), symbol(name));
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == 0) { TraceFn[i] = name; TraceDepth[i] = 0; return; }
    i++;
  }
  error2(TRACE, PSTR("already tracing 3 functions"));
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
  error(UNTRACE, PSTR("not tracing"), symbol(name));
}

// Helper functions

/*
  consp - implements Lisp consp
*/
bool consp (object *x) {
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
bool listp (object *x) {
  if (x == NULL) return true;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

/*
  improperp - tests whether x is an improper list
*/
#define improperp(x) (!listp(x))

object *quoteit (builtin_t quoter, object *it) {
  return cons(bsymbol(quoter), cons(it, nil));
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

/*
  toradix40 - returns a number from 0 to 39 if the character can be encoded, or -1 otherwise.
*/
int8_t toradix40 (char ch) {
  if (ch == 0) return 0;
  if (ch >= '0' && ch <= '9') return ch-'0'+1;
  if (ch == '-') return 37; if (ch == '*') return 38; if (ch == '$') return 39;
  ch = ch | 0x20;
  if (ch >= 'a' && ch <= 'z') return ch-'a'+11;
  return -1; // Invalid
}

/*
  fromradix40 - returns the character encoded by the number n.
*/
char fromradix40 (char n) {
  if (n >= 1 && n <= 9) return '0'+n-1;
  if (n >= 11 && n <= 36) return 'a'+n-11;
  if (n == 37) return '-'; if (n == 38) return '*'; if (n == 39) return '$';
  return 0;
}

/*
  pack40 - packs six radix40-encoded characters from buffer into a 32-bit number and returns it.
*/
uint32_t pack40 (char *buffer) {
  int x = 0;
  for (int i=0; i<6; i++) x = x * 40 + toradix40(buffer[i]);
  return x;
}

/*
  valid40 - returns true if the symbol in buffer can be encoded as six radix40-encoded characters.
*/
bool valid40 (char *buffer) {
  if (toradix40(buffer[0]) < 11) return false;
  for (int i=1; i<6; i++) if (toradix40(buffer[i]) < 0) return false;
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
int checkinteger (builtin_t name, object *obj) {
  if (!integerp(obj)) error(name, notaninteger, obj);
  return obj->integer;
}

/*
  checkbitvalue - check that obj is an integer equal to 0 or 1 and return it
*/
int checkbitvalue (builtin_t name, object *obj) {
  if (!integerp(obj)) error(name, notaninteger, obj);
  int n = obj->integer;
  if (n & ~1) error(name, PSTR("argument is not a bit value"), obj);
  return n;
}

/*
  checkintfloat - check that obj is an integer or floating-point number and return the number
*/
float checkintfloat (builtin_t name, object *obj){
  if (integerp(obj)) return obj->integer;
  if (!floatp(obj)) error(name, notanumber, obj);
  return obj->single_float;
}

/*
  checkchar - check that obj is a character and return the character
*/
int checkchar (builtin_t name, object *obj) {
  if (!characterp(obj)) error(name, PSTR("argument is not a character"), obj);
  return obj->chars;
}

/*
  checkstring - check that obj is a string
*/
object *checkstring (builtin_t name, object *obj) {
  if (!stringp(obj)) error(name, notastring, obj);
  return obj;
}

int isstream (object *obj){
  if (!streamp(obj)) error(NIL, PSTR("not a stream"), obj);
  return obj->integer;
}

int isbuiltin (object *obj, builtin_t n) {
  return symbolp(obj) && obj->name == sym(n);
}

bool builtinp (symbol_t name) {
  return (untwist(name) > BUILTINS && untwist(name) < ENDFUNCTIONS+BUILTINS);
}

int keywordp (object *obj) {
  if (!symbolp(obj)) return false;
  builtin_t name = builtin(obj->name);
  return ((name > KEYWORDS) && (name < USERFUNCTIONS));
}

int checkkeyword (builtin_t name, object *obj) {
  if (!keywordp(obj)) error(name, PSTR("argument is not a keyword"), obj);
  builtin_t kname = builtin(obj->name);
  uint8_t context = getminmax(kname);
  if (context != 0 && context != name) error(name, invalidkey, obj);
  return ((int)lookupfn(kname));
}

/*
  checkargs - checks that the number of objects in the list args
  is within the range specified in the symbol lookup table
*/
void checkargs (builtin_t name, object *args) {
  int nargs = listlength(name, args);
  checkminmax(name, nargs);
}

/*
  eq - implements Lisp eq
*/
int eq (object *arg1, object *arg2) {
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
int equal (object *arg1, object *arg2) {
  if (eq(arg1, arg2)) return true;
  if (stringp(arg1) && stringp(arg2)) {
    object *args = cons(arg1, cons(arg2, nil));
    return stringcompare(EQUAL, args, false, false, true);
  }
  if (consp(arg1) && consp(arg2)) {
    while (arg1 != nil && arg2 != nil) {
      if (!equal(car(arg1), car(arg2))) return false;
      arg1 = cdr(arg1);
      arg2 = cdr(arg2);
    }
    return arg1 == nil && arg2 == nil;
  }
  return false;
}

int listlength (builtin_t name, object *list) {
  int length = 0;
  while (list != NULL) {
    if (improperp(list)) error2(name, notproper);
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
object *add_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult + checkintfloat(ADD, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

/*
  subtract_floats - used by fn_subtract with more than one argument
  Converts the numbers in args to floats, subtracts them from fresult, and returns the result as a Lisp float.
*/
object *subtract_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult - checkintfloat(SUBTRACT, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

/*
  negate - used by fn_subtract with one argument
  If the result is an integer, and negating it doesn't overflow, keep the result as an integer.
  Otherwise convert the result to a float, negate it, and return the result as a Lisp float.
*/
object *negate (object *arg) {
  if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(-result);
    else return number(-result);
  } else if (floatp(arg)) return makefloat(-(arg->single_float));
  else error(SUBTRACT, notanumber, arg);
  return nil;
}

/*
  multiply_floats - used by fn_multiply
  Converts the numbers in args to floats, adds them to fresult, and returns the result as a Lisp float.
*/
object *multiply_floats (object *args, float fresult) {
  while (args != NULL) {
   object *arg = car(args);
    fresult = fresult * checkintfloat(MULTIPLY, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

/*
  divide_floats - used by fn_divide
  Converts the numbers in args to floats, divides fresult by them, and returns the result as a Lisp float.
*/
object *divide_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    float f = checkintfloat(DIVIDE, arg);
    if (f == 0.0) error2(DIVIDE, divisionbyzero);
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
object *compare (builtin_t name, object *args, bool lt, bool gt, bool eq) {
  object *arg1 = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg1) && integerp(arg2)) {
      if (!lt && ((arg1->integer) < (arg2->integer))) return nil;
      if (!eq && ((arg1->integer) == (arg2->integer))) return nil;
      if (!gt && ((arg1->integer) > (arg2->integer))) return nil;
    } else {
      if (!lt && (checkintfloat(name, arg1) < checkintfloat(name, arg2))) return nil;
      if (!eq && (checkintfloat(name, arg1) == checkintfloat(name, arg2))) return nil;
      if (!gt && (checkintfloat(name, arg1) > checkintfloat(name, arg2))) return nil;
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
object *assoc (object *key, object *list) {
  while (list != NULL) {
    if (improperp(list)) error(ASSOC, notproper, list);
    object *pair = first(list);
    if (!listp(pair)) error(ASSOC, PSTR("element is not a list"), pair);
    if (pair != NULL && eq(key,car(pair))) return pair;
    list = cdr(list);
  }
  return nil;
}

/*
  delassoc - deletes the pair matching key from an association list and returns the key, or nil if not found
*/
object *delassoc (object *key, object **alist) {
  object *list = *alist;
  object *prev = NULL;
  while (list != NULL) {
    object *pair = first(list);
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
object *buildarray (int n, int s, object *def) {
  int s2 = s>>1;
  if (s2 == 1) {
    if (n == 2) return cons(def, def);
    else if (n == 1) return cons(def, NULL);
    else return NULL;
  } else if (n >= s2) return cons(buildarray(s2, s2, def), buildarray(n - s2, s2, def));
  else return cons(buildarray(n, s2, def), nil);
}

object *makearray (builtin_t name, object *dims, object *def, bool bitp) {
  int size = 1;
  object *dimensions = dims;
  while (dims != NULL) {
    int d = car(dims)->integer;
    if (d < 0) error2(name, PSTR("dimension can't be negative"));
    size = size * d;
    dims = cdr(dims);
  }
  // Bit array identified by making first dimension negative
  if (bitp) {
    size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    car(dimensions) = number(-(car(dimensions)->integer));
  }
  object *ptr = myalloc();
  ptr->type = ARRAY;
  object *tree = nil;
  if (size != 0) tree = buildarray(size, nextpower2(size), def);
  ptr->cdr = cons(tree, dimensions);
  return ptr;
}

/*
  arrayref - returns a pointer to the element specified by index in the array of size s
*/
object **arrayref (object *array, int index, int size) {
  int mask = nextpower2(size)>>1;
  object **p = &car(cdr(array));
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
object **getarray (builtin_t name, object *array, object *subs, object *env, int *bit) {
  int index = 0, size = 1, s;
  *bit = -1;
  bool bitp = false;
  object *dims = cddr(array);
  while (dims != NULL && subs != NULL) {
    int d = car(dims)->integer;
    if (d < 0) { d = -d; bitp = true; }
    if (env) s = checkinteger(name, eval(car(subs), env)); else s = checkinteger(name, car(subs));
    if (s < 0 || s >= d) error(name, PSTR("subscript out of range"), car(subs));
    size = size * d;
    index = index * d + s;
    dims = cdr(dims); subs = cdr(subs);
  }
  if (dims != NULL) error2(name, PSTR("too few subscripts"));
  if (subs != NULL) error2(name, PSTR("too many subscripts"));
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
void rslice (object *array, int size, int slice, object *dims, object *args) {
  int d = first(dims)->integer;
  for (int i = 0; i < d; i++) {
    int index = slice * d + i;
    if (!consp(args)) error2(NIL, PSTR("initial contents don't match array type"));
    if (cdr(dims) == NULL) {
      object **p = arrayref(array, index, size);
      *p = car(args);
    } else rslice(array, size, index, cdr(dims), car(args));
    args = cdr(args);
  }
}

/*
  readarray - reads a list structure from args and converts it to a d-dimensional array.
  Uses rslice for each of the slices of the array.
*/
object *readarray (int d, object *args) {
  object *list = args;
  object *dims = NULL; object *head = NULL;
  int size = 1;
  for (int i = 0; i < d; i++) {
    if (!listp(list)) error2(NIL, PSTR("initial contents don't match array type"));
    int l = listlength(NIL, list);
    if (dims == NULL) { dims = cons(number(l), NULL); head = dims; }
    else { cdr(dims) = cons(number(l), NULL); dims = cdr(dims); }
    size = size * l;
    if (list != NULL) list = car(list);
  }
  object *array = makearray(NIL, head, NULL, false);
  rslice(array, size, 0, head, args);
  return array;
}

/*
  readbitarray - reads an item in the format #*1010101000110 by reading it and returning a list of integers,
  and then converting that to a bit array
*/
object *readbitarray (gfun_t gfun) {
  char ch = gfun();
  object *head = NULL;
  object *tail = NULL;
  while (!issp(ch) && ch != ')' && ch != '(') {
    if (ch != '0' && ch != '1') error2(NIL, PSTR("illegal character in bit array"));
    object *cell = cons(number(ch - '0'), NULL);
    if (head == NULL) head = cell;
    else tail->cdr = cell;
    tail = cell;
    ch = gfun();
  }
  LastChar = ch;
  int size = listlength(NIL, head);
  object *array = makearray(NIL, cons(number(size), NULL), 0, true);
  size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
  int index = 0;
  while (head != NULL) {
    object **loc = arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size);
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
void pslice (object *array, int size, int slice, object *dims, pfun_t pfun, bool bitp) {
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
void printarray (object *array, pfun_t pfun) {
  object *dimensions = cddr(array);
  object *dims = dimensions;
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

object *startstring (builtin_t name) {
  (void) name;
  object *string = newstring();
  GlobalString = string;
  GlobalStringTail = string;
  return string;
}

/*
  buildstring - adds a character on the end of a string
  Handles Lisp strings packed four characters per 32-bit word
*/
void buildstring (char ch, object **tail) {
  object *cell;
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
object *copystring (object *arg) {
  object *obj = newstring();
  object *ptr = obj;
  arg = cdr(arg);
  while (arg != NULL) {
    object *cell =  myalloc(); car(cell) = NULL;
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
object *readstring (uint8_t delim, gfun_t gfun) {
  object *obj = newstring();
  object *tail = obj;
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
int stringlength (object *form) {
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
uint8_t nthchar (object *string, int n) {
  object *arg = cdr(string);
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
object *lispstring (char *s) {
  object *obj = newstring();
  object *tail = obj;
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
bool stringcompare (builtin_t name, object *args, bool lt, bool gt, bool eq) {
  object *arg1 = checkstring(name, first(args));
  object *arg2 = checkstring(name, second(args));
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
object *documentation (builtin_t name, object *arg, object *env) {
  if (!symbolp(arg)) error(name, notasymbol, arg);
  object *pair = findpair(arg, env);
  if (pair != NULL) {
    object *val = cdr(pair);
    if (listp(val) && first(val)->name == sym(LAMBDA) && cdr(val) != NULL && cddr(val) != NULL) {
      if (stringp(third(val))) return third(val);
    }
  }
  symbol_t docname = arg->name;
  if (!builtinp(docname)) return nil;
  char *docstring = lookupdoc(builtin(docname));
  if (docstring == NULL) return nil;
  return lispstring(docstring);
}

/*
  cstring - converts a Lisp string to a C string in buffer and returns buffer
  Handles Lisp strings packed two characters per 16-bit word, or four characters per 32-bit word
*/
char *cstring (builtin_t name, object *form, char *buffer, int buflen) {
  form = cdr(checkstring(name, form));
  int index = 0;
  while (form != NULL) {
    int chars = form->integer;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (ch) {
        if (index >= buflen-1) error2(NIL, PSTR("no room for string"));
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
IPAddress ipstring (builtin_t name, object *form) {
  form = cdr(checkstring(name, form));
  int p = 0;
  union { uint32_t ipaddress; uint8_t ipbytes[4]; } ;
  ipaddress = 0;
  while (form != NULL) {
    int chars = form->integer;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (ch) {
        if (ch == '.') { p++; if (p > 3) error2(name, PSTR("illegal IP address")); }
        else ipbytes[p] = (ipbytes[p] * 10) + ch - '0';
      }
    }
    form = car(form);
  }
  return ipaddress;
}

// Lookup variable in environment

object *value (symbol_t n, object *env) {
  while (env != NULL) {
    object *pair = car(env);
    if (pair != NULL && car(pair)->name == n) return pair;
    env = cdr(env);
  }
  return nil;
}

/*
  findpair - returns the (var . value) pair bound to variable var in the local or global environment
*/
object *findpair (object *var, object *env) {
  symbol_t name = var->name;
  object *pair = value(name, env);
  if (pair == NULL) pair = value(name, GlobalEnv);
  return pair;
}

/*
  boundp - tests whether var is bound to a value
*/
bool boundp (object *var, object *env) {
  return (findpair(var, env) != NULL);
}

/*
  findvalue - returns the value bound to variable var, or givesn an error of unbound
*/
object *findvalue (builtin_t name, object *var, object *env) {
  object *pair = findpair(var, env);
  if (pair == NULL) error(name, PSTR("unknown variable"), var);
  return pair;
}

// Handling closures

object *closure (int tc, symbol_t name, object *function, object *args, object **env) {
  object *state = car(function);
  function = cdr(function);
  int trace = 0;
  if (name) trace = tracing(name);
  if (trace) {
    indent(TraceDepth[trace-1]<<1, ' ', pserial);
    pint(TraceDepth[trace-1]++, pserial);
    pserial(':'); pserial(' '); pserial('('); printsymbol(symbol(name), pserial);
  }
  object *params = first(function);
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
    object *pair = first(state);
    push(pair, *env);
    state = cdr(state);
  }
  // Add arguments to environment
  bool optional = false;
  while (params != NULL) {
    object *value;
    object *var = first(params);
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

object *apply (builtin_t name, object *function, object *args, object *env) {
  if (symbolp(function)) {
    builtin_t fname = builtin(function->name);
    if ((fname > FUNCTIONS) && (fname < KEYWORDS)) {
      checkargs(fname, args);
      return ((fn_ptr_type)lookupfn(fname))(args, env);
    } else function = eval(function, env);
  }
  if (consp(function) && isbuiltin(car(function), LAMBDA)) {
    object *result = closure(0, sym(name), function, args, &env);
    return eval(result, env);
  }
  if (consp(function) && isbuiltin(car(function), CLOSURE)) {
    function = cdr(function);
    object *result = closure(0, sym(name), function, args, &env);
    return eval(result, env);
  }
  error(name, PSTR("illegal function"), function);
  return NULL;
}

// In-place operations

/*
  place - returns a pointer to an object referenced in the second argument of an
  in-place operation such as setf. bit is used to indicate the bit position in a bit array
*/
object **place (builtin_t name, object *args, object *env, int *bit) {
  *bit = -1;
  if (atom(args)) return &cdr(findvalue(name, args, env));
  object* function = first(args);
  if (symbolp(function)) {
    symbol_t sname = function->name;
    if (sname == sym(CAR) || sname == sym(FIRST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(name, canttakecar, value);
      return &car(value);
    }
    if (sname == sym(CDR) || sname == sym(REST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(name, canttakecdr, value);
      return &cdr(value);
    }
    if (sname == sym(NTH)) {
      int index = checkinteger(NTH, eval(second(args), env));
      object *list = eval(third(args), env);
      if (atom(list)) error(name, PSTR("second argument to nth is not a list"), list);
      while (index > 0) {
        list = cdr(list);
        if (list == NULL) error2(name, PSTR("index to nth is out of range"));
        index--;
      }
      return &car(list);
    }
    if (sname == sym(AREF)) {
      object *array = eval(second(args), env);
      if (!arrayp(array)) error(AREF, PSTR("first argument is not an array"), array);
      return getarray(AREF, array, cddr(args), env, bit);
    }
  }
  error2(name, PSTR("illegal place"));
  return nil;
}

// Checked car and cdr

/*
  carx - car with error checking
*/
object *carx (object *arg) {
  if (!listp(arg)) error(NIL, canttakecar, arg);
  if (arg == nil) return nil;
  return car(arg);
}

/*
  cdrx - cdr with error checking
*/
object *cdrx (object *arg) {
  if (!listp(arg)) error(NIL, canttakecdr, arg);
  if (arg == nil) return nil;
  return cdr(arg);
}

/*
  cxxxr - implements a general cxxxr function, 
  pattern is a sequence of bits 0b1xxx where x is 0 for a and 1 for d.
*/
object *cxxxr (object *args, uint8_t pattern) {
  object *arg = first(args);
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
void mapcarfun (object *result, object **tail) {
  object *obj = cons(result,NULL);
  cdr(*tail) = obj; *tail = obj;
}

/*
  mapcanfun - function specifying how to combine the results in mapcan
*/
void mapcanfun (object *result, object **tail) {
  if (cdr(*tail) != NULL) error(MAPCAN, notproper, *tail);
  while (consp(result)) {
    cdr(*tail) = result; *tail = result;
    result = cdr(result);
  }
}

/*
  mapcarcan - function used by marcar and mapcan
  It takes the name of the function, the arguments, the env, and a function specifying how the results are combined.
*/
object *mapcarcan (builtin_t name, object *args, object *env, mapfun_t fun) {
  object *function = first(args);
  args = cdr(args);
  object *params = cons(NULL, NULL);
  push(params,GCStack);
  object *head = cons(NULL, NULL);
  push(head,GCStack);
  object *tail = head;
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         pop(GCStack);
         pop(GCStack);
         return cdr(head);
      }
      if (improperp(list)) error(name, notproper, list);
      object *obj = cons(first(list),NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    object *result = apply(name, function, cdr(params), env);
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
File SDpfile, SDgfile;
inline int SDread () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  return SDgfile.read();
}

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
  else error(WITHSERIAL, PSTR("port not supported"), number(address));
}

void serialend (int address) {
  if (address == 1) {Serial1.flush(); Serial1.end(); }
}

gfun_t gstreamfun (object *args) {
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
  else if (streamtype == SDSTREAM) gfun = (gfun_t)SDread;
  else if (streamtype == WIFISTREAM) gfun = (gfun_t)WiFiread;
  else error2(NIL, PSTR("unknown stream type"));
  return gfun;
}

inline void spiwrite (char c) { SPI.transfer(c); }
inline void serial1write (char c) { Serial1.write(c); }
inline void WiFiwrite (char c) { client.write(c); }
inline void SDwrite (char c) { SDpfile.write(c); }
#if defined(gfxsupport)
inline void gfxwrite (char c) { tft.write(c); tft.display(); }
#endif

pfun_t pstreamfun (object *args) {
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
  else if (streamtype == SDSTREAM) pfun = (pfun_t)SDwrite;
  #if defined(gfxsupport)
  else if (streamtype == GFXSTREAM) pfun = (pfun_t)gfxwrite;
  #endif
  else if (streamtype == WIFISTREAM) pfun = (pfun_t)WiFiwrite;
  else error2(NIL, PSTR("unknown stream type"));
  return pfun;
}

// Check pins

void checkanalogread (int pin) {
  if (!(pin==0 || pin==2 || pin==4 || (pin>=12 && pin<=15) || (pin>=25 && pin<=27) || (pin>=32 && pin<=36) || pin==39))
    error(ANALOGREAD, PSTR("invalid pin"), number(pin));
}

void checkanalogwrite (int pin) {
  if (!(pin>=25 && pin<=26)) error(ANALOGWRITE, PSTR("invalid pin"), number(pin));
}

// Note

const int scale[] PROGMEM = {4186,4435,4699,4978,5274,5588,5920,6272,6645,7040,7459,7902};

void playnote (int pin, int note, int octave) {
  int prescaler = 8 - octave - note/12;
  if (prescaler<0 || prescaler>8) error(NOTE, PSTR("octave out of range"), number(prescaler));
  tone(pin, pgm_read_word(&scale[note%12])>>prescaler);
}

void nonote (int pin) {
  noTone(pin);
}

// Sleep

void initsleep () { }

void sleep (int secs) {
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
uint8_t atomwidth (object *obj) {
  PrintCount = 0;
  printobject(obj, pcount);
  return PrintCount;
}

uint8_t basewidth (object *obj, uint8_t base) {
  PrintCount = 0;
  pintbase(obj->integer, base, pcount);
  return PrintCount;
}

bool quoted (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(QUOTE) && consp(cdr(obj)) && cddr(obj) == NULL);
}

bool quasiquoted (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(QUASIQUOTE) && consp(cdr(obj)) && cddr(obj) == NULL);
}

bool macrop (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(MACRO));
}

int subwidth (object *obj, int w) {
  if (atom(obj)) return w - atomwidth(obj);
  if (quoted(obj)) obj = car(cdr(obj));
  return subwidthlist(obj, w - 1);
}

int subwidthlist (object *form, int w) {
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
void superprint (object *form, int lm, pfun_t pfun) {
  if (atom(form)) {
    if (symbolp(form) && form->name == sym(NOTHING)) printsymbol(form, pfun);
    else printobject(form, pfun);
  }
  else if (quoted(form)) { pfun('\''); superprint(car(cdr(form)), lm + 1, pfun); }
  else if (quasiquoted(form)) { pfun('`'); superprint(car(cdr(form)), lm + 1, pfun); }
  else if (subwidth(form, ppwidth - lm) >= 0) supersub(form, lm + PPINDENT, 0, pfun);
  else supersub(form, lm + PPINDENT, 1, pfun);
}

const int ppspecials = 22;
const char ppspecial[ppspecials] PROGMEM =
  { DOTIMES, DOLIST, IF, SETQ, TEE, LET, LETSTAR, LAMBDA, WHEN, UNLESS, WITHI2C, WITHSERIAL, WITHSPI, WITHSDCARD, FORMILLIS, WITHOUTPUTTOSTRING, DEFVAR, CASE, WITHGFX, WITHCLIENT, UNWINDPROTECT, IGNOREERRORS/*, SP_ERROR*/ };

/*
  supersub - subroutine used by pprint
*/
void supersub (object *form, int lm, int super, pfun_t pfun) {
  int special = 0, separate = 1;
  object *arg = car(form);
  if (symbolp(arg)) {
    symbol_t sname = arg->name;
    if (sname == sym(DEFUN)) special = 2;
    else for (int i=0; i<ppspecials; i++) {
      if (sname == sym((builtin_t)pgm_read_byte(&ppspecial[i]))) { special = 1; break; }
    } 
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
object *edit (object *fun) {
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

object *sp_quote (object *args, object *env) {
  (void) env;
  checkargs(QUOTE, args);
  return first(args);
}

/*
  (or item*)
  Evaluates its arguments until one returns non-nil, and returns its value.
*/
object *sp_or (object *args, object *env) {
  while (args != NULL) {
    object *val = eval(car(args), env);
    if (val != NULL) return val;
    args = cdr(args);
  }
  return nil;
}

/*
  (defun name (parameters) form*)
  Defines a function.
*/
object *sp_defun (object *args, object *env) {
  (void) env;
  checkargs(DEFUN, args);
  object *var = first(args);
  if (!symbolp(var)) error(DEFUN, notasymbol, var);
  object *val = cons(bsymbol(LAMBDA), cdr(args));
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

/*
  (defvar variable form)
  Defines a global variable.
*/
object *sp_defvar (object *args, object *env) {
  checkargs(DEFVAR, args);
  object *var = first(args);
  if (!symbolp(var)) error(DEFVAR, notasymbol, var);
  object *val = NULL;
  args = cdr(args);
  if (args != NULL) { setflag(NOESC); val = eval(first(args), env); clrflag(NOESC); }
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

/*
  (setq symbol value [symbol value]*)
  For each pair of arguments assigns the value of the second argument
  to the variable specified in the first argument.
*/
object *sp_setq (object *args, object *env) {
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) error2(SETQ, oddargs);
    object *pair = findvalue(SETQ, first(args), env);
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
object *sp_loop (object *args, object *env) {
  object *start = args;
  for (;;) {
    yield();
    args = start;
    while (args != NULL) {
      object *result = eval(car(args),env);
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
object *sp_return (object *args, object *env) {
  object *result = eval(tf_progn(args,env), env);
  setflag(RETURNFLAG);
  return result;
}

/*
  (push item place)
  Modifies the value of place, which should be a list, to add item onto the front of the list,
  and returns the new list.
*/
object *sp_push (object *args, object *env) {
  int bit;
  checkargs(PUSH, args);
  object *item = eval(first(args), env);
  object **loc = place(PUSH, second(args), env, &bit);
  push(item, *loc);
  return *loc;
}

/*
  (pop place)
  Modifies the value of place, which should be a list, to remove its first item, and returns that item.
*/
object *sp_pop (object *args, object *env) {
  int bit;
  checkargs(POP, args);
  object **loc = place(POP, first(args), env, &bit);
  object *result = car(*loc);
  pop(*loc);
  return result;
}

// Accessors

/*
  (incf place [number])
  Increments a place, which should have an numeric value, and returns the result.
  The third argument is an optional increment which defaults to 1.
*/
object *sp_incf (object *args, object *env) {
  int bit;
  checkargs(INCF, args);
  object **loc = place(INCF, first(args), env, &bit);
  args = cdr(args);

  object *x = *loc;
  object *inc = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int increment;
    if (inc == NULL) increment = 1; else increment = checkbitvalue(INCF, inc);
    int newvalue = (((*loc)->integer)>>bit & 1) + increment;

    if (newvalue & ~1) error2(INCF, PSTR("result is not a bit value"));
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(inc)) {
    float increment;
    float value = checkintfloat(INCF, x);

    if (inc == NULL) increment = 1.0; else increment = checkintfloat(INCF, inc);

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
  } else error2(INCF, notanumber);
  return *loc;
}

/*
  (decf place [number])
  Decrements a place, which should have an numeric value, and returns the result.
  The third argument is an optional decrement which defaults to 1.
*/
object *sp_decf (object *args, object *env) {
  int bit;
  checkargs(DECF, args);
  object **loc = place(DECF, first(args), env, &bit);
  args = cdr(args);

  object *x = *loc;
  object *dec = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int decrement;
    if (dec == NULL) decrement = 1; else decrement = checkbitvalue(DECF, dec);
    int newvalue = (((*loc)->integer)>>bit & 1) - decrement;

    if (newvalue & ~1) error2(INCF, PSTR("result is not a bit value"));
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(dec)) {
    float decrement;
    float value = checkintfloat(DECF, x);

    if (dec == NULL) decrement = 1.0; else decrement = checkintfloat(DECF, dec);

    *loc = makefloat(value - decrement);
  } if (integerp(x) && (integerp(dec) || dec == NULL)) {
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
  } else error2(DECF, notanumber);
  return *loc;
}

/*
  (setf place value [place value]*)
  For each pair of arguments modifies a place to the result of evaluating value.
*/
object *sp_setf (object *args, object *env) {
  int bit;
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) error2(SETF, oddargs);
    object **loc = place(SETF, first(args), env, &bit);
    arg = eval(second(args), env);
    if (bit == -1) *loc = arg;
    else *loc = number((checkinteger(SETF,*loc) & ~(1<<bit)) | checkbitvalue(SETF,arg)<<bit);
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
object *sp_dolist (object *args, object *env) {
  if (args == NULL || listlength(DOLIST, first(args)) < 2) error2(DOLIST, noargument);
  object *params = first(args);
  object *var = first(params);
  object *list = eval(second(params), env);
  push(list, GCStack); // Don't GC the list
  object *pair = cons(var,nil);
  push(pair,env);
  params = cdr(cdr(params));
  args = cdr(args);
  while (list != NULL) {
    if (improperp(list)) error(DOLIST, notproper, list);
    cdr(pair) = first(list);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
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
object *sp_dotimes (object *args, object *env) {
  if (args == NULL || listlength(DOTIMES, first(args)) < 2) error2(DOTIMES, noargument);
  object *params = first(args);
  object *var = first(params);
  int count = checkinteger(DOTIMES, eval(second(params), env));
  int index = 0;
  params = cdr(cdr(params));
  object *pair = cons(var,number(0));
  push(pair,env);
  args = cdr(args);
  while (index < count) {
    cdr(pair) = number(index);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
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
object *sp_trace (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *var = first(args);
    if (!symbolp(var)) error(TRACE, notasymbol, var);
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
object *sp_untrace (object *args, object *env) {
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
      object *var = first(args);
      if (!symbolp(var)) error(UNTRACE, notasymbol, var);
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
object *sp_formillis (object *args, object *env) {
  if (args == NULL) error2(FORMILLIS, noargument);
  object *param = first(args);
  unsigned long start = millis();
  unsigned long now, total = 0;
  if (param != NULL) total = checkinteger(FORMILLIS, eval(first(param), env));
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
object *sp_time (object *args, object *env) {
  unsigned long start = millis();
  object *result = eval(first(args), env);
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
object *sp_withoutputtostring (object *args, object *env) {
  if (args == NULL) error2(WITHOUTPUTTOSTRING, noargument);
  object *params = first(args);
  if (params == NULL) error2(WITHOUTPUTTOSTRING, nostream);
  object *var = first(params);
  object *pair = cons(var, stream(STRINGSTREAM, 0));
  push(pair,env);
  object *string = startstring(WITHOUTPUTTOSTRING);
  push(string, GCStack);
  object *forms = cdr(args);
  eval(tf_progn(forms,env), env);
  pop(GCStack);
  return string;
}

/*
  (with-serial (str port [baud]) form*)
  Evaluates the forms with str bound to a serial-stream using port.
  The optional baud gives the baud rate divided by 100, default 96.
*/
object *sp_withserial (object *args, object *env) {
  object *params = first(args);
  if (params == NULL) error2(WITHSERIAL, nostream);
  object *var = first(params);
  int address = checkinteger(WITHSERIAL, eval(second(params), env));
  params = cddr(params);
  int baud = 96;
  if (params != NULL) baud = checkinteger(WITHSERIAL, eval(first(params), env));
  object *pair = cons(var, stream(SERIALSTREAM, address));
  push(pair,env);
  serialbegin(address, baud);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  serialend(address);
  return result;
}

/*
  (with-i2c (str [port] address [read-p]) form*)
  Evaluates the forms with str bound to an i2c-stream defined by address.
  If read-p is nil or omitted the stream is written to, otherwise it specifies the number of bytes
  to be read from the stream. The port if specified is ignored.
*/
object *sp_withi2c (object *args, object *env) {
  object *params = first(args);
  if (params == NULL) error2(WITHI2C, nostream);
  object *var = first(params);
  int address = checkinteger(WITHI2C, eval(second(params), env));
  params = cddr(params);
  if (address == 0 && params != NULL) params = cdr(params); // Ignore port
  int read = 0; // Write
  I2Ccount = 0;
  if (params != NULL) {
    object *rw = eval(first(params), env);
    if (integerp(rw)) I2Ccount = rw->integer;
    read = (rw != NULL);
  }
  I2Cinit(1); // Pullups
  object *pair = cons(var, (I2Cstart(address, read)) ? stream(I2CSTREAM, address) : nil);
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  I2Cstop(read);
  return result;
}

/*
  (with-spi (str pin [clock] [bitorder] [mode]) form*)
  Evaluates the forms with str bound to an spi-stream.
  The parameters specify the enable pin, clock in kHz (default 4000),
  bitorder 0 for LSBFIRST and 1 for MSBFIRST (default 1), and SPI mode (default 0).
*/
object *sp_withspi (object *args, object *env) {
  object *params = first(args);
  if (params == NULL) error2(WITHSPI, nostream);
  object *var = first(params);
  params = cdr(params);
  if (params == NULL) error2(WITHSPI, nostream);
  int pin = checkinteger(WITHSPI, eval(car(params), env));
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  params = cdr(params);
  int clock = 4000, mode = SPI_MODE0; // Defaults
  int bitorder = MSBFIRST;
  if (params != NULL) {
    clock = checkinteger(WITHSPI, eval(car(params), env));
    params = cdr(params);
    if (params != NULL) {
      bitorder = (checkinteger(WITHSPI, eval(car(params), env)) == 0) ? LSBFIRST : MSBFIRST;
      params = cdr(params);
      if (params != NULL) {
        int modeval = checkinteger(WITHSPI, eval(car(params), env));
        mode = (modeval == 3) ? SPI_MODE3 : (modeval == 2) ? SPI_MODE2 : (modeval == 1) ? SPI_MODE1 : SPI_MODE0;
      }
    }
  }
  object *pair = cons(var, stream(SPISTREAM, pin));
  push(pair,env);
  SPI.begin();
  SPI.beginTransaction(SPISettings(((unsigned long)clock * 1000), bitorder, mode));
  digitalWrite(pin, LOW);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  digitalWrite(pin, HIGH);
  SPI.endTransaction();
  return result;
}

/*
  (with-sd-card (str filename [mode]) form*)
  Evaluates the forms with str bound to an sd-stream reading from or writing to the file filename.
  If mode is omitted the file is read, otherwise 0 means read, 1 write-append, or 2 write-overwrite.
*/
object *sp_withsdcard (object *args, object *env) {
  object *params = first(args);
  object *var = first(params);
  object *filename = eval(second(params), env);
  params = cddr(params);
  SD.begin();
  int mode = 0;
  if (params != NULL && first(params) != NULL) mode = checkinteger(WITHSDCARD, first(params));
  const char *oflag = FILE_READ;
  if (mode == 1) oflag = FILE_APPEND; else if (mode == 2) oflag = FILE_WRITE;
  if (mode >= 1) {
    char buffer[BUFFERSIZE];
    SDpfile = SD.open(MakeFilename(filename, buffer), oflag);
    if (!SDpfile) error2(WITHSDCARD, PSTR("problem writing to SD card or invalid filename"));
  } else {
    char buffer[BUFFERSIZE];
    SDgfile = SD.open(MakeFilename(filename, buffer), oflag);
    if (!SDgfile) error2(WITHSDCARD, PSTR("problem reading from SD card or invalid filename"));
  }
  object *pair = cons(var, stream(SDSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  if (mode >= 1) SDpfile.close(); else SDgfile.close();
  return result;
}

/*
  (with-gfx (str) form*)
  Evaluates the forms with str bound to an gfx-stream so you can print text
  to the graphics display using the standard uLisp print commands.
*/
object *sp_withgfx (object *args, object *env) {
#if defined(gfxsupport)
  object *params = first(args);
  object *var = first(params);
  object *pair = cons(var, stream(GFXSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  return result;
#else
  (void) args, (void) env;
  error2(WITHGFX, PSTR("not supported"));
  return nil;
#endif
}

/*
  (with-client (str [address port]) form*)
  Evaluates the forms with str bound to a wifi-stream.
*/
object *sp_withclient (object *args, object *env) {
  object *params = first(args);
  object *var = first(params);
  char buffer[BUFFERSIZE];
  params = cdr(params);
  int n;
  if (params == NULL) {
    client = server.available();
    if (!client) return nil;
    n = 2;
  } else {
    object *address = eval(first(params), env);
    object *port = eval(second(params), env);
    int success;
    if (stringp(address)) success = client.connect(cstring(WITHCLIENT, address, buffer, BUFFERSIZE), checkinteger(WITHCLIENT, port));
    else if (integerp(address)) success = client.connect(address->integer, checkinteger(WITHCLIENT, port));
    else error2(WITHCLIENT, PSTR("invalid address"));
    if (!success) return nil;
    n = 1;
  }
  object *pair = cons(var, stream(WIFISTREAM, n));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  client.stop();
  return result;
}

/*
  unwind-protect - runs first form, if it errors, stores the error in GlobalErrorString and runs the rest of the forms.
*/
object *sp_unwindprotect (object *args, object *env) {
  checkargs(UNWINDPROTECT, args);
  object *current_GCStack = GCStack;
  jmp_buf dynamic_errorbuffer;
  jmp_buf *previous_errorbuffer = errorbuffer;
  errorbuffer = &dynamic_errorbuffer;
  object *protected_form = first(args);
  object *result;
  boolean oldmuffle = tstflag(MUFFLEERRORS);
  setflag(MUFFLEERRORS);
  if (!setjmp(dynamic_errorbuffer)) {
    result = eval(protected_form, env);
    current_GCStack = NULL;
  } else {
    GCStack = current_GCStack;
    errorbuffer = previous_errorbuffer;
  }
  if (!oldmuffle) clrflag(MUFFLEERRORS);
  object *finally_forms = cdr(args);
  while (finally_forms != NULL) {
    eval(car(finally_forms),env);
    if (tstflag(RETURNFLAG)) break;
    finally_forms = cdr(finally_forms);
  }
  if (current_GCStack) longjmp(*errorbuffer, 1);
  else return result;
}

/*
  ignore-errors - catches errors and stores them in GlobalErrorString.
*/
object *sp_ignoreerrors (object *args, object *env) {
  checkargs(IGNOREERRORS, args);
  object *current_GCStack = GCStack;
  jmp_buf dynamic_errorbuffer;
  jmp_buf *previous_errorbuffer = errorbuffer;
  errorbuffer = &dynamic_errorbuffer;
  object *result = nil;
  boolean oldmuffle = tstflag(MUFFLEERRORS);
  setflag(MUFFLEERRORS);
  if (!setjmp(dynamic_errorbuffer)) {
    while (args != NULL) {
      result = eval(car(args),env);
      if (tstflag(RETURNFLAG)) break;
      args = cdr(args);
    }
    current_GCStack = NULL;
  } else {
    GCStack = current_GCStack;
    errorbuffer = previous_errorbuffer;
  }
  if (!oldmuffle) clrflag(MUFFLEERRORS);
  if (current_GCStack) return symbol(NOTHING);
  else return result;
}

/*
  error - formats the string as with (format) and then throws the error.
*/
//object *sp_error (object *args, object *env) {
//  checkargs(SP_ERROR, args);
//  object *message = eval(cons(symbol(FORMAT), cons(nil, args)), env);
//  if (!tstflag(MUFFLEERRORS)) {
//    pfstring(PSTR("Error: "), pserial); printstring(message, pserial);
//    pln(pserial);
//  }
//  GlobalErrorString = message;
//  GCStack = NULL;
//  longjmp(*errorbuffer, 1);
//}

// Macro support

/*
  defmacro - defines a new macro.
*/
object *sp_defmacro (object *args, object *env) {
  (void) env;
  checkargs(DEFMACRO, args);
  object *var = first(args);
  if (!symbolp(var)) error(DEFMACRO, notasymbol, var);
  object *val = cons(bsymbol(MACRO), cdr(args));
  object *pair = value(var->name,GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

/*
    reverse_it - reverses a cons list.
*/
object *reverse_it (object *l) {
  object *reversed = NULL;
  push(reversed, GCStack);
  while (l != NULL) {
    push(first(l),reversed);
    l = cdr(l);
  }
  pop(GCStack);
  return reversed;
}

object *reverse_and_flatten (object *expr) {
  if (!consp(expr))
    return expr;
  
  object *reversed = reverse_it(expr);

  object *result = NULL;
  push(result, GCStack);
  for (object *cell = reversed; cell != NULL; cell = cdr(cell)) {
    if (!consp(car(cell))) {
      push(car(cell), result);
    } else {
      for (object *subcell = car(cell); subcell != NULL; subcell = cdr(subcell)) {
        push(car(subcell), result);
      }
    }
  }
  pop(GCStack);
  return reverse_it(result);
}

#define ATNOTHINGS (object*)-1

/*
  process_quasiquoted - evals the unquotes inside a quasiquote form.
*/
object *process_quasiquoted (object *expr, int level, object *env) {
  // Serial.print("**** Processing quasiquote of : ");
  // printobject(expr, pserial);
  // Serial.println();
  // Serial.print("**** at level ");
  // Serial.println(level);
  if (!consp(expr)) return cons(expr, NULL);

  if (isbuiltin(car(expr), QUASIQUOTE)) {
    // Serial.println("nested quasiquote");
    push(second(expr),GCStack);
    object *processed = process_quasiquoted(second(expr), level + 1, env);
    pop(GCStack);
    return cons(cons(symbol(QUASIQUOTE), processed), NULL);
  } else if (isbuiltin(car(expr), UNQUOTE)) {
    // Serial.println("**** Processing UNQUOTE");
    // Serial.print("**** At level ");
    // Serial.println(level);
    if (level == 1) {
      push(second(expr),GCStack);
      object *processed = process_quasiquoted(second(expr), level, env);
      push(car(processed),GCStack);
      object *result = eval(car(processed), env);
      // Serial.print("**** Result: ");
      // printobject(result, pserial);
      // Serial.println();
      pop(GCStack);
      pop(GCStack);
      return cons(result, NULL);
    } else {
      push(second(expr),GCStack);
      object *processed = process_quasiquoted(second(expr), level - 1, env);
      pop(GCStack);
      return cons(cons(symbol(UNQUOTE), processed), NULL);
    }
  } else if (isbuiltin(car(expr), UNQUOTESPLICING)) {
    // Serial.println("**** Processing UNQUOTESPLICING");
    // Serial.print("**** At level ");
    // Serial.println(level);
    if (level == 1) {
      push(second(expr),GCStack);
      object *processed = process_quasiquoted(second(expr), level, env);
      push(car(processed),GCStack);
      // Serial.print("**** Processed: ");
      // printobject(car(processed), pserial);
      // Serial.println();
      object *result = eval(car(processed), env);
      // Serial.print("**** Result: ");
      // printobject(result, pserial);
      // Serial.println();
      pop(GCStack);
      pop(GCStack);
      if (result == nil) return ATNOTHINGS;     // sentinel to signal that @... should insert nothing (i.e. empty list)
      else return result;
    } else {
      push(second(expr), GCStack);
      object *processed = process_quasiquoted(second(expr), level - 1, env);
      pop(GCStack);
      return cons(cons(symbol(UNQUOTESPLICING), processed), NULL);
    }
  } else {
    // Serial.println("Processing something else");
    // Serial.print("**** At level ");
    // Serial.println(level);
    object *parts = NULL;
    push(parts, GCStack);
    for (object *cell = expr; cell != NULL; cell = cdr(cell)) {
      push(car(cell), GCStack);
      object *processed = process_quasiquoted(car(cell), level, env);
      pop(GCStack);
      if (processed != ATNOTHINGS) { // Check for empty list insertion sentinel
        push(processed, parts);
      }
    }
    // Serial.print("**** parts: ");
    // printobject(parts, pserial);
    // Serial.println();

    object *result = reverse_and_flatten(parts);
    // Serial.print("**** Result: ");
    // printobject(result, pserial);
    // Serial.println();
    pop(GCStack);
    return cons(result, NULL);
  }


}

/*
  (quasiquote x) or `x - Lisp quasiquotes.
*/
object *sp_quasiquote (object *args, object *env) {
  checkargs(QUASIQUOTE, args);
  object *result = process_quasiquoted(car(args), 1, env);
  return car(result);
}

/*
  (unquote x) or ,x - Lisp unquotes. Cannot be used outside of (quasiquote).
*/
object *sp_unquote (object *args, object *env) {
  (void)args, (void)env;
  error2(UNQUOTE, PSTR("not supported outside a quasiquote form"));
  return nil; // unreachable, just to satisfy compiler
}

/*
  (unquote-splicing x) or ,@x - Lisp unquotes. Cannot be used outside of (quasiquote).
*/
object *sp_unquote_splicing (object *args, object *env) {
  (void)args, (void)env;
  error2(UNQUOTESPLICING, PSTR("not supported outside a quasiquote form"));
  return nil; // unreachable, just to satisfy compiler
}

object *expand (object *body, object *env) {
  return eval(body, env);
}

//MOVE arg evaluation to sp_expand
/*
  (expand macro params body) - Expand a macro.
*/
object *sp_expand (object *args, object *env) {
  object *macro = eval(car(args), env);
  if (!macrop(macro)) {
    error(EXPAND, PSTR("needs a macro"), macro);
  }
  object *params = car(cdr(macro));
  object *body = car(cdr(cdr(macro)));
  if (!consp(params) || !consp(cdr(args))) {
    error2(EXPAND, PSTR("params and args must be lists"));
  }

  bool contains_rest = false;
  for (object *p = params; p != nil; p = cdr(p)) {
    if (isbuiltin(car(p), AMPREST)) {
      contains_rest = true;
      break;
    }
  }

  if (!contains_rest && listlength(EXPAND, params) != listlength(EXPAND, cdr(args))) {
    error2(EXPAND, PSTR("different number of params and args"));
  }
  // add params->args to newenv
  object *newenv = env;
  push(newenv, GCStack);
  object *p;
  object *a;
  for (p = params, a = cdr(args); p != NULL; p = cdr(p), a = cdr(a)) {
    if (isbuiltin(car(p), AMPREST)) {
      push(cons(second(p), a), newenv);
      car(GCStack) = newenv;
      break;
    } else {
      push(cons(car(p), eval(car(a), env)), newenv);
      // push(cons(car(p), car(a)), newenv);
      car(GCStack) = newenv;
    }
  }
  object *expanded = expand(body, newenv);
  pop(GCStack);
  return expanded;
}

// Tail-recursive forms

/*
  (progn form*)
  Evaluates several forms grouped together into a block, and returns the result of evaluating the last form.
*/
object *tf_progn (object *args, object *env) {
  if (args == NULL) return nil;
  object *more = cdr(args);
  while (more != NULL) {
    object *result = eval(car(args),env);
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
object *tf_if (object *args, object *env) {
  if (args == NULL || cdr(args) == NULL) error2(IF, toofewargs);
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
object *tf_cond (object *args, object *env) {
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(COND, illegalclause, clause);
    object *test = eval(first(clause), env);
    object *forms = cdr(clause);
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
object *tf_when (object *args, object *env) {
  if (args == NULL) error2(WHEN, noargument);
  if (eval(first(args), env) != nil) return tf_progn(cdr(args),env);
  else return nil;
}

/*
  (unless test form*)
  Evaluates the test. If it's nil the forms are evaluated and the last value is returned.
*/
object *tf_unless (object *args, object *env) {
  if (args == NULL) error2(UNLESS, noargument);
  if (eval(first(args), env) != nil) return nil;
  else return tf_progn(cdr(args),env);
}

/*
  (case keyform ((key form*) (key form*) ... ))
  Evaluates a keyform to produce a test key, and then tests this against a series of arguments,
  each of which is a list containing a key optionally followed by one or more forms.
*/
object *tf_case (object *args, object *env) {
  object *test = eval(first(args), env);
  args = cdr(args);
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(CASE, illegalclause, clause);
    object *key = car(clause);
    object *forms = cdr(clause);
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
object *tf_and (object *args, object *env) {
  if (args == NULL) return tee;
  object *more = cdr(args);
  while (more != NULL) {
    if (eval(car(args), env) == NULL) return nil;
    args = more;
    more = cdr(args);
  }
  return car(args);
}

/*
  (? item)
  Prints the documentation string of a built-in or user-defined function.
*/
object *tf_help (object *args, object *env) {
  if (args == NULL) error2(HELP, noargument);
  object *docstring = documentation(HELP, first(args), env);
  if (docstring) {
    char temp = Flags;
    clrflag(PRINTREADABLY);
    printstring(docstring, pserial);
    Flags = temp;
  }
  return bsymbol(NOTHING);
}

// Core functions

/*
  get-error - returns the GlobalErrorString containing the last error.
*/
object *fn_geterror (object *args, object *env) {
  (void)args, (void)env;
  return GlobalErrorString;
}

/*
  (throw message) - throws an error with the message.
*/
object *fn_throw (object *args, object *env) {
  checkargs(THROW, args);
  object *message = first(args);
  if (!stringp(message)) error(THROW, notastring, message);
  if (!tstflag(MUFFLEERRORS)) {
    uint16_t temp = Flags;
    clrflag(PRINTREADABLY);
    pfstring(PSTR("Error: "), pserial); printstring(message, pserial);
    Flags = temp;
    pln(pserial);
  }
  GlobalErrorString = message;
  errorend();
  // unreachable, just to satisfy compiler
  return (object *)NULL;
}

/*
  (intern string) - returns a symbol with the same name as the string.
*/
object *fn_intern (object *args, object *env) {
  checkargs(INTERN, args);
  if (!stringp(car(args))) error(INTERN, notastring, car(args));
  char b[BUFFERSIZE];
  char *cs = cstring(INTERN, car(args), b, BUFFERSIZE);
  return internlong(cs);
}


/*
  (not item)
  Returns t if its argument is nil, or nil otherwise. Equivalent to null.
*/
object *fn_not (object *args, object *env) {
  (void) env;
  return (first(args) == nil) ? tee : nil;
}

/*
  (cons item item)
  If the second argument is a list, cons returns a new list with item added to the front of the list.
  If the second argument isn't a list cons returns a dotted pair.
*/
object *fn_cons (object *args, object *env) {
  (void) env;
  return cons(first(args), second(args));
}

/*
  (atom item)
  Returns t if its argument is a single number, symbol, or nil.
*/
object *fn_atom (object *args, object *env) {
  (void) env;
  return atom(first(args)) ? tee : nil;
}

/*
  (listp item)
  Returns t if its argument is a list.
*/
object *fn_listp (object *args, object *env) {
  (void) env;
  return listp(first(args)) ? tee : nil;
}

/*
  (consp item)
  Returns t if its argument is a non-null list.
*/
object *fn_consp (object *args, object *env) {
  (void) env;
  return consp(first(args)) ? tee : nil;
}

/*
  (symbolp item)
  Returns t if its argument is a symbol.
*/
object *fn_symbolp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (arg == NULL || symbolp(arg)) ? tee : nil;
}

/*
  (arrayp item)
  Returns t if its argument is an array.
*/
object *fn_arrayp (object *args, object *env) {
  (void) env;
  return arrayp(first(args)) ? tee : nil;
}

/*
  (boundp item)
  Returns t if its argument is a symbol with a value.
*/
object *fn_boundp (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(BOUNDP, notasymbol, var);
  return boundp(var, env) ? tee : nil;
}

/*
  (set symbol value [symbol value]*)
  For each pair of arguments, assigns the value of the second argument to the value of the first argument.
*/
object *fn_setfn (object *args, object *env) {
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) error2(SETFN, oddargs);
    object *pair = findvalue(SETFN, first(args), env);
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
object *fn_streamp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return streamp(arg) ? tee : nil;
}

/*
  (eq item item)
  Tests whether the two arguments are the same symbol, same character, equal numbers,
  or point to the same cons, and returns t or nil as appropriate.
*/
object *fn_eq (object *args, object *env) {
  (void) env;
  return eq(first(args), second(args)) ? tee : nil;
}

/*
  (equal item item)
  Tests whether the two arguments are the same symbol, same character, equal numbers,
  or have the same cons structure, and returns t or nil as appropriate.
*/
object *fn_equal (object *args, object *env) {
  (void) env;
  return equal(first(args), second(args)) ? tee : nil;
}

// List functions

/*
  (car list)
  Returns the first item in a list. 
*/
object *fn_car (object *args, object *env) {
  (void) env;
  return carx(first(args));
}

/*
  (cdr list)
  Returns a list with the first item removed.
*/
object *fn_cdr (object *args, object *env) {
  (void) env;
  return cdrx(first(args));
}

/*
  (caar list)
*/
object *fn_caar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b100);
}

/*
  (cadr list)
*/
object *fn_cadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b101);
}

/*
  (cdar list)
  Equivalent to (cdr (car list)).
*/
object *fn_cdar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b110);
}

/*
  (cddr list)
  Equivalent to (cdr (cdr list)).
*/
object *fn_cddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b111);
}

/*
  (caaar list)
  Equivalent to (car (car (car list))). 
*/
object *fn_caaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1000);
}

/*
  (caadr list)
  Equivalent to (car (car (cdar list))).
*/
object *fn_caadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1001);;
}

/*
  (cadar list)
  Equivalent to (car (cdr (car list))).
*/
object *fn_cadar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1010);
}

/*
  (caddr list)
  Equivalent to (car (cdr (cdr list))).
*/
object *fn_caddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1011);
}

/*
  (cdaar list)
  Equivalent to (cdar (car (car list))).
*/
object *fn_cdaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1100);
}

/*
  (cdadr list)
  Equivalent to (cdr (car (cdr list))).
*/
object *fn_cdadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1101);
}

/*
  (cddar list)
  Equivalent to (cdr (cdr (car list))).
*/
object *fn_cddar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1110);
}

/*
  (cdddr list)
  Equivalent to (cdr (cdr (cdr list))).
*/
object *fn_cdddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1111);
}

/*
  (length item)
  Returns the number of items in a list, the length of a string, or the length of a one-dimensional array.
*/
object *fn_length (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (listp(arg)) return number(listlength(LENGTH, arg));
  if (stringp(arg)) return number(stringlength(arg));
  if (!(arrayp(arg) && cdr(cddr(arg)) == NULL)) error(LENGTH, PSTR("argument is not a list, 1d array, or string"), arg);
  return number(abs(first(cddr(arg))->integer));
}

/*
  (array-dimensions item)
  Returns a list of the dimensions of an array.
*/
object *fn_arraydimensions (object *args, object *env) {
  (void) env;
  object *array = first(args);
  if (!arrayp(array)) error(ARRAYDIMENSIONS, PSTR("argument is not an array"), array);
  object *dimensions = cddr(array);
  return (first(dimensions)->integer < 0) ? cons(number(-(first(dimensions)->integer)), cdr(dimensions)) : dimensions;
}

/*
  (list item*)
  Returns a list of the values of its arguments.
*/
object *fn_list (object *args, object *env) {
  (void) env;
  return args;
}

/*
  (make-array size [:initial-element element] [:element-type 'bit])
  If size is an integer it creates a one-dimensional array with elements from 0 to size-1.
  If size is a list of n integers it creates an n-dimensional array with those dimensions.
  If :element-type 'bit is specified the array is a bit array.
*/
object *fn_makearray (object *args, object *env) {
  (void) env;
  object *def = nil;
  bool bitp = false;
  object *dims = first(args);
  if (dims == NULL) error2(MAKEARRAY, PSTR("dimensions can't be nil"));
  else if (atom(dims)) dims = cons(dims, NULL);
  args = cdr(args);
  while (args != NULL && cdr(args) != NULL) {
    object *var = first(args);
    if (isbuiltin(first(args), INITIALELEMENT)) def = second(args);
    else if (isbuiltin(first(args), ELEMENTTYPE) && isbuiltin(second(args), BIT)) bitp = true;
    else error(MAKEARRAY, PSTR("argument not recognised"), var);
    args = cddr(args);
  }
  if (bitp) {
    if (def == nil) def = number(0);
    else def = number(-checkbitvalue(MAKEARRAY, def)); // 1 becomes all ones
  }
  return makearray(MAKEARRAY, dims, def, bitp);
}

/*
  (reverse list)
  Returns a list with the elements of list in reverse order.
*/
object *fn_reverse (object *args, object *env) {
  (void) env;
  object *list = first(args);
  object *result = NULL;
  while (list != NULL) {
    if (improperp(list)) error(REVERSE, notproper, list);
    push(first(list),result);
    list = cdr(list);
  }
  return result;
}

/*
  (nth number list)
  Returns the nth item in list, counting from zero.
*/
object *fn_nth (object *args, object *env) {
  (void) env;
  int n = checkinteger(NTH, first(args));
  if (n < 0) error(NTH, indexnegative, first(args));
  object *list = second(args);
  while (list != NULL) {
    if (improperp(list)) error(NTH, notproper, list);
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
object *fn_aref (object *args, object *env) {
  (void) env;
  int bit;
  object *array = first(args);
  if (!arrayp(array)) error(AREF, PSTR("first argument is not an array"), array);
  object *loc = *getarray(AREF, array, cdr(args), 0, &bit);
  if (bit == -1) return loc;
  else return number((loc->integer)>>bit & 1);
}

/*
  (assoc key list)
  Looks up a key in an association list of (key . value) pairs,
  and returns the matching pair, or nil if no pair is found.
*/
object *fn_assoc (object *args, object *env) {
  (void) env;
  object *key = first(args);
  object *list = second(args);
  return assoc(key,list);
}

/*
  (member item list)
  Searches for an item in a list, using eq, and returns the list starting from the first occurrence of the item,
  or nil if it is not found.
*/
object *fn_member (object *args, object *env) {
  (void) env;
  object *item = first(args);
  object *list = second(args);
  while (list != NULL) {
    if (improperp(list)) error(MEMBER, notproper, list);
    if (eq(item,car(list))) return list;
    list = cdr(list);
  }
  return nil;
}

/*
  (apply function list)
  Returns the result of evaluating function, with the list of arguments specified by the second parameter.
*/
object *fn_apply (object *args, object *env) {
  object *previous = NULL;
  object *last = args;
  while (cdr(last) != NULL) {
    previous = last;
    last = cdr(last);
  }
  object *arg = car(last);
  if (!listp(arg)) error(APPLY, notalist, arg);
  cdr(previous) = arg;
  return apply(APPLY, first(args), cdr(args), env);
}

/*
  (funcall function argument*)
  Evaluates function with the specified arguments.
*/
object *fn_funcall (object *args, object *env) {
  return apply(FUNCALL, first(args), cdr(args), env);
}

/*
  (append list*)
  Joins its arguments, which should be lists, into a single list.
*/
object *fn_append (object *args, object *env) {
  (void) env;
  object *head = NULL;
  object *tail;
  while (args != NULL) {
    object *list = first(args);
    if (!listp(list)) error(APPEND, notalist, list);
    while (consp(list)) {
      object *obj = cons(car(list), cdr(list));
      if (head == NULL) head = obj;
      else cdr(tail) = obj;
      tail = obj;
      list = cdr(list);
      if (cdr(args) != NULL && improperp(list)) error(APPEND, notproper, first(args));
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
object *fn_mapc (object *args, object *env) {
  object *function = first(args);
  args = cdr(args);
  object *result = first(args);
  object *params = cons(NULL, NULL);
  push(params,GCStack);
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         pop(GCStack);
         return result;
      }
      if (improperp(list)) error(MAPC, notproper, list);
      object *obj = cons(first(list),NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    apply(MAPC, function, cdr(params), env);
  }
}

/*
  (mapcar function list1 [list]*)
  Applies the function to each element in one or more lists, and returns the resulting list.
*/
object *fn_mapcar (object *args, object *env) {
  return mapcarcan(MAPCAR, args, env, mapcarfun);
}

/*
  (mapcan function list1 [list]*)
  Applies the function to each element in one or more lists. The results should be lists,
  and these are appended together to give the value returned.
*/
object *fn_mapcan (object *args, object *env) {
  return mapcarcan(MAPCAN, args, env, mapcanfun);
}

// Arithmetic functions

/*
  (+ number*)
  Adds its arguments together.
  If each argument is an integer, and the running total doesn't overflow, the result is an integer,
  otherwise a floating-point number.
*/
object *fn_add (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    object *arg = car(args);
    if (floatp(arg)) return add_floats(args, (float)result);
    else if (integerp(arg)) {
      int val = arg->integer;
      if (val < 1) { if (INT_MIN - val > result) return add_floats(args, (float)result); }
      else { if (INT_MAX - val < result) return add_floats(args, (float)result); }
      result = result + val;
    } else error(ADD, notanumber, arg);
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
object *fn_subtract (object *args, object *env) {
  (void) env;
  object *arg = car(args);
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
      } else error(SUBTRACT, notanumber, arg);
      args = cdr(args);
    }
    return number(result);
  } else error(SUBTRACT, notanumber, arg);
  return nil;
}

/*
  (* number*)
  Multiplies its arguments together.
  If each argument is an integer, and the running total doesn't overflow, the result is an integer,
  otherwise it's a floating-point number.
*/
object *fn_multiply (object *args, object *env) {
  (void) env;
  int result = 1;
  while (args != NULL){
    object *arg = car(args);
    if (floatp(arg)) return multiply_floats(args, result);
    else if (integerp(arg)) {
      int64_t val = result * (int64_t)(arg->integer);
      if ((val > INT_MAX) || (val < INT_MIN)) return multiply_floats(args, result);
      result = val;
    } else error(MULTIPLY, notanumber, arg);
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
object *fn_divide (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  args = cdr(args);
  // One argument
  if (args == NULL) {
    if (floatp(arg)) {
      float f = arg->single_float;
      if (f == 0.0) error2(DIVIDE, PSTR("division by zero"));
      return makefloat(1.0 / f);
    } else if (integerp(arg)) {
      int i = arg->integer;
      if (i == 0) error2(DIVIDE, PSTR("division by zero"));
      else if (i == 1) return number(1);
      else return makefloat(1.0 / i);
    } else error(DIVIDE, notanumber, arg);
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
        if (i == 0) error2(DIVIDE, PSTR("division by zero"));
        if ((result % i) != 0) return divide_floats(args, result);
        if ((result == INT_MIN) && (i == -1)) return divide_floats(args, result);
        result = result / i;
        args = cdr(args);
      } else error(DIVIDE, notanumber, arg);
    }
    return number(result);
  } else error(DIVIDE, notanumber, arg);
  return nil;
}

/*
  (mod number number)
  Returns its first argument modulo the second argument.
  If both arguments are integers the result is an integer; otherwise it's a floating-point number.
*/
object *fn_mod (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  object *arg2 = second(args);
  if (integerp(arg1) && integerp(arg2)) {
    int divisor = arg2->integer;
    if (divisor == 0) error2(MOD, PSTR("division by zero"));
    int dividend = arg1->integer;
    int remainder = dividend % divisor;
    if ((dividend<0) != (divisor<0)) remainder = remainder + divisor;
    return number(remainder);
  } else {
    float fdivisor = checkintfloat(MOD, arg2);
    if (fdivisor == 0.0) error2(MOD, PSTR("division by zero"));
    float fdividend = checkintfloat(MOD, arg1);
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
object *fn_oneplus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) + 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MAX) return makefloat((arg->integer) + 1.0);
    else return number(result + 1);
  } else error(ONEPLUS, notanumber, arg);
  return nil;
}

/*
  (1- number)
  Subtracts one from its argument and returns it.
  If the argument is an integer the result is an integer if possible;
  otherwise it's a floating-point number.
*/
object *fn_oneminus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) - 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat((arg->integer) - 1.0);
    else return number(result - 1);
  } else error(ONEMINUS, notanumber, arg);
  return nil;
}

/*
  (abs number)
  Returns the absolute, positive value of its argument.
  If the argument is an integer the result will be returned as an integer if possible,
  otherwise a floating-point number.
*/
object *fn_abs (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return makefloat(abs(arg->single_float));
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(abs((float)result));
    else return number(abs(result));
  } else error(ABS, notanumber, arg);
  return nil;
}

/*
  (random number)
  If number is an integer returns a random number between 0 and one less than its argument.
  Otherwise returns a floating-point number between zero and number.
*/
object *fn_random (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (integerp(arg)) return number(random(arg->integer));
  else if (floatp(arg)) return makefloat((float)rand()/(float)(RAND_MAX/(arg->single_float)));
  else error(RANDOM, notanumber, arg);
  return nil;
}

/*
  (max number*)
  Returns the maximum of one or more arguments.
*/
object *fn_maxfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) > (result->integer)) result = arg;
    } else if ((checkintfloat(MAXFN, arg) > checkintfloat(MAXFN, result))) result = arg;
    args = cdr(args);
  }
  return result;
}

/*
  (min number*)
  Returns the minimum of one or more arguments.
*/
object *fn_minfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) < (result->integer)) result = arg;
    } else if ((checkintfloat(MINFN, arg) < checkintfloat(MINFN, result))) result = arg;
    args = cdr(args);
  }
  return result;
}

// Arithmetic comparisons

/*
  (/= number*)
  Returns t if none of the arguments are equal, or nil if two or more arguments are equal.
*/
object *fn_noteq (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *nargs = args;
    object *arg1 = first(nargs);
    nargs = cdr(nargs);
    while (nargs != NULL) {
      object *arg2 = first(nargs);
      if (integerp(arg1) && integerp(arg2)) {
        if ((arg1->integer) == (arg2->integer)) return nil;
      } else if ((checkintfloat(NOTEQ, arg1) == checkintfloat(NOTEQ, arg2))) return nil;
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
object *fn_numeq (object *args, object *env) {
  (void) env;
  return compare(NUMEQ, args, false, false, true);
}

/*
  (< number*)
  Returns t if each argument is less than the next argument, and nil otherwise.
*/
object *fn_less (object *args, object *env) {
  (void) env;
  return compare(LESS, args, true, false, false);
}

/*
  (<= number*)
  Returns t if each argument is less than or equal to the next argument, and nil otherwise.
*/
object *fn_lesseq (object *args, object *env) {
  (void) env;
  return compare(LESSEQ, args, true, false, true);
}

/*
  (> number*)
  Returns t if each argument is greater than the next argument, and nil otherwise.
*/
object *fn_greater (object *args, object *env) {
  (void) env;
  return compare(GREATER, args, false, true, false);
}

/*
  (>= number*)
  Returns t if each argument is greater than or equal to the next argument, and nil otherwise.
*/
object *fn_greatereq (object *args, object *env) {
  (void) env;
  return compare(GREATEREQ, args, false, true, true);
}

/*
  (plusp number)
  Returns t if the argument is greater than zero, or nil otherwise.
*/
object *fn_plusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) > 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) > 0) ? tee : nil;
  else error(PLUSP, notanumber, arg);
  return nil;
}

/*
  (minusp number)
  Returns t if the argument is less than zero, or nil otherwise.
*/
object *fn_minusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) < 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) < 0) ? tee : nil;
  else error(MINUSP, notanumber, arg);
  return nil;
}

/*
  (zerop number)
  Returns t if the argument is zero.
*/
object *fn_zerop (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) == 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) == 0) ? tee : nil;
  else error(ZEROP, notanumber, arg);
  return nil;
}

/*
  (oddp number)
  Returns t if the integer argument is odd.
*/
object *fn_oddp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(ODDP, first(args));
  return ((arg & 1) == 1) ? tee : nil;
}

/*
  (evenp number)
  Returns t if the integer argument is even.
*/
object *fn_evenp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(EVENP, first(args));
  return ((arg & 1) == 0) ? tee : nil;
}

// Number functions

/*
  (integerp number)
  Returns t if the argument is an integer.
*/
object *fn_integerp (object *args, object *env) {
  (void) env;
  return integerp(first(args)) ? tee : nil;
}

/*
  (numberp number)
  Returns t if the argument is a number.
*/
object *fn_numberp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (integerp(arg) || floatp(arg)) ? tee : nil;
}

// Floating-point functions

/*
  (float number)
  Returns its argument converted to a floating-point number.
*/
object *fn_floatfn (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (floatp(arg)) ? arg : makefloat((float)(arg->integer));
}

/*
  (floatp number)
  Returns t if the argument is a floating-point number.
*/
object *fn_floatp (object *args, object *env) {
  (void) env;
  return floatp(first(args)) ? tee : nil;
}

/*
  (sin number)
  Returns sin(number).
*/
object *fn_sin (object *args, object *env) {
  (void) env;
  return makefloat(sin(checkintfloat(SIN, first(args))));
}

/*
  (cos number)
  Returns cos(number).
*/
object *fn_cos (object *args, object *env) {
  (void) env;
  return makefloat(cos(checkintfloat(COS, first(args))));
}

/*
  (tan number)
  Returns tan(number).
*/
object *fn_tan (object *args, object *env) {
  (void) env;
  return makefloat(tan(checkintfloat(TAN, first(args))));
}

/*
  (asin number)
  Returns asin(number).
*/
object *fn_asin (object *args, object *env) {
  (void) env;
  return makefloat(asin(checkintfloat(ASIN, first(args))));
}

/*
  (acos number)
  Returns acos(number).
*/
object *fn_acos (object *args, object *env) {
  (void) env;
  return makefloat(acos(checkintfloat(ACOS, first(args))));
}

/*
  (atan number1 [number2])
  Returns the arc tangent of number1/number2, in radians. If number2 is omitted it defaults to 1.
*/
object *fn_atan (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float div = 1.0;
  args = cdr(args);
  if (args != NULL) div = checkintfloat(ATAN, first(args));
  return makefloat(atan2(checkintfloat(ATAN, arg), div));
}

/*
  (sinh number)
  Returns sinh(number).
*/
object *fn_sinh (object *args, object *env) {
  (void) env;
  return makefloat(sinh(checkintfloat(SINH, first(args))));
}

/*
  (cosh number)
  Returns cosh(number).
*/
object *fn_cosh (object *args, object *env) {
  (void) env;
  return makefloat(cosh(checkintfloat(COSH, first(args))));
}

/*
  (tanh number)
  Returns tanh(number).
*/
object *fn_tanh (object *args, object *env) {
  (void) env;
  return makefloat(tanh(checkintfloat(TANH, first(args))));
}

/*
  (exp number)
  Returns exp(number).
*/
object *fn_exp (object *args, object *env) {
  (void) env;
  return makefloat(exp(checkintfloat(EXP, first(args))));
}

/*
  (sqrt number)
  Returns sqrt(number).
*/
object *fn_sqrt (object *args, object *env) {
  (void) env;
  return makefloat(sqrt(checkintfloat(SQRT, first(args))));
}

/*
  (number [base])
  Returns the logarithm of number to the specified base. If base is omitted it defaults to e.
*/
object *fn_log (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float fresult = log(checkintfloat(LOG, arg));
  args = cdr(args);
  if (args == NULL) return makefloat(fresult);
  else return makefloat(fresult / log(checkintfloat(LOG, first(args))));
}

/*
  (expt number power)
  Returns number raised to the specified power.
  Returns the result as an integer if the arguments are integers and the result will be within range,
  otherwise a floating-point number.
*/
object *fn_expt (object *args, object *env) {
  (void) env;
  object *arg1 = first(args); object *arg2 = second(args);
  float float1 = checkintfloat(EXPT, arg1);
  float value = log(abs(float1)) * checkintfloat(EXPT, arg2);
  if (integerp(arg1) && integerp(arg2) && ((arg2->integer) >= 0) && (abs(value) < 21.4875))
    return number(intpower(arg1->integer, arg2->integer));
  if (float1 < 0) {
    if (integerp(arg2)) return makefloat((arg2->integer & 1) ? -exp(value) : exp(value));
    else error2(EXPT, PSTR("invalid result"));
  }
  return makefloat(exp(value));
}

/*
  (ceiling number [divisor])
  Returns ceil(number/divisor). If omitted, divisor is 1.
*/
object *fn_ceiling (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(ceil(checkintfloat(CEILING, arg) / checkintfloat(CEILING, first(args))));
  else return number(ceil(checkintfloat(CEILING, arg)));
}

/*
  (floor number [divisor])
  Returns floor(number/divisor). If omitted, divisor is 1.
*/
object *fn_floor (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(floor(checkintfloat(FLOOR, arg) / checkintfloat(FLOOR, first(args))));
  else return number(floor(checkintfloat(FLOOR, arg)));
}

/*
  (truncate number)
  Returns t if the argument is a floating-point number.
*/
object *fn_truncate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number((int)(checkintfloat(TRUNCATE, arg) / checkintfloat(TRUNCATE, first(args))));
  else return number((int)(checkintfloat(TRUNCATE, arg)));
}

/*
  (round number)
  Returns t if the argument is a floating-point number.
*/
object *fn_round (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(myround(checkintfloat(ROUND, arg) / checkintfloat(ROUND, first(args))));
  else return number(myround(checkintfloat(ROUND, arg)));
}

// Characters

/*
  (char string n)
  Returns the nth character in a string, counting from zero.
*/
object *fn_char (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!stringp(arg)) error(CHAR, notastring, arg);
  object *n = second(args);
  char c = nthchar(arg, checkinteger(CHAR, n));
  if (c == 0) error(CHAR, indexrange, n);
  return character(c);
}

/*
  (char-code character)
  Returns the ASCII code for a character, as an integer.
*/
object *fn_charcode (object *args, object *env) {
  (void) env;
  return number(checkchar(CHARCODE, first(args)));
}

/*
  (code-char integer)
  Returns the character for the specified ASCII code.
*/
object *fn_codechar (object *args, object *env) {
  (void) env;
  return character(checkinteger(CODECHAR, first(args)));
}

/*
  (characterp item)
  Returns t if the argument is a character and nil otherwise.
*/
object *fn_characterp (object *args, object *env) {
  (void) env;
  return characterp(first(args)) ? tee : nil;
}

// Strings

/*
  (stringp item)
  Returns t if the argument is a string and nil otherwise.
*/
object *fn_stringp (object *args, object *env) {
  (void) env;
  return stringp(first(args)) ? tee : nil;
}

/*
  (string= string string)
  Tests whether two strings are the same.
*/
object *fn_stringeq (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGEQ, args, false, false, true) ? tee : nil;
}

/*
  (string< string string)
  Returns t if the first string is alphabetically less than the second string, and nil otherwise. For example:
*/
object *fn_stringless (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGLESS, args, true, false, false) ? tee : nil;
}

/*
  (string> string string)
  Returns t if the first string is alphabetically greater than the second string, and nil otherwise.
*/
object *fn_stringgreater (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGGREATER, args, false, true, false) ? tee : nil;
}

/*
  (sort list test)
  Destructively sorts list according to the test function, using an insertion sort, and returns the sorted list.
*/
object *fn_sort (object *args, object *env) {
  if (first(args) == NULL) return nil;
  object *list = cons(nil,first(args));
  push(list,GCStack);
  object *predicate = second(args);
  object *compare = cons(NULL, cons(NULL, NULL));
  push(compare,GCStack);
  object *ptr = cdr(list);
  while (cdr(ptr) != NULL) {
    object *go = list;
    while (go != ptr) {
      car(compare) = car(cdr(ptr));
      car(cdr(compare)) = car(cdr(go));
      if (apply(SORT, predicate, compare, env)) break;
      go = cdr(go);
    }
    if (go != ptr) {
      object *obj = cdr(ptr);
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
object *fn_stringfn (object *args, object *env) {
  return fn_princtostring(args, env);
}

/*
  (concatenate 'string string*)
  Joins together the strings given in the second and subsequent arguments, and returns a single string.
*/
object *fn_concatenate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (builtin(arg->name) != STRINGFN) error2(CONCATENATE, PSTR("only supports strings"));
  args = cdr(args);
  object *result = newstring();
  object *tail = result;
  while (args != NULL) {
    object *obj = checkstring(CONCATENATE, first(args));
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
  (subseq string start [end])
  Returns a substring from a string, from character start to character end-1:
*/
object *fn_subseq (object *args, object *env) {
  (void) env;
  object *arg = checkstring(SUBSEQ, first(args));
  int start = checkinteger(SUBSEQ, second(args));
  if (start < 0) error(SUBSEQ, indexnegative, second(args));
  int end;
  args = cddr(args);
  if (args != NULL) end = checkinteger(SUBSEQ, car(args)); else end = stringlength(arg);
  object *result = newstring();
  object *tail = result;
  for (int i=start; i<end; i++) {
    char ch = nthchar(arg, i);
    if (ch == 0) error2(SUBSEQ, indexrange);
    buildstring(ch, &tail);
  }
  return result;
}

/*
  (read-from-string string)
  Reads an atom or list from the specified string and returns it.
*/
object *fn_readfromstring (object *args, object *env) {
  (void) env;
  object *arg = checkstring(READFROMSTRING, first(args));
  GlobalString = arg;
  GlobalStringIndex = 0;
  return read(gstr);
}

/*
  (princ-to-string item)
  Prints its argument to a string, and returns the string.
  Characters and strings are printed without quotation marks or escape characters.
*/
object *fn_princtostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring(PRINCTOSTRING);
  prin1object(arg, pstr);
  return obj;
}

/*
  (prin1-to-string item [stream])
  Prints its argument to a string, and returns the string.
  Characters and strings are printed with quotation marks and escape characters,
  in a format that will be suitable for read-from-string.
*/
object *fn_prin1tostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring(PRIN1TOSTRING);
  printobject(arg, pstr);
  return obj;
}

// Bitwise operators

/*
  (logand [value*])
  Returns the bitwise & of the values.
*/
object *fn_logand (object *args, object *env) {
  (void) env;
  int result = -1;
  while (args != NULL) {
    result = result & checkinteger(LOGAND, first(args));
    args = cdr(args);
  }
  return number(result);
}

/*
  (logior [value*])
  Returns the bitwise | of the values.
*/
object *fn_logior (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result | checkinteger(LOGIOR, first(args));
    args = cdr(args);
  }
  return number(result);
}

/*
  (logxor [value*])
  Returns the bitwise ^ of the values.
*/
object *fn_logxor (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result ^ checkinteger(LOGXOR, first(args));
    args = cdr(args);
  }
  return number(result);
}

/*
  (prin1-to-string item [stream])
  Prints its argument to a string, and returns the string.
  Characters and strings are printed with quotation marks and escape characters,
  in a format that will be suitable for read-from-string.
*/
object *fn_lognot (object *args, object *env) {
  (void) env;
  int result = checkinteger(LOGNOT, car(args));
  return number(~result);
}

/*
  (ash value shift)
  Returns the result of bitwise shifting value by shift bits. If shift is positive, value is shifted to the left.
*/
object *fn_ash (object *args, object *env) {
  (void) env;
  int value = checkinteger(ASH, first(args));
  int count = checkinteger(ASH, second(args));
  if (count >= 0) return number(value << count);
  else return number(value >> abs(count));
}

/*
  (logbitp bit value)
  Returns t if bit number bit in value is a '1', and nil if it is a '0'.
*/
object *fn_logbitp (object *args, object *env) {
  (void) env;
  int index = checkinteger(LOGBITP, first(args));
  int value = checkinteger(LOGBITP, second(args));
  return (bitRead(value, index) == 1) ? tee : nil;
}

// System functions

/*
  (eval form*)
  Evaluates its argument an extra time.
*/
object *fn_eval (object *args, object *env) {
  return eval(first(args), env);
}

/*
  (globals)
  Returns an association list of global variables and their values.
*/
object *fn_globals (object *args, object *env) {
  (void) args;
  if (GlobalEnv == NULL) return nil;
  return fn_mapcar(cons(bsymbol(CAR),cons(GlobalEnv,nil)), env);
}

/*
  (locals)
  Returns an association list of local variables and their values.
*/
object *fn_locals (object *args, object *env) {
  (void) args;
  return env;
}

/*
  (makunbound symbol)
  Removes the value of the symbol from GlobalEnv and returns the symbol.
*/
object *fn_makunbound (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(MAKUNBOUND, notasymbol, var);
  delassoc(var, &GlobalEnv);
  return var;
}

/*
  (break)
  Inserts a breakpoint in the program. When evaluated prints Break! and reenters the REPL.
*/
object *fn_break (object *args, object *env) {
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
object *fn_read (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return read(gfun);
}

/*
  (prin1 item [stream]) 
  Prints its argument, and returns its value.
  Strings are printed with quotation marks and escape characters.
*/
object *fn_prin1 (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  printobject(obj, pfun);
  return obj;
}

/*
  (print item [stream])
  Prints its argument with quotation marks and escape characters, on a new line, and followed by a space.
  If stream is specified the argument is printed to the specified stream.
*/
object *fn_print (object *args, object *env) {
  (void) env;
  object *obj = first(args);
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
object *fn_princ (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  prin1object(obj, pfun);
  return obj;
}

/*
  (terpri [stream])
  Prints a new line, and returns nil.
  If stream is specified the new line is written to the specified stream. 
*/
object *fn_terpri (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  pln(pfun);
  return nil;
}

/*
  (read-byte stream)
  Reads a byte from a stream and returns it.
*/
object *fn_readbyte (object *args, object *env) {
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
object *fn_readline (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return readstring('\n', gfun);
}

/*
  (write-byte number [stream])
  Writes a byte to a stream.
*/
object *fn_writebyte (object *args, object *env) {
  (void) env;
  int value = checkinteger(WRITEBYTE, first(args));
  pfun_t pfun = pstreamfun(cdr(args));
  (pfun)(value);
  return nil;
}

/*
  (write-string string [stream])
  Writes a string. If stream is specified the string is written to the stream.
*/
object *fn_writestring (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  uint16_t temp = Flags;
  clrflag(PRINTREADABLY);
  printstring(obj, pfun);
  Flags = temp;
  return nil;
}

/*
  (write-line string [stream])
  Writes a string terminated by a newline character. If stream is specified the string is written to the stream.
*/
object *fn_writeline (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  uint16_t temp = Flags;
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
object *fn_restarti2c (object *args, object *env) {
  (void) env;
  int stream = first(args)->integer;
  args = cdr(args);
  int read = 0; // Write
  I2Ccount = 0;
  if (args != NULL) {
    object *rw = first(args);
    if (integerp(rw)) I2Ccount = rw->integer;
    read = (rw != NULL);
  }
  int address = stream & 0xFF;
  if (stream>>8 != I2CSTREAM) error2(RESTARTI2C, PSTR("not an i2c stream"));
  return I2Crestart(address, read) ? tee : nil;
}

/*
  (gc)
  Forces a garbage collection and prints the number of objects collected, and the time taken.
*/
object *fn_gc (object *obj, object *env) {
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
object *fn_room (object *args, object *env) {
  (void) args, (void) env;
  return number(Freespace);
}

/*
  (cls)
  Prints a clear-screen character.
*/
object *fn_cls (object *args, object *env) {
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
object *fn_pinmode (object *args, object *env) {
  (void) env; int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(NIL, arg);
  else pin = checkinteger(PINMODE, first(args));
  int pm = INPUT;
  arg = second(args);
  if (keywordp(arg)) pm = checkkeyword(PINMODE, arg);
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
object *fn_digitalread (object *args, object *env) {
  (void) env;
  int pin = checkinteger(DIGITALREAD, first(args));
  if (digitalRead(pin) != 0) return tee; else return nil;
}

/*
  (digitalwrite pin state)
  Sets the state of the specified Arduino pin number.
*/
object *fn_digitalwrite (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(NIL, arg);
  else pin = checkinteger(DIGITALWRITE, arg);
  arg = second(args);
  int mode;
  if (keywordp(arg)) mode = checkkeyword(DIGITALWRITE, arg);
  else if (integerp(arg)) mode = arg->integer ? HIGH : LOW;
  else mode = (arg != nil) ? HIGH : LOW;
  digitalWrite(pin, mode);
  return arg;
}

/*
  (analogread pin)
  Reads the specified Arduino analogue pin number and returns the value.
*/
object *fn_analogread (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(ANALOGREAD, arg);
  else {
    pin = checkinteger(ANALOGREAD, arg);
    checkanalogread(pin);
  }
  return number(analogRead(pin));
}

/*
  (analogreadresolution bits)
  Specifies the resolution for the analogue inputs on platforms that support it.
  The default resolution on all platforms is 10 bits.
*/
object *fn_analogreadresolution (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  analogReadResolution(checkinteger(ANALOGREADRESOLUTION, arg));
  return arg;
}

/*
  (analogwrite pin value)
  Writes the value to the specified Arduino pin number.
*/
object *fn_analogwrite (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(NIL, arg);
  else pin = checkinteger(ANALOGWRITE, arg);
  checkanalogwrite(pin);
  object *value = second(args);
  analogWrite(pin, checkinteger(ANALOGWRITE, value));
  return value;
}

/*
  (delay number)
  Delays for a specified number of milliseconds.
*/
object *fn_delay (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  delay(checkinteger(DELAY, arg1));
  return arg1;
}

/*
  (millis)
  Returns the time in milliseconds that uLisp has been running.
*/
object *fn_millis (object *args, object *env) {
  (void) args, (void) env;
  return number(millis());
}

/*
  (sleep secs)
  Puts the processor into a low-power sleep mode for secs.
  Only supported on some platforms. On other platforms it does delay(1000*secs).
*/
object *fn_sleep (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  sleep(checkinteger(SLEEP, arg1));
  return arg1;
}

/*
  (note [pin] [note] [octave])
  Generates a square wave on pin.
  The argument note represents the note in the well-tempered scale, from 0 to 11,  
  where 0 represents C, 1 represents C#, and so on.
  The argument octave can be from 3 to 6. If omitted it defaults to 0.
*/
object *fn_note (object *args, object *env) {
  (void) env;
  static int pin = 255;
  if (args != NULL) {
    pin = checkinteger(NOTE, first(args));
    int note = 0;
    if (cddr(args) != NULL) note = checkinteger(NOTE, second(args));
    int octave = 0;
    if (cddr(args) != NULL) octave = checkinteger(NOTE, third(args));
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
object *fn_register (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  int addr;
  if (keywordp(arg)) addr = checkkeyword(REGISTER, arg);
  else addr = checkinteger(REGISTER, first(args));
  if (cdr(args) == NULL) return number(*(uint32_t *)addr);
  (*(uint32_t *)addr) = checkinteger(REGISTER, second(args));
  return second(args);
}

// Tree Editor

/*
  (edit 'function)
  Calls the Lisp tree editor to allow you to edit a function definition.
*/
object *fn_edit (object *args, object *env) {
  object *fun = first(args);
  object *pair = findvalue(EDIT, fun, env);
  clrflag(EXITEDITOR);
  object *arg = edit(eval(fun, env));
  cdr(pair) = arg;
  return arg;
}

// Pretty printer

/*
  (pprint item [str])
  Prints its argument, using the pretty printer, to display it formatted in a structured way.
  If str is specified it prints to the specified stream. It returns no value.
*/
object *fn_pprint (object *args, object *env) {
  (void) env;
  object *obj = first(args);
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
  Is str is specified it prints to the specified stream. It returns no value.
*/
object *fn_pprintall (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  #if defined(gfxsupport)
  if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
  #endif
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    object *val = cdr(pair);
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
  (format output controlstring arguments*)
  Outputs its arguments formatted according to the format directives in controlstring.
*/
object *fn_format (object *args, object *env) {
  (void) env;
  pfun_t pfun = pserial;
  object *output = first(args);
  object *obj;
  if (output == nil) { obj = startstring(FORMAT); pfun = pstr; }
  else if (output != tee) pfun = pstreamfun(args);
  object *formatstr = checkstring(FORMAT, second(args));
  object *save = NULL;
  args = cddr(args);
  int len = stringlength(formatstr);
  uint8_t n = 0, width = 0, w, bra = 0;
  char pad = ' ';
  bool tilde = false, mute = false, comma, quote;
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
          object *arg = first(args); args = cdr(args);
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
object *fn_require (object *args, object *env) {
  object *arg = first(args);
  object *globals = GlobalEnv;
  if (!symbolp(arg)) error(REQUIRE, notasymbol, arg);
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    if (symbolp(var) && var == arg) return nil;
    globals = cdr(globals);
  }
  GlobalStringIndex = 0;
  object *line = read(glibrary);
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
object *fn_listlibrary (object *args, object *env) {
  (void) args, (void) env;
  GlobalStringIndex = 0;
  object *line = read(glibrary);
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
  (documentation 'symbol [type])
  Returns the documentation string of a built-in or user-defined function. The type argument is ignored.
*/
object *fn_documentation (object *args, object *env) {
  return documentation(DOCUMENTATION, first(args), env);
}

// Wi-fi

/*
  (available stream)
  Returns the number of bytes available for reading from the wifi-stream, or zero if no bytes are available.
*/
object *fn_available (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2(AVAILABLE, PSTR("invalid stream"));
  return number(client.available());
}

/*
  (wifi-server)
  Starts a Wi-Fi server running. It returns nil.
*/
object *fn_wifiserver (object *args, object *env) {
  (void) args, (void) env;
  server.begin();
  return nil;
}

/*
  (wifi-softap ssid [password channel hidden])
  Set up a soft access point to establish a Wi-Fi network.
  Returns the IP address as a string or nil if unsuccessful.
*/
object *fn_wifisoftap (object *args, object *env) {
  (void) env;
  char ssid[33], pass[65];
  if (args == NULL) return WiFi.softAPdisconnect(true) ? tee : nil;
  object *first = first(args); args = cdr(args);
  if (args == NULL) WiFi.softAP(cstring(WIFISOFTAP, first, ssid, 33));
  else {
    object *second = first(args);
    args = cdr(args);
    int channel = 1;
    bool hidden = false;
    if (args != NULL) {
      channel = checkinteger(WIFISOFTAP, first(args));
      args = cdr(args);
      if (args != NULL) hidden = (first(args) != nil);
    }
    WiFi.softAP(cstring(WIFISOFTAP, first, ssid, 33), cstring(WIFISOFTAP, second, pass, 65), channel, hidden);
  }
  return lispstring((char*)WiFi.softAPIP().toString().c_str());
}

/*
  (connected stream)
  Returns t or nil to indicate if the client on stream is connected.
*/
object *fn_connected (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2(CONNECTED, PSTR("invalid stream"));
  return client.connected() ? tee : nil;
}

/*
  (wifi-localip)
  Returns the IP address of the local network as a string.
*/
object *fn_wifilocalip (object *args, object *env) {
  (void) args, (void) env;
  return lispstring((char*)WiFi.localIP().toString().c_str());
}

/*
  (wifi-connect [ssid pass])
  Connects to the Wi-Fi network ssid using password pass. It returns the IP address as a string.
*/
object *fn_wificonnect (object *args, object *env) {
  (void) env;
  char ssid[33], pass[65];
  if (args == NULL) { WiFi.disconnect(true); return nil; }
  if (cdr(args) == NULL) WiFi.begin(cstring(WIFICONNECT, first(args), ssid, 33));
  else WiFi.begin(cstring(WIFICONNECT, first(args), ssid, 33), cstring(WIFICONNECT, second(args), pass, 65));
  int result = WiFi.waitForConnectResult();
  if (result == WL_CONNECTED) return lispstring((char*)WiFi.localIP().toString().c_str());
  else if (result == WL_NO_SSID_AVAIL) error2(WIFICONNECT, PSTR("network not found"));
  else if (result == WL_CONNECT_FAILED) error2(WIFICONNECT, PSTR("connection failed"));
  else error2(WIFICONNECT, PSTR("unable to connect"));
  return nil;
}

// Graphics functions

/*
  (draw-pixel x y [colour])
  Draws a pixel at coordinates (x,y) in colour, or white if omitted.
*/
object *fn_drawpixel (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t colour = COLOR_WHITE;
  if (cddr(args) != NULL) colour = checkinteger(DRAWPIXEL, third(args));
  tft.drawPixel(checkinteger(DRAWPIXEL, first(args)), checkinteger(DRAWPIXEL, second(args)), colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (draw-line x0 y0 x1 y1 [colour])
  Draws a line from (x0,y0) to (x1,y1) in colour, or white if omitted.
*/
object *fn_drawline (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(DRAWLINE, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(DRAWLINE, car(args));
  tft.drawLine(params[0], params[1], params[2], params[3], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (draw-rect x y w h [colour])
  Draws an outline rectangle with its top left corner at (x,y), with width w,
  and with height h. The outline is drawn in colour, or white if omitted.
*/
object *fn_drawrect (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(DRAWRECT, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(DRAWRECT, car(args));
  tft.drawRect(params[0], params[1], params[2], params[3], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (fill-rect x y w h [colour])
  Draws a filled rectangle with its top left corner at (x,y), with width w,
  and with height h. The outline is drawn in colour, or white if omitted.
*/
object *fn_fillrect (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(FILLRECT, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(FILLRECT, car(args));
  tft.fillRect(params[0], params[1], params[2], params[3], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (draw-circle x y r [colour])
  Draws an outline circle with its centre at (x, y) and with radius r.
  The circle is drawn in colour, or white if omitted.
*/
object *fn_drawcircle (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(DRAWCIRCLE, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(DRAWCIRCLE, car(args));
  tft.drawCircle(params[0], params[1], params[2], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (fill-circle x y r [colour])
  Draws a filled circle with its centre at (x, y) and with radius r.
  The circle is drawn in colour, or white if omitted.
*/
object *fn_fillcircle (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(FILLCIRCLE, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(FILLCIRCLE, car(args));
  tft.fillCircle(params[0], params[1], params[2], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (draw-round-rect x y w h radius [colour])
  Draws an outline rounded rectangle with its top left corner at (x,y), with width w,
  height h, and corner radius radius. The outline is drawn in colour, or white if omitted.
*/
object *fn_drawroundrect (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(DRAWROUNDRECT, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(DRAWROUNDRECT, car(args));
  tft.drawRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (fill-round-rect x y w h radius [colour])
  Draws a filled rounded rectangle with its top left corner at (x,y), with width w,
  height h, and corner radius radius. The outline is drawn in colour, or white if omitted.
*/
object *fn_fillroundrect (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(FILLROUNDRECT, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(FILLROUNDRECT, car(args));
  tft.fillRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (draw-triangle x0 y0 x1 y1 x2 y2 [colour])
  Draws an outline triangle between (x1,y1), (x2,y2), and (x3,y3).
  The outline is drawn in colour, or white if omitted.
*/
object *fn_drawtriangle (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(DRAWTRIANGLE, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(DRAWTRIANGLE, car(args));
  tft.drawTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (fill-triangle x0 y0 x1 y1 x2 y2 [colour])
  Draws a filled triangle between (x1,y1), (x2,y2), and (x3,y3).
  The outline is drawn in colour, or white if omitted.
*/
object *fn_filltriangle (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(FILLTRIANGLE, car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(FILLTRIANGLE, car(args));
  tft.fillTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  tft.display();
  #else
  (void) args, (void) env;
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
object *fn_drawchar (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t colour = COLOR_WHITE, bg = COLOR_BLACK, size = 1;
  object *more = cdr(cddr(args));
  if (more != NULL) {
    colour = checkinteger(DRAWCHAR, car(more));
    more = cdr(more);
    if (more != NULL) {
      bg = checkinteger(DRAWCHAR, car(more));
      more = cdr(more);
      if (more != NULL) size = checkinteger(DRAWCHAR, car(more));
    }
  }
  tft.drawChar(checkinteger(DRAWCHAR, first(args)), checkinteger(DRAWCHAR, second(args)), checkchar(DRAWCHAR, third(args)),
    colour, bg, size);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (set-cursor x y)
  Sets the start point for text plotting to (x, y).
*/
object *fn_setcursor (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setCursor(checkinteger(SETCURSOR, first(args)), checkinteger(SETCURSOR, second(args)));
  #endif
  return nil;
}

/*
  (set-text-color colour [background])
  Sets the text colour for text plotted using (with-gfx ...).
*/
object *fn_settextcolor (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  if (cdr(args) != NULL) tft.setTextColor(checkinteger(SETTEXTCOLOR, first(args)), checkinteger(SETTEXTCOLOR, second(args)));
  else tft.setTextColor(checkinteger(SETTEXTCOLOR, first(args)));
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (set-text-size scale)
  Scales text by the specified size, default 1.
*/
object *fn_settextsize (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setTextSize(checkinteger(SETTEXTSIZE, first(args)));
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (set-text-wrap boolean)
  Specified whether text wraps at the right-hand edge of the display; the default is t.
*/
object *fn_settextwrap (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setTextWrap(first(args) != NULL);
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (fill-screen [colour])
  Fills or clears the screen with colour, default black.
*/
object *fn_fillscreen (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  uint16_t colour = COLOR_BLACK;
  if (args != NULL) colour = checkinteger(FILLSCREEN, first(args));
  tft.fillScreen(colour);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (set-rotation option)
  Sets the display orientation for subsequent graphics commands; values are 0, 1, 2, or 3.
*/
object *fn_setrotation (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setRotation(checkinteger(SETROTATION, first(args)));
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

/*
  (invert-display boolean)
  Mirror-images the display. 
*/
object *fn_invertdisplay (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.invertDisplay(first(args) != NULL);
  tft.display();
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

// Insert your own function definitions here


object *fn_batteryvoltage(object *args, object *env) {
    (void)args, (void)env;
    return makefloat((float)battery.getVoltage());
}

object *fn_batterypercentage(object *args, object *env) {
    (void)args, (void)env;
    return makefloat((float)battery.getSOC());
}

object *fn_batterychangerate(object *args, object *env) {
    (void)args, (void)env;
    return makefloat((float)battery.getChangeRate());
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
const char string13[] PROGMEM = ""; // SPECIALFORMS
const char string14[] PROGMEM = "quote";
const char string15[] PROGMEM = "or";
const char string16[] PROGMEM = "defun";
const char string17[] PROGMEM = "defvar";
const char string18[] PROGMEM = "setq";
const char string19[] PROGMEM = "loop";
const char string20[] PROGMEM = "return";
const char string21[] PROGMEM = "push";
const char string22[] PROGMEM = "pop";
const char string23[] PROGMEM = "incf";
const char string24[] PROGMEM = "decf";
const char string25[] PROGMEM = "setf";
const char string26[] PROGMEM = "dolist";
const char string27[] PROGMEM = "dotimes";
const char string28[] PROGMEM = "trace";
const char string29[] PROGMEM = "untrace";
const char string30[] PROGMEM = "for-millis";
const char string31[] PROGMEM = "time";
const char string32[] PROGMEM = "with-output-to-string";
const char string33[] PROGMEM = "with-serial";
const char string34[] PROGMEM = "with-i2c";
const char string35[] PROGMEM = "with-spi";
const char string36[] PROGMEM = "with-sd-card";
const char string37[] PROGMEM = "with-gfx";
const char string38[] PROGMEM = "with-client";
const char string_unwindprotect[] PROGMEM = "unwind-protect";
const char string_ignoreerrors[] PROGMEM = "ignore-errors";
//const char string_error[] PROGMEM = "error";
const char string_MACRO_SENTINEL[] PROGMEM =  "MACRO";
const char string_defmacro[] PROGMEM = "defmacro";
const char string_quasiquote[] PROGMEM = "quasiquote";
const char string_unquote[] PROGMEM = "unquote";
const char string_unquotesplicing[] PROGMEM = "unquote-splicing";
const char string_expand[] PROGMEM = "expand";
const char string39[] PROGMEM = ""; // TAILFORMS
const char string40[] PROGMEM = "progn";
const char string41[] PROGMEM = "if";
const char string42[] PROGMEM = "cond";
const char string43[] PROGMEM = "when";
const char string44[] PROGMEM = "unless";
const char string45[] PROGMEM = "case";
const char string46[] PROGMEM = "and";
const char string47[] PROGMEM = "?";
const char string48[] PROGMEM = ""; // FUNCTIONS
const char string_geterror[] PROGMEM = "get-error";
const char string_throw[] PROGMEM = "throw";
const char string_intern[] PROGMEM = "intern";
const char string49[] PROGMEM = "not";
const char string50[] PROGMEM = "null";
const char string51[] PROGMEM = "cons";
const char string52[] PROGMEM = "atom";
const char string53[] PROGMEM = "listp";
const char string54[] PROGMEM = "consp";
const char string55[] PROGMEM = "symbolp";
const char string56[] PROGMEM = "arrayp";
const char string57[] PROGMEM = "boundp";
const char string58[] PROGMEM = "set";
const char string59[] PROGMEM = "streamp";
const char string60[] PROGMEM = "eq";
const char string61[] PROGMEM = "car";
const char string62[] PROGMEM = "first";
const char string63[] PROGMEM = "cdr";
const char string64[] PROGMEM = "rest";
const char string65[] PROGMEM = "caar";
const char string66[] PROGMEM = "cadr";
const char string67[] PROGMEM = "second";
const char string68[] PROGMEM = "cdar";
const char string69[] PROGMEM = "cddr";
const char string70[] PROGMEM = "caaar";
const char string71[] PROGMEM = "caadr";
const char string72[] PROGMEM = "cadar";
const char string73[] PROGMEM = "caddr";
const char string74[] PROGMEM = "third";
const char string75[] PROGMEM = "cdaar";
const char string76[] PROGMEM = "cdadr";
const char string77[] PROGMEM = "cddar";
const char string78[] PROGMEM = "cdddr";
const char string79[] PROGMEM = "length";
const char string80[] PROGMEM = "array-dimensions";
const char string81[] PROGMEM = "list";
const char string82[] PROGMEM = "make-array";
const char string83[] PROGMEM = "reverse";
const char string84[] PROGMEM = "nth";
const char string85[] PROGMEM = "aref";
const char string86[] PROGMEM = "assoc";
const char string87[] PROGMEM = "member";
const char string88[] PROGMEM = "apply";
const char string89[] PROGMEM = "funcall";
const char string90[] PROGMEM = "append";
const char string91[] PROGMEM = "mapc";
const char string92[] PROGMEM = "mapcar";
const char string93[] PROGMEM = "mapcan";
const char string94[] PROGMEM = "+";
const char string95[] PROGMEM = "-";
const char string96[] PROGMEM = "*";
const char string97[] PROGMEM = "/";
const char string98[] PROGMEM = "mod";
const char string99[] PROGMEM = "1+";
const char string100[] PROGMEM = "1-";
const char string101[] PROGMEM = "abs";
const char string102[] PROGMEM = "random";
const char string103[] PROGMEM = "max";
const char string104[] PROGMEM = "min";
const char string105[] PROGMEM = "/=";
const char string106[] PROGMEM = "=";
const char string107[] PROGMEM = "<";
const char string108[] PROGMEM = "<=";
const char string109[] PROGMEM = ">";
const char string110[] PROGMEM = ">=";
const char string111[] PROGMEM = "plusp";
const char string112[] PROGMEM = "minusp";
const char string113[] PROGMEM = "zerop";
const char string114[] PROGMEM = "oddp";
const char string115[] PROGMEM = "evenp";
const char string116[] PROGMEM = "integerp";
const char string117[] PROGMEM = "numberp";
const char string118[] PROGMEM = "float";
const char string119[] PROGMEM = "floatp";
const char string120[] PROGMEM = "sin";
const char string121[] PROGMEM = "cos";
const char string122[] PROGMEM = "tan";
const char string123[] PROGMEM = "asin";
const char string124[] PROGMEM = "acos";
const char string125[] PROGMEM = "atan";
const char string126[] PROGMEM = "sinh";
const char string127[] PROGMEM = "cosh";
const char string128[] PROGMEM = "tanh";
const char string129[] PROGMEM = "exp";
const char string130[] PROGMEM = "sqrt";
const char string131[] PROGMEM = "log";
const char string132[] PROGMEM = "expt";
const char string133[] PROGMEM = "ceiling";
const char string134[] PROGMEM = "floor";
const char string135[] PROGMEM = "truncate";
const char string136[] PROGMEM = "round";
const char string137[] PROGMEM = "char";
const char string138[] PROGMEM = "char-code";
const char string139[] PROGMEM = "code-char";
const char string140[] PROGMEM = "characterp";
const char string141[] PROGMEM = "stringp";
const char string142[] PROGMEM = "string=";
const char string143[] PROGMEM = "string<";
const char string144[] PROGMEM = "string>";
const char string145[] PROGMEM = "sort";
const char string146[] PROGMEM = "string";
const char string147[] PROGMEM = "concatenate";
const char string148[] PROGMEM = "subseq";
const char string149[] PROGMEM = "read-from-string";
const char string150[] PROGMEM = "princ-to-string";
const char string151[] PROGMEM = "prin1-to-string";
const char string152[] PROGMEM = "logand";
const char string153[] PROGMEM = "logior";
const char string154[] PROGMEM = "logxor";
const char string155[] PROGMEM = "lognot";
const char string156[] PROGMEM = "ash";
const char string157[] PROGMEM = "logbitp";
const char string158[] PROGMEM = "eval";
const char string159[] PROGMEM = "globals";
const char string160[] PROGMEM = "locals";
const char string161[] PROGMEM = "makunbound";
const char string162[] PROGMEM = "break";
const char string163[] PROGMEM = "read";
const char string164[] PROGMEM = "prin1";
const char string165[] PROGMEM = "print";
const char string166[] PROGMEM = "princ";
const char string167[] PROGMEM = "terpri";
const char string168[] PROGMEM = "read-byte";
const char string169[] PROGMEM = "read-line";
const char string170[] PROGMEM = "write-byte";
const char string171[] PROGMEM = "write-string";
const char string172[] PROGMEM = "write-line";
const char string173[] PROGMEM = "restart-i2c";
const char string174[] PROGMEM = "gc";
const char string175[] PROGMEM = "room";
// const char string176[] PROGMEM = "save-image";
// const char string177[] PROGMEM = "load-image";
const char string178[] PROGMEM = "cls";
const char string179[] PROGMEM = "pinmode";
const char string180[] PROGMEM = "digitalread";
const char string181[] PROGMEM = "digitalwrite";
const char string182[] PROGMEM = "analogread";
const char string183[] PROGMEM = "analogreadresolution";
const char string184[] PROGMEM = "analogwrite";
const char string185[] PROGMEM = "delay";
const char string186[] PROGMEM = "millis";
const char string187[] PROGMEM = "sleep";
const char string188[] PROGMEM = "note";
const char string189[] PROGMEM = "register";
const char string190[] PROGMEM = "edit";
const char string191[] PROGMEM = "pprint";
const char string192[] PROGMEM = "pprintall";
const char string193[] PROGMEM = "format";
const char string194[] PROGMEM = "require";
const char string195[] PROGMEM = "list-library";
const char string196[] PROGMEM = "documentation";
const char string197[] PROGMEM = "available";
const char string198[] PROGMEM = "wifi-server";
const char string199[] PROGMEM = "wifi-softap";
const char string200[] PROGMEM = "connected";
const char string201[] PROGMEM = "wifi-localip";
const char string202[] PROGMEM = "wifi-connect";
const char string203[] PROGMEM = "draw-pixel";
const char string204[] PROGMEM = "draw-line";
const char string205[] PROGMEM = "draw-rect";
const char string206[] PROGMEM = "fill-rect";
const char string207[] PROGMEM = "draw-circle";
const char string208[] PROGMEM = "fill-circle";
const char string209[] PROGMEM = "draw-round-rect";
const char string210[] PROGMEM = "fill-round-rect";
const char string211[] PROGMEM = "draw-triangle";
const char string212[] PROGMEM = "fill-triangle";
const char string213[] PROGMEM = "draw-char";
const char string214[] PROGMEM = "set-cursor";
const char string215[] PROGMEM = "set-text-color";
const char string216[] PROGMEM = "set-text-size";
const char string217[] PROGMEM = "set-text-wrap";
const char string218[] PROGMEM = "fill-screen";
const char string219[] PROGMEM = "set-rotation";
const char string220[] PROGMEM = "invert-display";
const char string221[] PROGMEM = ""; // KEYWORDS
const char string222[] PROGMEM = ":led-builtin";
const char string223[] PROGMEM = ":high";
const char string224[] PROGMEM = ":low";
const char string225[] PROGMEM = ":input";
const char string226[] PROGMEM = ":input-pullup";
const char string227[] PROGMEM = ":input-pulldown";
const char string228[] PROGMEM = ":output";
const char string229[] PROGMEM = "";

// Insert your own function names here
const char string_batteryvoltage[] PROGMEM = "battery:voltage";
const char string_batterypercentage[] PROGMEM = "battery:percentage";
const char string_batterychangerate[] PROGMEM = "battery:change-rate";

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
const char doc15[] PROGMEM = "(or item*)\n"
"Evaluates its arguments until one returns non-nil, and returns its value.";
const char doc16[] PROGMEM = "(defun name (parameters) form*)\n"
"Defines a function.";
const char doc17[] PROGMEM = "(defvar variable form)\n"
"Defines a global variable.";
const char doc18[] PROGMEM = "(setq symbol value [symbol value]*)\n"
"For each pair of arguments assigns the value of the second argument\n"
"to the variable specified in the first argument.";
const char doc19[] PROGMEM = "(loop forms*)\n"
"Executes its arguments repeatedly until one of the arguments calls (return),\n"
"which then causes an exit from the loop.";
const char doc20[] PROGMEM = "(return [value])\n"
"Exits from a (dotimes ...), (dolist ...), or (loop ...) loop construct and returns value.";
const char doc21[] PROGMEM = "(push item place)\n"
"Modifies the value of place, which should be a list, to add item onto the front of the list,\n"
"and returns the new list.";
const char doc22[] PROGMEM = "(pop place)\n"
"Modifies the value of place, which should be a list, to remove its first item, and returns that item.";
const char doc23[] PROGMEM = "(incf place [number])\n"
"Increments a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional increment which defaults to 1.";
const char doc24[] PROGMEM = "(decf place [number])\n"
"Decrements a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional decrement which defaults to 1.";
const char doc25[] PROGMEM = "(setf place value [place value]*)\n"
"For each pair of arguments modifies a place to the result of evaluating value.";
const char doc26[] PROGMEM = "(dolist (var list [result]) form*)\n"
"Sets the local variable var to each element of list in turn, and executes the forms.\n"
"It then returns result, or nil if result is omitted.";
const char doc27[] PROGMEM = "(dotimes (var number [result]) form*)\n"
"Executes the forms number times, with the local variable var set to each integer from 0 to number-1 in turn.\n"
"It then returns result, or nil if result is omitted.";
const char doc28[] PROGMEM = "(trace [function]*)\n"
"Turns on tracing of up to TRACEMAX user-defined functions,\n"
"and returns a list of the functions currently being traced.";
const char doc29[] PROGMEM = "(untrace [function]*)\n"
"Turns off tracing of up to TRACEMAX user-defined functions, and returns a list of the functions untraced.\n"
"If no functions are specified it untraces all functions.";
const char doc30[] PROGMEM = "(for-millis ([number]) form*)\n"
"Executes the forms and then waits until a total of number milliseconds have elapsed.\n"
"Returns the total number of milliseconds taken.";
const char doc31[] PROGMEM = "(time form)\n"
"Prints the value returned by the form, and the time taken to evaluate the form\n"
"in milliseconds or seconds.";
const char doc32[] PROGMEM = "(with-output-to-string (str) form*)\n"
"Returns a string containing the output to the stream variable str.";
const char doc33[] PROGMEM = "(with-serial (str port [baud]) form*)\n"
"Evaluates the forms with str bound to a serial-stream using port.\n"
"The optional baud gives the baud rate divided by 100, default 96.";
const char doc34[] PROGMEM = "(with-i2c (str [port] address [read-p]) form*)\n"
"Evaluates the forms with str bound to an i2c-stream defined by address.\n"
"If read-p is nil or omitted the stream is written to, otherwise it specifies the number of bytes\n"
"to be read from the stream. The port if specified is ignored.";
const char doc35[] PROGMEM = "(with-spi (str pin [clock] [bitorder] [mode]) form*)\n"
"Evaluates the forms with str bound to an spi-stream.\n"
"The parameters specify the enable pin, clock in kHz (default 4000),\n"
"bitorder 0 for LSBFIRST and 1 for MSBFIRST (default 1), and SPI mode (default 0).";
const char doc36[] PROGMEM = "(with-sd-card (str filename [mode]) form*)\n"
"Evaluates the forms with str bound to an sd-stream reading from or writing to the file filename.\n"
"If mode is omitted the file is read, otherwise 0 means read, 1 write-append, or 2 write-overwrite.";
const char doc37[] PROGMEM = "(with-gfx (str) form*)\n"
"Evaluates the forms with str bound to an gfx-stream so you can print text\n"
"to the graphics display using the standard uLisp print commands.";
const char doc38[] PROGMEM = "(with-client (str [address port]) form*)\n"
"Evaluates the forms with str bound to a wifi-stream.";

const char doc_unwindprotect[] PROGMEM = "(unwind-protect testform finallyforms*)\n"
"runs testform, if it errors, stores the error in GlobalErrorString and runs the finallyforms.";
const char doc_ignoreerrors[] PROGMEM = "(ignore-errors forms*)\n"
"catches errors and stores them in GlobalErrorString.";
// const char doc_error[] PROGMEM = "(error fmtstr args*)\n"
// "formats the string with the args, and then throws the error.";
const char doc_defmacro[] PROGMEM = "(defmacro (params*) body*)\n"
"defines a macro";
const char doc_quasiquote[] PROGMEM = "(quasiquote *args)\n"
"Lisp quasiquotes";
const char doc_unquote[] PROGMEM = "(unqouote arg)\n"
"Lisp unquotes. Cannot be used outside of (quasiquote).";
const char doc_unquotesplicing[] PROGMEM = "(unquote-splicing *args)\n"
"Lisp unquotes. Cannot be used outside of (quasiquote).";
const char doc_expand[] PROGMEM = "(expand macro params body)\n"
"Expand a macro.";

const char doc40[] PROGMEM = "(progn form*)\n"
"Evaluates several forms grouped together into a block, and returns the result of evaluating the last form.";
const char doc41[] PROGMEM = "(if test then [else])\n"
"Evaluates test. If it's non-nil the form then is evaluated and returned;\n"
"otherwise the form else is evaluated and returned.";
const char doc42[] PROGMEM = "(cond ((test form*) (test form*) ... ))\n"
"Each argument is a list consisting of a test optionally followed by one or more forms.\n"
"If the test evaluates to non-nil the forms are evaluated, and the last value is returned as the result of the cond.\n"
"If the test evaluates to nil, none of the forms are evaluated, and the next argument is processed in the same way.";
const char doc43[] PROGMEM = "(when test form*)\n"
"Evaluates the test. If it's non-nil the forms are evaluated and the last value is returned.";
const char doc44[] PROGMEM = "(unless test form*)\n"
"Evaluates the test. If it's nil the forms are evaluated and the last value is returned.";
const char doc45[] PROGMEM = "(case keyform ((key form*) (key form*) ... ))\n"
"Evaluates a keyform to produce a test key, and then tests this against a series of arguments,\n"
"each of which is a list containing a key optionally followed by one or more forms.";
const char doc46[] PROGMEM = "(and item*)\n"
"Evaluates its arguments until one returns nil, and returns the last value.";
const char doc47[] PROGMEM = "(? item)\n"
"Prints the documentation string of a built-in or user-defined function.";

const char doc_geterror[] PROGMEM = "(geterror)\n"
"Returns the GlobalErrorString that holds the last error encountered.";
const char doc_throw[] PROGMEM = "(throw str)\n"
"Throws the string as an error.";
const char doc_intern[] PROGMEM = "(intern str)\n"
"Returns a symbol with the same name as str.";

const char doc49[] PROGMEM = "(not item)\n"
"Returns t if its argument is nil, or nil otherwise. Equivalent to null.";
const char doc51[] PROGMEM = "(cons item item)\n"
"If the second argument is a list, cons returns a new list with item added to the front of the list.\n"
"If the second argument isn't a list cons returns a dotted pair.";
const char doc52[] PROGMEM = "(atom item)\n"
"Returns t if its argument is a single number, symbol, or nil.";
const char doc53[] PROGMEM = "(listp item)\n"
"Returns t if its argument is a list.";
const char doc54[] PROGMEM = "(consp item)\n"
"Returns t if its argument is a non-null list.";
const char doc55[] PROGMEM = "(symbolp item)\n"
"Returns t if its argument is a symbol.";
const char doc56[] PROGMEM = "(arrayp item)\n"
"Returns t if its argument is an array.";
const char doc57[] PROGMEM = "(boundp item)\n"
"Returns t if its argument is a symbol with a value.";
const char doc58[] PROGMEM = "(set symbol value [symbol value]*)\n"
"For each pair of arguments, assigns the value of the second argument to the value of the first argument.";
const char doc59[] PROGMEM = "(streamp item)\n"
"Returns t if its argument is a stream.";
const char doc60[] PROGMEM = "(eq item item)\n"
"Tests whether the two arguments are the same symbol, same character, equal numbers,\n"
"or point to the same cons, and returns t or nil as appropriate.";
const char doc61[] PROGMEM = "(car list)\n"
"Returns the first item in a list.";
const char doc63[] PROGMEM = "(cdr list)\n"
"Returns a list with the first item removed.";
const char doc65[] PROGMEM = "(caar list)";
const char doc66[] PROGMEM = "(cadr list)";
const char doc68[] PROGMEM = "(cdar list)\n"
"Equivalent to (cdr (car list)).";
const char doc69[] PROGMEM = "(cddr list)\n"
"Equivalent to (cdr (cdr list)).";
const char doc70[] PROGMEM = "(caaar list)\n"
"Equivalent to (car (car (car list))).";
const char doc71[] PROGMEM = "(caadr list)\n"
"Equivalent to (car (car (cdar list))).";
const char doc72[] PROGMEM = "(cadar list)\n"
"Equivalent to (car (cdr (car list))).";
const char doc73[] PROGMEM = "(caddr list)\n"
"Equivalent to (car (cdr (cdr list))).";
const char doc75[] PROGMEM = "(cdaar list)\n"
"Equivalent to (cdar (car (car list))).";
const char doc76[] PROGMEM = "(cdadr list)\n"
"Equivalent to (cdr (car (cdr list))).";
const char doc77[] PROGMEM = "(cddar list)\n"
"Equivalent to (cdr (cdr (car list))).";
const char doc78[] PROGMEM = "(cdddr list)\n"
"Equivalent to (cdr (cdr (cdr list))).";
const char doc79[] PROGMEM = "(length item)\n"
"Returns the number of items in a list, the length of a string, or the length of a one-dimensional array.";
const char doc80[] PROGMEM = "(array-dimensions item)\n"
"Returns a list of the dimensions of an array.";
const char doc81[] PROGMEM = "(list item*)\n"
"Returns a list of the values of its arguments.";
const char doc82[] PROGMEM = "(make-array size [:initial-element element] [:element-type 'bit])\n"
"If size is an integer it creates a one-dimensional array with elements from 0 to size-1.\n"
"If size is a list of n integers it creates an n-dimensional array with those dimensions.\n"
"If :element-type 'bit is specified the array is a bit array.";
const char doc83[] PROGMEM = "(reverse list)\n"
"Returns a list with the elements of list in reverse order.";
const char doc84[] PROGMEM = "(nth number list)\n"
"Returns the nth item in list, counting from zero.";
const char doc85[] PROGMEM = "(aref array index [index*])\n"
"Returns an element from the specified array.";
const char doc86[] PROGMEM = "(assoc key list)\n"
"Looks up a key in an association list of (key . value) pairs,\n"
"and returns the matching pair, or nil if no pair is found.";
const char doc87[] PROGMEM = "(member item list)\n"
"Searches for an item in a list, using eq, and returns the list starting from the first occurrence of the item,\n"
"or nil if it is not found.";
const char doc88[] PROGMEM = "(apply function list)\n"
"Returns the result of evaluating function, with the list of arguments specified by the second parameter.";
const char doc89[] PROGMEM = "(funcall function argument*)\n"
"Evaluates function with the specified arguments.";
const char doc90[] PROGMEM = "(append list*)\n"
"Joins its arguments, which should be lists, into a single list.";
const char doc91[] PROGMEM = "(mapc function list1 [list]*)\n"
"Applies the function to each element in one or more lists, ignoring the results.\n"
"It returns the first list argument.";
const char doc92[] PROGMEM = "(mapcar function list1 [list]*)\n"
"Applies the function to each element in one or more lists, and returns the resulting list.";
const char doc93[] PROGMEM = "(mapcan function list1 [list]*)\n"
"Applies the function to each element in one or more lists. The results should be lists,\n"
"and these are appended together to give the value returned.";
const char doc94[] PROGMEM = "(+ number*)\n"
"Adds its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise a floating-point number.";
const char doc95[] PROGMEM = "(- number*)\n"
"If there is one argument, negates the argument.\n"
"If there are two or more arguments, subtracts the second and subsequent arguments from the first argument.\n"
"If each argument is an integer, and the running total doesn't overflow, returns the result as an integer,\n"
"otherwise a floating-point number.";
const char doc96[] PROGMEM = "(* number*)\n"
"Multiplies its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise it's a floating-point number.";
const char doc97[] PROGMEM = "(/ number*)\n"
"Divides the first argument by the second and subsequent arguments.\n"
"If each argument is an integer, and each division produces an exact result, the result is an integer;\n"
"otherwise it's a floating-point number.";
const char doc98[] PROGMEM = "(mod number number)\n"
"Returns its first argument modulo the second argument.\n"
"If both arguments are integers the result is an integer; otherwise it's a floating-point number.";
const char doc99[] PROGMEM = "(1+ number)\n"
"Adds one to its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc100[] PROGMEM = "(1- number)\n"
"Subtracts one from its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc101[] PROGMEM = "(abs number)\n"
"Returns the absolute, positive value of its argument.\n"
"If the argument is an integer the result will be returned as an integer if possible,\n"
"otherwise a floating-point number.";
const char doc102[] PROGMEM = "(random number)\n"
"If number is an integer returns a random number between 0 and one less than its argument.\n"
"Otherwise returns a floating-point number between zero and number.";
const char doc103[] PROGMEM = "(max number*)\n"
"Returns the maximum of one or more arguments.";
const char doc104[] PROGMEM = "(min number*)\n"
"Returns the minimum of one or more arguments.";
const char doc105[] PROGMEM = "(/= number*)\n"
"Returns t if none of the arguments are equal, or nil if two or more arguments are equal.";
const char doc106[] PROGMEM = "(= number*)\n"
"Returns t if all the arguments, which must be numbers, are numerically equal, and nil otherwise.";
const char doc107[] PROGMEM = "(< number*)\n"
"Returns t if each argument is less than the next argument, and nil otherwise.";
const char doc108[] PROGMEM = "(<= number*)\n"
"Returns t if each argument is less than or equal to the next argument, and nil otherwise.";
const char doc109[] PROGMEM = "(> number*)\n"
"Returns t if each argument is greater than the next argument, and nil otherwise.";
const char doc110[] PROGMEM = "(>= number*)\n"
"Returns t if each argument is greater than or equal to the next argument, and nil otherwise.";
const char doc111[] PROGMEM = "(plusp number)\n"
"Returns t if the argument is greater than zero, or nil otherwise.";
const char doc112[] PROGMEM = "(minusp number)\n"
"Returns t if the argument is less than zero, or nil otherwise.";
const char doc113[] PROGMEM = "(zerop number)\n"
"Returns t if the argument is zero.";
const char doc114[] PROGMEM = "(oddp number)\n"
"Returns t if the integer argument is odd.";
const char doc115[] PROGMEM = "(evenp number)\n"
"Returns t if the integer argument is even.";
const char doc116[] PROGMEM = "(integerp number)\n"
"Returns t if the argument is an integer.";
const char doc117[] PROGMEM = "(numberp number)\n"
"Returns t if the argument is a number.";
const char doc118[] PROGMEM = "(float number)\n"
"Returns its argument converted to a floating-point number.";
const char doc119[] PROGMEM = "(floatp number)\n"
"Returns t if the argument is a floating-point number.";
const char doc120[] PROGMEM = "(sin number)\n"
"Returns sin(number).";
const char doc121[] PROGMEM = "(cos number)\n"
"Returns cos(number).";
const char doc122[] PROGMEM = "(tan number)\n"
"Returns tan(number).";
const char doc123[] PROGMEM = "(asin number)\n"
"Returns asin(number).";
const char doc124[] PROGMEM = "(acos number)\n"
"Returns acos(number).";
const char doc125[] PROGMEM = "(atan number1 [number2])\n"
"Returns the arc tangent of number1/number2, in radians. If number2 is omitted it defaults to 1.";
const char doc126[] PROGMEM = "(sinh number)\n"
"Returns sinh(number).";
const char doc127[] PROGMEM = "(cosh number)\n"
"Returns cosh(number).";
const char doc128[] PROGMEM = "(tanh number)\n"
"Returns tanh(number).";
const char doc129[] PROGMEM = "(exp number)\n"
"Returns exp(number).";
const char doc130[] PROGMEM = "(sqrt number)\n"
"Returns sqrt(number).";
const char doc131[] PROGMEM = "(number [base])\n"
"Returns the logarithm of number to the specified base. If base is omitted it defaults to e.";
const char doc132[] PROGMEM = "(expt number power)\n"
"Returns number raised to the specified power.\n"
"Returns the result as an integer if the arguments are integers and the result will be within range,\n"
"otherwise a floating-point number.";
const char doc133[] PROGMEM = "(ceiling number [divisor])\n"
"Returns ceil(number/divisor). If omitted, divisor is 1.";
const char doc134[] PROGMEM = "(floor number [divisor])\n"
"Returns floor(number/divisor). If omitted, divisor is 1.";
const char doc135[] PROGMEM = "(truncate number)\n"
"Returns t if the argument is a floating-point number.";
const char doc136[] PROGMEM = "(round number)\n"
"Returns t if the argument is a floating-point number.";
const char doc137[] PROGMEM = "(char string n)\n"
"Returns the nth character in a string, counting from zero.";
const char doc138[] PROGMEM = "(char-code character)\n"
"Returns the ASCII code for a character, as an integer.";
const char doc139[] PROGMEM = "(code-char integer)\n"
"Returns the character for the specified ASCII code.";
const char doc140[] PROGMEM = "(characterp item)\n"
"Returns t if the argument is a character and nil otherwise.";
const char doc141[] PROGMEM = "(stringp item)\n"
"Returns t if the argument is a string and nil otherwise.";
const char doc142[] PROGMEM = "(string= string string)\n"
"Tests whether two strings are the same.";
const char doc143[] PROGMEM = "(string< string string)\n"
"Returns t if the first string is alphabetically less than the second string, and nil otherwise. For example:";
const char doc144[] PROGMEM = "(string> string string)\n"
"Returns t if the first string is alphabetically greater than the second string, and nil otherwise.";
const char doc145[] PROGMEM = "(sort list test)\n"
"Destructively sorts list according to the test function, using an insertion sort, and returns the sorted list.";
const char doc146[] PROGMEM = "(string item)\n"
"Converts its argument to a string.";
const char doc147[] PROGMEM = "(concatenate 'string string*)\n"
"Joins together the strings given in the second and subsequent arguments, and returns a single string.";
const char doc148[] PROGMEM = "(subseq string start [end])\n"
"Returns a substring from a string, from character start to character end-1:";
const char doc149[] PROGMEM = "(read-from-string string)\n"
"Reads an atom or list from the specified string and returns it.";
const char doc150[] PROGMEM = "(princ-to-string item)\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc151[] PROGMEM = "(prin1-to-string item [stream])\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed with quotation marks and escape characters,\n"
"in a format that will be suitable for read-from-string.";
const char doc152[] PROGMEM = "(logand [value*])\n"
"Returns the bitwise & of the values.";
const char doc153[] PROGMEM = "(logior [value*])\n"
"Returns the bitwise | of the values.";
const char doc154[] PROGMEM = "(logxor [value*])\n"
"Returns the bitwise ^ of the values.";
const char doc155[] PROGMEM = "(prin1-to-string item [stream])\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed with quotation marks and escape characters,\n"
"in a format that will be suitable for read-from-string.";
const char doc156[] PROGMEM = "(ash value shift)\n"
"Returns the result of bitwise shifting value by shift bits. If shift is positive, value is shifted to the left.";
const char doc157[] PROGMEM = "(logbitp bit value)\n"
"Returns t if bit number bit in value is a '1', and nil if it is a '0'.";
const char doc158[] PROGMEM = "(eval form*)\n"
"Evaluates its argument an extra time.";
const char doc159[] PROGMEM = "(globals)\n"
"Returns an association list of global variables and their values.";
const char doc160[] PROGMEM = "(locals)\n"
"Returns an association list of local variables and their values.";
const char doc161[] PROGMEM = "(makunbound symbol)\n"
"Removes the value of the symbol from GlobalEnv and returns the symbol.";
const char doc162[] PROGMEM = "(break)\n"
"Inserts a breakpoint in the program. When evaluated prints Break! and reenters the REPL.";
const char doc163[] PROGMEM = "(read [stream])\n"
"Reads an atom or list from the serial input and returns it.\n"
"If stream is specified the item is read from the specified stream.";
const char doc164[] PROGMEM = "(prin1 item [stream])\n"
"Prints its argument, and returns its value.\n"
"Strings are printed with quotation marks and escape characters.";
const char doc165[] PROGMEM = "(print item [stream])\n"
"Prints its argument with quotation marks and escape characters, on a new line, and followed by a space.\n"
"If stream is specified the argument is printed to the specified stream.";
const char doc166[] PROGMEM = "(princ item [stream])\n"
"Prints its argument, and returns its value.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc167[] PROGMEM = "(terpri [stream])\n"
"Prints a new line, and returns nil.\n"
"If stream is specified the new line is written to the specified stream.";
const char doc168[] PROGMEM = "(read-byte stream)\n"
"Reads a byte from a stream and returns it.";
const char doc169[] PROGMEM = "(read-line [stream])\n"
"Reads characters from the serial input up to a newline character, and returns them as a string, excluding the newline.\n"
"If stream is specified the line is read from the specified stream.";
const char doc170[] PROGMEM = "(write-byte number [stream])\n"
"Writes a byte to a stream.";
const char doc171[] PROGMEM = "(write-string string [stream])\n"
"Writes a string. If stream is specified the string is written to the stream.";
const char doc172[] PROGMEM = "(write-line string [stream])\n"
"Writes a string terminated by a newline character. If stream is specified the string is written to the stream.";
const char doc173[] PROGMEM = "(restart-i2c stream [read-p])\n"
"Restarts an i2c-stream.\n"
"If read-p is nil or omitted the stream is written to.\n"
"If read-p is an integer it specifies the number of bytes to be read from the stream.";
const char doc174[] PROGMEM = "(gc)\n"
"Forces a garbage collection and prints the number of objects collected, and the time taken.";
const char doc175[] PROGMEM = "(room)\n"
"Returns the number of free Lisp cells remaining.";
// const char doc176[] PROGMEM = "(save-image [symbol])\n"
// "Saves the current uLisp image to non-volatile memory or SD card so it can be loaded using load-image.";
// const char doc177[] PROGMEM = "(load-image [filename])\n"
// "Loads a saved uLisp image from non-volatile memory or SD card.";
const char doc178[] PROGMEM = "(cls)\n"
"Prints a clear-screen character.";
const char doc179[] PROGMEM = "(pinmode pin mode)\n"
"Sets the input/output mode of an Arduino pin number, and returns nil.\n"
"The mode parameter can be an integer, a keyword, or t or nil.";
const char doc180[] PROGMEM = "(digitalread pin)\n"
"Reads the state of the specified Arduino pin number and returns t (high) or nil (low).";
const char doc181[] PROGMEM = "(digitalwrite pin state)\n"
"Sets the state of the specified Arduino pin number.";
const char doc182[] PROGMEM = "(analogread pin)\n"
"Reads the specified Arduino analogue pin number and returns the value.";
const char doc183[] PROGMEM = "(analogreadresolution bits)\n"
"Specifies the resolution for the analogue inputs on platforms that support it.\n"
"The default resolution on all platforms is 10 bits.";
const char doc184[] PROGMEM = "(analogwrite pin value)\n"
"Writes the value to the specified Arduino pin number.";
const char doc185[] PROGMEM = "(delay number)\n"
"Delays for a specified number of milliseconds.";
const char doc186[] PROGMEM = "(millis)\n"
"Returns the time in milliseconds that uLisp has been running.";
const char doc187[] PROGMEM = "(sleep secs)\n"
"Puts the processor into a low-power sleep mode for secs.\n"
"Only supported on some platforms. On other platforms it does delay(1000*secs).";
const char doc188[] PROGMEM = "(note [pin] [note] [octave])\n"
"Generates a square wave on pin.\n"
"The argument note represents the note in the well-tempered scale, from 0 to 11,\n"
"where 0 represents C, 1 represents C#, and so on.\n"
"The argument octave can be from 3 to 6. If omitted it defaults to 0.";
const char doc189[] PROGMEM = "(register address [value])\n"
"Reads or writes the value of a peripheral register.\n"
"If value is not specified the function returns the value of the register at address.\n"
"If value is specified the value is written to the register at address and the function returns value.";
const char doc190[] PROGMEM = "(edit 'function)\n"
"Calls the Lisp tree editor to allow you to edit a function definition.";
const char doc191[] PROGMEM = "(pprint item [str])\n"
"Prints its argument, using the pretty printer, to display it formatted in a structured way.\n"
"If str is specified it prints to the specified stream. It returns no value.";
const char doc192[] PROGMEM = "(pprintall [str])\n"
"Pretty-prints the definition of every function and variable defined in the uLisp workspace.\n"
"Is str is specified it prints to the specified stream. It returns no value.";
const char doc193[] PROGMEM = "(format output controlstring arguments*)\n"
"Outputs its arguments formatted according to the format directives in controlstring.";
const char doc194[] PROGMEM = "(require 'symbol)\n"
"Loads the definition of a function defined with defun, or a variable defined with defvar, from the Lisp Library.\n"
"It returns t if it was loaded, or nil if the symbol is already defined or isn't defined in the Lisp Library.";
const char doc195[] PROGMEM = "(list-library)\n"
"Prints a list of the functions defined in the List Library.";
const char doc196[] PROGMEM = "(documentation 'symbol [type])\n"
"Returns the documentation string of a built-in or user-defined function. The type argument is ignored.";
const char doc197[] PROGMEM = "(available stream)\n"
"Returns the number of bytes available for reading from the wifi-stream, or zero if no bytes are available.";
const char doc198[] PROGMEM = "(wifi-server)\n"
"Starts a Wi-Fi server running. It returns nil.";
const char doc199[] PROGMEM = "(wifi-softap ssid [password channel hidden])\n"
"Set up a soft access point to establish a Wi-Fi network.\n"
"Returns the IP address as a string or nil if unsuccessful.";
const char doc200[] PROGMEM = "(connected stream)\n"
"Returns t or nil to indicate if the client on stream is connected.";
const char doc201[] PROGMEM = "(wifi-localip)\n"
"Returns the IP address of the local network as a string.";
const char doc202[] PROGMEM = "(wifi-connect [ssid pass])\n"
"Connects to the Wi-Fi network ssid using password pass. It returns the IP address as a string.";
const char doc203[] PROGMEM = "(draw-pixel x y [colour])\n"
"Draws a pixel at coordinates (x,y) in colour, or white if omitted.";
const char doc204[] PROGMEM = "(draw-line x0 y0 x1 y1 [colour])\n"
"Draws a line from (x0,y0) to (x1,y1) in colour, or white if omitted.";
const char doc205[] PROGMEM = "(draw-rect x y w h [colour])\n"
"Draws an outline rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc206[] PROGMEM = "(fill-rect x y w h [colour])\n"
"Draws a filled rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc207[] PROGMEM = "(draw-circle x y r [colour])\n"
"Draws an outline circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc208[] PROGMEM = "(fill-circle x y r [colour])\n"
"Draws a filled circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc209[] PROGMEM = "(draw-round-rect x y w h radius [colour])\n"
"Draws an outline rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc210[] PROGMEM = "(fill-round-rect x y w h radius [colour])\n"
"Draws a filled rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc211[] PROGMEM = "(draw-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws an outline triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc212[] PROGMEM = "(fill-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws a filled triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc213[] PROGMEM = "(draw-char x y char [colour background size])\n"
"Draws the character char with its top left corner at (x,y).\n"
"The character is drawn in a 5 x 7 pixel font in colour against background,\n"
"which default to white and black respectively.\n"
"The character can optionally be scaled by size.";
const char doc214[] PROGMEM = "(set-cursor x y)\n"
"Sets the start point for text plotting to (x, y).";
const char doc215[] PROGMEM = "(set-text-color colour [background])\n"
"Sets the text colour for text plotted using (with-gfx ...).";
const char doc216[] PROGMEM = "(set-text-size scale)\n"
"Scales text by the specified size, default 1.";
const char doc217[] PROGMEM = "(set-text-wrap boolean)\n"
"Specified whether text wraps at the right-hand edge of the display; the default is t.";
const char doc218[] PROGMEM = "(fill-screen [colour])\n"
"Fills or clears the screen with colour, default black.";
const char doc219[] PROGMEM = "(set-rotation option)\n"
"Sets the display orientation for subsequent graphics commands; values are 0, 1, 2, or 3.";
const char doc220[] PROGMEM = "(invert-display boolean)\n"
"Mirror-images the display.";

const char doc_batteryvoltage[] PROGMEM = "(battery:voltage)\n"
"Returns the current voltage of the LiPo battery powering the board.";
const char doc_batterypercentage[] PROGMEM = "(battery:percentage)\n"
"Returns the current percentage charge of the LiPo battery powering the board, from 0-100%.";
const char doc_batterychangerate[] PROGMEM = "(battery:change-rate)\n"
"Returns the change rate of the LiPo battery powering the board, that is, how fast it is charging or discharging.";

// Insert your own function documentation here

// Built-in symbol lookup table
const tbl_entry_t lookup_table[] PROGMEM = {
  { string0, NULL, 0x00, doc0 },
  { string1, NULL, 0x00, doc1 },
  { string2, NULL, 0x00, doc2 },
  { string3, NULL, 0x00, doc3 },
  { string4, NULL, 0x00, NULL },
  { string5, NULL, 0x00, NULL },
  { string6, NULL, 0x00, NULL },
  { string7, NULL, 0x00, doc7 },
  { string8, NULL, 0x0F, doc8 },
  { string9, NULL, 0x0F, doc9 },
  { string10, NULL, 0x0F, doc10 },
  { string11, NULL, 0x0F, NULL },
  { string12, NULL, 0x0F, NULL },
  { string13, NULL, 0x00, NULL },
  { string14, sp_quote, 0x11, NULL },
  { string15, sp_or, 0x0F, doc15 },
  { string16, sp_defun, 0x2F, doc16 },
  { string17, sp_defvar, 0x13, doc17 },
  { string18, sp_setq, 0x2F, doc18 },
  { string19, sp_loop, 0x0F, doc19 },
  { string20, sp_return, 0x0F, doc20 },
  { string21, sp_push, 0x22, doc21 },
  { string22, sp_pop, 0x11, doc22 },
  { string23, sp_incf, 0x12, doc23 },
  { string24, sp_decf, 0x12, doc24 },
  { string25, sp_setf, 0x2F, doc25 },
  { string26, sp_dolist, 0x1F, doc26 },
  { string27, sp_dotimes, 0x1F, doc27 },
  { string28, sp_trace, 0x01, doc28 },
  { string29, sp_untrace, 0x01, doc29 },
  { string30, sp_formillis, 0x1F, doc30 },
  { string31, sp_time, 0x11, doc31 },
  { string32, sp_withoutputtostring, 0x1F, doc32 },
  { string33, sp_withserial, 0x1F, doc33 },
  { string34, sp_withi2c, 0x1F, doc34 },
  { string35, sp_withspi, 0x1F, doc35 },
  { string36, sp_withsdcard, 0x2F, doc36 },
  { string37, sp_withgfx, 0x1F, doc37 },
  { string38, sp_withclient, 0x12, doc38 },

  { string_unwindprotect, sp_unwindprotect, 0x1F, doc_unwindprotect },
  { string_ignoreerrors, sp_ignoreerrors, 0x11, doc_ignoreerrors },
//   { string_error, sp_error, 0x2F, doc_error },
  { string_MACRO_SENTINEL, NULL, 0x00, NULL },
  { string_defmacro, sp_defmacro, 0x2F, doc_defmacro },
  { string_quasiquote, sp_quasiquote, 0x11, doc_quasiquote },
  { string_unquote, sp_unquote, 0x11, doc_unquote },
  { string_unquotesplicing, sp_unquote_splicing, 0x11, doc_unquotesplicing },
  { string_expand, sp_expand, 0x23, doc_expand },

  { string39, NULL, 0x00, NULL },
  { string40, tf_progn, 0x0F, doc40 },
  { string41, tf_if, 0x23, doc41 },
  { string42, tf_cond, 0x0F, doc42 },
  { string43, tf_when, 0x1F, doc43 },
  { string44, tf_unless, 0x1F, doc44 },
  { string45, tf_case, 0x1F, doc45 },
  { string46, tf_and, 0x0F, doc46 },
  { string47, tf_help, 0x11, doc47 },
  { string48, NULL, 0x00, NULL },

  { string_geterror, fn_geterror, 0x00, doc_geterror },
  { string_throw, fn_throw, 0x11, doc_throw },
  { string_intern, fn_intern, 0x11, doc_intern },

  { string49, fn_not, 0x11, doc49 },
  { string50, fn_not, 0x11, NULL },
  { string51, fn_cons, 0x22, doc51 },
  { string52, fn_atom, 0x11, doc52 },
  { string53, fn_listp, 0x11, doc53 },
  { string54, fn_consp, 0x11, doc54 },
  { string55, fn_symbolp, 0x11, doc55 },
  { string56, fn_arrayp, 0x11, doc56 },
  { string57, fn_boundp, 0x11, doc57 },
  { string58, fn_setfn, 0x2F, doc58 },
  { string59, fn_streamp, 0x11, doc59 },
  { string60, fn_eq, 0x22, doc60 },
  { string61, fn_car, 0x11, doc61 },
  { string62, fn_car, 0x11, NULL },
  { string63, fn_cdr, 0x11, doc63 },
  { string64, fn_cdr, 0x11, NULL },
  { string65, fn_caar, 0x11, doc65 },
  { string66, fn_cadr, 0x11, doc66 },
  { string67, fn_cadr, 0x11, NULL },
  { string68, fn_cdar, 0x11, doc68 },
  { string69, fn_cddr, 0x11, doc69 },
  { string70, fn_caaar, 0x11, doc70 },
  { string71, fn_caadr, 0x11, doc71 },
  { string72, fn_cadar, 0x11, doc72 },
  { string73, fn_caddr, 0x11, doc73 },
  { string74, fn_caddr, 0x11, NULL },
  { string75, fn_cdaar, 0x11, doc75 },
  { string76, fn_cdadr, 0x11, doc76 },
  { string77, fn_cddar, 0x11, doc77 },
  { string78, fn_cdddr, 0x11, doc78 },
  { string79, fn_length, 0x11, doc79 },
  { string80, fn_arraydimensions, 0x11, doc80 },
  { string81, fn_list, 0x0F, doc81 },
  { string82, fn_makearray, 0x15, doc82 },
  { string83, fn_reverse, 0x11, doc83 },
  { string84, fn_nth, 0x22, doc84 },
  { string85, fn_aref, 0x2F, doc85 },
  { string86, fn_assoc, 0x22, doc86 },
  { string87, fn_member, 0x22, doc87 },
  { string88, fn_apply, 0x2F, doc88 },
  { string89, fn_funcall, 0x1F, doc89 },
  { string90, fn_append, 0x0F, doc90 },
  { string91, fn_mapc, 0x2F, doc91 },
  { string92, fn_mapcar, 0x2F, doc92 },
  { string93, fn_mapcan, 0x2F, doc93 },
  { string94, fn_add, 0x0F, doc94 },
  { string95, fn_subtract, 0x1F, doc95 },
  { string96, fn_multiply, 0x0F, doc96 },
  { string97, fn_divide, 0x1F, doc97 },
  { string98, fn_mod, 0x22, doc98 },
  { string99, fn_oneplus, 0x11, doc99 },
  { string100, fn_oneminus, 0x11, doc100 },
  { string101, fn_abs, 0x11, doc101 },
  { string102, fn_random, 0x11, doc102 },
  { string103, fn_maxfn, 0x1F, doc103 },
  { string104, fn_minfn, 0x1F, doc104 },
  { string105, fn_noteq, 0x1F, doc105 },
  { string106, fn_numeq, 0x1F, doc106 },
  { string107, fn_less, 0x1F, doc107 },
  { string108, fn_lesseq, 0x1F, doc108 },
  { string109, fn_greater, 0x1F, doc109 },
  { string110, fn_greatereq, 0x1F, doc110 },
  { string111, fn_plusp, 0x11, doc111 },
  { string112, fn_minusp, 0x11, doc112 },
  { string113, fn_zerop, 0x11, doc113 },
  { string114, fn_oddp, 0x11, doc114 },
  { string115, fn_evenp, 0x11, doc115 },
  { string116, fn_integerp, 0x11, doc116 },
  { string117, fn_numberp, 0x11, doc117 },
  { string118, fn_floatfn, 0x11, doc118 },
  { string119, fn_floatp, 0x11, doc119 },
  { string120, fn_sin, 0x11, doc120 },
  { string121, fn_cos, 0x11, doc121 },
  { string122, fn_tan, 0x11, doc122 },
  { string123, fn_asin, 0x11, doc123 },
  { string124, fn_acos, 0x11, doc124 },
  { string125, fn_atan, 0x12, doc125 },
  { string126, fn_sinh, 0x11, doc126 },
  { string127, fn_cosh, 0x11, doc127 },
  { string128, fn_tanh, 0x11, doc128 },
  { string129, fn_exp, 0x11, doc129 },
  { string130, fn_sqrt, 0x11, doc130 },
  { string131, fn_log, 0x12, doc131 },
  { string132, fn_expt, 0x22, doc132 },
  { string133, fn_ceiling, 0x12, doc133 },
  { string134, fn_floor, 0x12, doc134 },
  { string135, fn_truncate, 0x12, doc135 },
  { string136, fn_round, 0x12, doc136 },
  { string137, fn_char, 0x22, doc137 },
  { string138, fn_charcode, 0x11, doc138 },
  { string139, fn_codechar, 0x11, doc139 },
  { string140, fn_characterp, 0x11, doc140 },
  { string141, fn_stringp, 0x11, doc141 },
  { string142, fn_stringeq, 0x22, doc142 },
  { string143, fn_stringless, 0x22, doc143 },
  { string144, fn_stringgreater, 0x22, doc144 },
  { string145, fn_sort, 0x22, doc145 },
  { string146, fn_stringfn, 0x11, doc146 },
  { string147, fn_concatenate, 0x1F, doc147 },
  { string148, fn_subseq, 0x23, doc148 },
  { string149, fn_readfromstring, 0x11, doc149 },
  { string150, fn_princtostring, 0x11, doc150 },
  { string151, fn_prin1tostring, 0x11, doc151 },
  { string152, fn_logand, 0x0F, doc152 },
  { string153, fn_logior, 0x0F, doc153 },
  { string154, fn_logxor, 0x0F, doc154 },
  { string155, fn_lognot, 0x11, doc155 },
  { string156, fn_ash, 0x22, doc156 },
  { string157, fn_logbitp, 0x22, doc157 },
  { string158, fn_eval, 0x11, doc158 },
  { string159, fn_globals, 0x00, doc159 },
  { string160, fn_locals, 0x00, doc160 },
  { string161, fn_makunbound, 0x11, doc161 },
  { string162, fn_break, 0x00, doc162 },
  { string163, fn_read, 0x01, doc163 },
  { string164, fn_prin1, 0x12, doc164 },
  { string165, fn_print, 0x12, doc165 },
  { string166, fn_princ, 0x12, doc166 },
  { string167, fn_terpri, 0x01, doc167 },
  { string168, fn_readbyte, 0x02, doc168 },
  { string169, fn_readline, 0x01, doc169 },
  { string170, fn_writebyte, 0x12, doc170 },
  { string171, fn_writestring, 0x12, doc171 },
  { string172, fn_writeline, 0x12, doc172 },
  { string173, fn_restarti2c, 0x12, doc173 },
  { string174, fn_gc, 0x00, doc174 },
  { string175, fn_room, 0x00, doc175 },
//   { string176, fn_saveimage, 0x01, doc176 },
//   { string177, fn_loadimage, 0x01, doc177 },
  { string178, fn_cls, 0x00, doc178 },
  { string179, fn_pinmode, 0x22, doc179 },
  { string180, fn_digitalread, 0x11, doc180 },
  { string181, fn_digitalwrite, 0x22, doc181 },
  { string182, fn_analogread, 0x11, doc182 },
  { string183, fn_analogreadresolution, 0x11, doc183 },
  { string184, fn_analogwrite, 0x22, doc184 },
  { string185, fn_delay, 0x11, doc185 },
  { string186, fn_millis, 0x00, doc186 },
  { string187, fn_sleep, 0x11, doc187 },
  { string188, fn_note, 0x03, doc188 },
  { string189, fn_register, 0x12, doc189 },
  { string190, fn_edit, 0x11, doc190 },
  { string191, fn_pprint, 0x12, doc191 },
  { string192, fn_pprintall, 0x01, doc192 },
  { string193, fn_format, 0x2F, doc193 },
  { string194, fn_require, 0x11, doc194 },
  { string195, fn_listlibrary, 0x00, doc195 },
  { string196, fn_documentation, 0x12, doc196 },
  { string197, fn_available, 0x11, doc197 },
  { string198, fn_wifiserver, 0x00, doc198 },
  { string199, fn_wifisoftap, 0x04, doc199 },
  { string200, fn_connected, 0x11, doc200 },
  { string201, fn_wifilocalip, 0x00, doc201 },
  { string202, fn_wificonnect, 0x03, doc202 },
  { string203, fn_drawpixel, 0x23, doc203 },
  { string204, fn_drawline, 0x45, doc204 },
  { string205, fn_drawrect, 0x45, doc205 },
  { string206, fn_fillrect, 0x45, doc206 },
  { string207, fn_drawcircle, 0x34, doc207 },
  { string208, fn_fillcircle, 0x34, doc208 },
  { string209, fn_drawroundrect, 0x56, doc209 },
  { string210, fn_fillroundrect, 0x56, doc210 },
  { string211, fn_drawtriangle, 0x67, doc211 },
  { string212, fn_filltriangle, 0x67, doc212 },
  { string213, fn_drawchar, 0x36, doc213 },
  { string214, fn_setcursor, 0x22, doc214 },
  { string215, fn_settextcolor, 0x12, doc215 },
  { string216, fn_settextsize, 0x11, doc216 },
  { string217, fn_settextwrap, 0x11, doc217 },
  { string218, fn_fillscreen, 0x01, doc218 },
  { string219, fn_setrotation, 0x11, doc219 },
  { string220, fn_invertdisplay, 0x11, doc220 },
  { string221, NULL, 0x00, NULL },
  { string222, (fn_ptr_type)LED_BUILTIN, 0, NULL },
  { string223, (fn_ptr_type)HIGH, DIGITALWRITE, NULL },
  { string224, (fn_ptr_type)LOW, DIGITALWRITE, NULL },
  { string225, (fn_ptr_type)INPUT, PINMODE, NULL },
  { string226, (fn_ptr_type)INPUT_PULLUP, PINMODE, NULL },
  { string227, (fn_ptr_type)INPUT_PULLDOWN, PINMODE, NULL },
  { string228, (fn_ptr_type)OUTPUT, PINMODE, NULL },
  { string229, NULL, 0x00, NULL },

// Insert your own table entries here
  { string_batteryvoltage, fn_batteryvoltage, 0x00, doc_batteryvoltage },
  { string_batterypercentage, fn_batterypercentage, 0x00, doc_batterypercentage },
  { string_batterychangerate, fn_batterychangerate, 0x00, doc_batterychangerate },

};

// Table lookup functions

/*
  lookupbuiltin - looks up a string in lookup_table[], and returns the index of its entry,
  or ENDFUNCTIONS if no match is found
*/
builtin_t lookupbuiltin (char* n) {
  int entry = 0;
  while (entry < ENDFUNCTIONS) {
    if (strcasecmp_P(n, lookup_table[entry].string) == 0)
      return (builtin_t)entry;
    entry++;
  }
  return ENDFUNCTIONS;
}

/*
  lookupfn - looks up the entry for name in lookup_table[], and returns the function entry point
*/
intptr_t lookupfn (builtin_t name) {
  return (intptr_t)pgm_read_ptr(&lookup_table[name].fptr);
}

/*
  getminmax - gets the byte from lookup_table[] whose nibbles specify the minimum and maximum number of arguments for name
*/
uint8_t getminmax (builtin_t name) {
  uint8_t minmax = pgm_read_byte(&lookup_table[name].minmax);
  return minmax;
}

/*
  checkminmax - checks that the number of arguments nargs for name is within the range specified in lookup_table[]
*/
void checkminmax (builtin_t name, int nargs) {
  uint8_t minmax = getminmax(name);
  if (nargs<(minmax >> 4)) error2(name, toofewargs);
  if ((minmax & 0x0f) != 0x0f && nargs>(minmax & 0x0f)) error2(name, toomanyargs);
}

/*
  lookupdoc - looks up the documentation string for the built-in function name
*/
char *lookupdoc (builtin_t name) {
  return (char*)lookup_table[name].doc;
}

/*
  testescape - tests whether the '~' escape character has been typed
*/
void testescape () {
  if (Serial.read() == '~') error2(NIL, PSTR("escape!"));
}

// Main evaluator

/*
  eval - the main Lisp evaluator
*/
object *eval (object *form, object *env) {
  int TC=0;
  EVAL:
  yield();  // Needed on ESP8266 to avoid Soft WDT Reset or ESP32 for multithreading
  // Enough space?
  if (Freespace <= WORKSPACESIZE>>4) gc(form, env);
  // Escape
  if (tstflag(ESCAPE)) { clrflag(ESCAPE); error2(NIL, PSTR("escape!"));}
  if (!tstflag(NOESC)) testescape();

  // Evaluates to itself?
  if (form == NULL) return nil;

  if (form->type >= NUMBER && form->type <= STRING) return form;

  if (symbolp(form)) {
    symbol_t name = form->name;
    object *pair = value(name, env);
    if (pair != NULL) return cdr(pair);
    pair = value(name, GlobalEnv);
    if (pair != NULL) return cdr(pair);
    else if (builtinp(name)) return form;
    error(NIL, PSTR("undefined"), form);
  }

  // It's a list
  object *function = car(form);
  object *args = cdr(form);
  object *unevaled_args = cdr(form);

  if (function == NULL) error(NIL, PSTR("illegal function"), nil);
  if (!listp(args)) error(NIL, PSTR("can't evaluate a dotted pair"), args);

  // List starts with a symbol?
  if (symbolp(function)) {
    builtin_t name = builtin(function->name);

    if ((name == LET) || (name == LETSTAR)) {
      int TCstart = TC;
      if (args == NULL) error2(name, noargument);
      object *assigns = first(args);
      if (!listp(assigns)) error(name, notalist, assigns);
      object *forms = cdr(args);
      object *newenv = env;
      push(newenv, GCStack);
      while (assigns != NULL) {
        object *assign = car(assigns);
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
      object *envcopy = NULL;
      while (env != NULL) {
        object *pair = first(env);
        if (pair != NULL) push(pair, envcopy);
        env = cdr(env);
      }
      return cons(bsymbol(CLOSURE), cons(envcopy,args));
    }

    if ((name > SPECIAL_FORMS) && (name < TAIL_FORMS)) {
      return ((fn_ptr_type)lookupfn(name))(args, env);
    }

    if ((name > TAIL_FORMS) && (name < FUNCTIONS)) {
      form = ((fn_ptr_type)lookupfn(name))(args, env);
      TC = 1;
      goto EVAL;
    }
    if (((name > 0) && (name < SPECIAL_FORMS)) || ((name > KEYWORDS) && (name < USERFUNCTIONS))) error2(name, PSTR("can't be used as a function"));
  }

  // Evaluate the parameters - result in head
  object *fname = car(form);
  int TCstart = TC;
  object *head = cons(eval(fname, env), NULL);
  push(head, GCStack); // Don't GC the result list
  object *tail = head;
  form = cdr(form);
  int nargs = 0;

  while (form != NULL){
    object *obj = cons(eval(car(form),env),NULL);
    cdr(tail) = obj;
    tail = obj;
    form = cdr(form);
    nargs++;
  }

  function = car(head);
  args = cdr(head);

  if (symbolp(function)) {
    builtin_t bname = builtin(function->name);
    if (!builtinp(function->name)) error(NIL, PSTR("not valid here"), fname);
    checkminmax(bname, nargs);
    object *result = ((fn_ptr_type)lookupfn(bname))(args, env);
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
        object *result = eval(form, env);
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
  if (consp(function) && (isbuiltin(car(function), MACRO))) {
    // has the form (MACRO (param...) expr...)
    object *definition = cdr(function);
    object *params = car(definition);
    object *body = car(cdr(definition));
    push(unevaled_args,GCStack);
    // Serial.print("Macro: params: ");
    // printobject(params, pserial);
    // Serial.print(" - args: ");
    // printobject(args, pserial);
    // Serial.print(" - body: ");
    // printobject(body, pserial);
    // Serial.println();
    object *newenv = env;
    push(newenv, GCStack);
    object *p;
    object *a;
    for (p = params, a = unevaled_args; p != NULL; p = cdr(p), a = cdr(a)) {
      if (isbuiltin(car(p), AMPREST)) {
        push(cons(car(cdr(p)), a), newenv);
        car(GCStack) = newenv;
        break;
      } else {
        push(cons(car(p), eval(car(a), env)), newenv);
        // push(cons(car(p), car(a)), newenv);
        car(GCStack) = newenv;
      }
    }
    form = expand(body, newenv);
    pop(GCStack);
    pop(GCStack);
    goto EVAL;
  }
  error(NIL, PSTR("illegal function"), fname); return nil;
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
void pcharacter (uint8_t c, pfun_t pfun) {
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
void pstring (char *s, pfun_t pfun) {
  while (*s) pfun(*s++);
}

/*
  plispstring - prints a Lisp string object to the specified stream
*/
void plispstring (object *form, pfun_t pfun) {
  plispstr(form->name, pfun);
}

/*
  plispstr - prints a Lisp string name to the specified stream
*/
void plispstr (symbol_t name, pfun_t pfun) {
  object *form = (object *)name;
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
void printstring (object *form, pfun_t pfun) {
  if (tstflag(PRINTREADABLY)) pfun('"');
  plispstr(form->name, pfun);
  if (tstflag(PRINTREADABLY)) pfun('"');
}

/*
  pbuiltin - prints a built-in symbol to the specified stream
*/
void pbuiltin (builtin_t name, pfun_t pfun) {
  int p = 0;
  PGM_P s = (char*)pgm_read_dword(&lookup_table[name].string);
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
void printsymbol (object *form, pfun_t pfun) {
  psymbol(form->name, pfun);
}

/*
  psymbol - prints any symbol from a symbol name to the specified stream
*/
void psymbol (symbol_t name, pfun_t pfun) {
  if ((name & 0x03) == 0) plispstr(name, pfun);
  else {
    uint32_t value = untwist(name);
    if (value < PACKEDS) error2(NIL, PSTR("invalid symbol"));
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
void plist (object *form, pfun_t pfun) {
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
void pstream (object *form, pfun_t pfun) {
  pfun('<');
  pfstring(streamname[(form->integer)>>8], pfun);
  pfstring(PSTR("-stream "), pfun);
  pint(form->integer & 0xFF, pfun);
  pfun('>');
}

/*
  printobject - prints any Lisp object to the specified stream
*/
void printobject (object *form, pfun_t pfun) {
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
  else error2(NIL, PSTR("error in print"));
}

/*
  prin1object - prints any Lisp object to the specified stream escaping special characters
*/
void prin1object (object *form, pfun_t pfun) {
  uint16_t temp = Flags;
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
void loadfromlibrary (object *env) {
  GlobalStringIndex = 0;
  object *line = read(glibrary);
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
object *nextitem (gfun_t gfun) {
  int ch = gfun();
  while(issp(ch)) ch = gfun();

  while (ch == ';') {           // handle multiple comment lines
    ch = gfun();
    while (ch != '\n' && ch != -1) {
      ch = gfun();
    }
    while (issp(ch)) {
      ch = gfun();
    }
  }
  if (ch == '\n') ch = gfun();
  if (ch == -1 || ch == '\0') return nil;
  if (ch == ')') return (object *)KET;
  if (ch == '(') return (object *)BRA;
  if (ch == '\'') return (object *)QUO;
  if (ch == '`') return (object *)BACKTICK;
  if (ch == '@') return (object *)COMMAAT;
  if (ch == ',') {
    ch = gfun();
    if (ch == '@') return (object *)COMMAAT;
    else {
      LastChar = ch;
      return (object *)COMMA;
    }
  }

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
    if (ch == ' ') return (object *)DOT;
    isfloat = true;
  }

  // Parse reader macros
  else if (ch == '#') {
    ch = gfun();
    char ch2 = ch & ~0x20; // force to upper case
    if (ch == '\\') { // Character
      base = 0; ch = gfun();
      if (issp(ch) || ch == ')' || ch == '(') return character(ch);
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
      object *result = eval(read(gfun), NULL);
      clrflag(NOESC);
      return result;
    }
    else if (ch == '(') { LastChar = ch; return readarray(1, read(gfun)); }
    else if (ch == '*') return readbitarray(gfun);
    else if (ch >= '1' && ch <= '9' && (gfun() & ~0x20) == 'A') return readarray(ch - '0', read(gfun));
    else error2(NIL, PSTR("illegal character after #"));
    ch = gfun();
  }
  int valid; // 0=undecided, -1=invalid, +1=valid
  if (ch == '.') valid = 0; else if (digitvalue(ch)<base) valid = 1; else valid = -1;
  bool isexponent = false;
  int exponent = 0, esign = 1;
  buffer[2] = '\0'; buffer[3] = '\0'; buffer[4] = '\0'; buffer[5] = '\0'; // In case symbol is < 5 letters
  float divisor = 10.0;

  while(!issp(ch) && ch != ')' && ch != '(' && index < bufmax) {
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
  if (ch == ')' || ch == '(') LastChar = ch;
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
    error2(NIL, PSTR("unknown character"));
  }

  builtin_t x = lookupbuiltin(buffer);
  if (x == NIL) return nil;
  if (x != ENDFUNCTIONS) return bsymbol(x);
  object *sym;
  if ((index <= 6) && valid40(buffer)) sym = intern(twist(pack40(buffer)));
  else {
    buffer[index+1] = '\0'; buffer[index+2] = '\0'; buffer[index+3] = '\0'; // For internlong
    sym = internlong(buffer);
  }
  if (buffer[0] == ':') { // handle keywords
    if (lookupbuiltin(buffer) == ENDFUNCTIONS) // make sure it isn't a special keyword
      sym = quoteit(QUOTE, sym); // keywords quote themselves
  }
  return sym;
}

/*
  readrest - reads the remaining tokens from the specified stream
*/
object *readrest (gfun_t gfun) {
  object *item = nextitem(gfun);
  object *head = NULL;
  object *tail = NULL;

  while (item != (object *)KET) {
    if (item == (object *)BRA) {
      item = readrest(gfun);
    } else if (item == (object *)QUO) {
      item = quoteit(QUOTE, read(gfun));
    } else if (item == (object *)BACKTICK) {
      item = quoteit(QUASIQUOTE, read(gfun));
    } else if (item == (object *)COMMA) {
      item = quoteit(UNQUOTE, read(gfun));
    } else if (item == (object *)COMMAAT) {
      item = quoteit(UNQUOTESPLICING, read(gfun));
    } else if (item == (object *)DOT) {
      tail->cdr = read(gfun);
      if (readrest(gfun) != NULL) error2(NIL, PSTR("malformed list"));
      return head;
    } else {
      object *cell = cons(item, NULL);
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
object *read (gfun_t gfun) {
  object *item = nextitem(gfun);
  if (item == (object *)KET) error2(NIL, PSTR("incomplete list"));
  if (item == (object *)BRA) return readrest(gfun);
  if (item == (object *)DOT) return read(gfun);
  if (item == (object *)QUO) return quoteit(QUOTE, read(gfun));
  if (item == (object *)BACKTICK) return quoteit(QUASIQUOTE, read(gfun));
  if (item == (object *)COMMA) return quoteit(UNQUOTE, read(gfun));
  if (item == (object *)COMMAAT) return quoteit(UNQUOTESPLICING, read(gfun));
  return item;
}

// Setup

/*
  initgfx - initialises the graphics
*/
void initgfx () {
#if defined(gfxsupport)
  Wire.begin();
  tft.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  tft.fillScreen(COLOR_BLACK);
  tft.display();
#endif
}

/*
  initenv - initialises the uLisp environment
*/
void initenv () {
  GlobalEnv = NULL;
  tee = bsymbol(TEE);
}
const char SDMain[] PROGMEM = "(with-sd-card (file \"" sdmainfile "\") (loop (let ((form (read file))) (unless form (return)) (eval form))))";
int SDMainIndex = 0;

int gsdmain () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  char c = pgm_read_byte(&SDMain[SDMainIndex++]);
  return (c != 0) ? c : -1; // -1?
}

void sdmain () {
    SD.begin();
    if (initerror()) return;
    object *line = read(gsdmain);
    push(line, GCStack);
    (void)eval(line, NULL);
    pop(GCStack);
}

bool initerror () {
  if (setjmp(toplevel_errorbuffer)) return true;
  return false;
}

void ulisp_init () {
  initworkspace();
  initenv();
  initsleep();
  initgfx();
}

void setup () {
  Serial.begin(9600);
  Wire.begin();
  if (battery.begin() == false) { // Connect to the MAX17048 using the default wire port
    pfstring(PSTR("Error setting up battery gauge IC"), pserial);
    while (1);
  }
  ulisp_init();
  sdmain();
  pfstring(PSTR("\n\nuLisp 4.3 "), pserial); pln(pserial);
}

// Read/Evaluate/Print loop

/*
  repl - the Lisp Read/Evaluate/Print loop
*/
void repl (object *env) {
  for (;;) {
    randomSeed(micros());
    gc(NULL, env);
    pint(Freespace, pserial);
    if (BreakLevel) {
      pfstring(PSTR(" : "), pserial);
      pint(BreakLevel, pserial);
    }
    pserial('>'); pserial(' ');
    object *line = read(gserial);
    if (BreakLevel && line == nil) { pln(pserial); return; }
    if (line == (object *)KET) error2(NIL, PSTR("unmatched right bracket"));
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

/*
  loop - the Arduino IDE main execution loop
*/
void loop () {
  if (initerror()) {
    ; // noop
  }
  // Come here after error
  delay(100); Serial.flush();
  clrflag(NOESC); BreakLevel = 0;
  for (int i=0; i<TRACEMAX; i++) TraceDepth[i] = 0;
  SDpfile.close(); SDgfile.close();
  client.stop();
  repl(NULL);
}
