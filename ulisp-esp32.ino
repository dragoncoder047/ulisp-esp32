/* uLisp ESP Version 4.3 - www.ulisp.com
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
(foo 'bar "baz")

*/

// Lisp Library
// const char LispLibrary[] PROGMEM = "";
#include "LispLibrary.h"

// Compile options

// #define resetautorun
#define printfreespace
#define printgcs
#define sdcardsupport
#define runfromsd
// #define gfxsupport
// #define lisplibrary
// #define lineeditor
// #define vt100

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

#if defined(sdcardsupport)
  #include <SD.h>
  #define SDSIZE 172
#else
  #define SDSIZE 0
#endif

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
WRITEBYTE, WRITESTRING, WRITELINE, RESTARTI2C, GC, ROOM, SAVEIMAGE, LOADIMAGE, CLS, PINMODE, DIGITALREAD,
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
unsigned int I2CCount;
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

void errorsym2 (symbol_t fname, PGM_P string) {
  GlobalErrorString = errorsub(fname, string);
  errorend();
}

void error (builtin_t fname, PGM_P string, object *symbol) {
  errorsym(sym(fname), string, symbol);
}

void error2 (builtin_t fname, PGM_P string) {
  errorsym2(sym(fname), string);
}

void errorend () {
  if (!tstflag(MUFFLEERRORS)) pln(pserial);
  GCStack = NULL;
  longjmp(*errorbuffer, 1);
}

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
const char indexnegative[] PROGMEM = "index can't be negative";
const char invalidarg[] PROGMEM = "invalid argument";
const char invalidkey[] PROGMEM = "invalid keyword";
const char invalidpin[] PROGMEM = "invalid pin";
const char oddargs[] PROGMEM = "odd number of arguments";
const char indexrange[] PROGMEM = "index out of range";
const char canttakecar[] PROGMEM = "can't take car";
const char canttakecdr[] PROGMEM = "can't take cdr";
const char unknownstreamtype[] PROGMEM = "unknown stream type";
const char illegalclause[] PROGMEM = "illegal clause";
const char divisionbyzero[] PROGMEM = "division by zero";

// Set up workspace

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

object *myalloc () {
  if (Freespace == 0) error2(NIL, PSTR("out of memory"));
  object *temp = Freelist;
  Freelist = cdr(Freelist);
  Freespace--;
  return temp;
}

inline void myfree (object *obj) {
  car(obj) = NULL;
  cdr(obj) = Freelist;
  Freelist = obj;
  Freespace++;
}

// Make each type of object

object *number (int n) {
  object *ptr = myalloc();
  ptr->type = NUMBER;
  ptr->integer = n;
  return ptr;
}

object *makefloat (float f) {
  object *ptr = myalloc();
  ptr->type = FLOAT;
  ptr->single_float = f;
  return ptr;
}

object *character (uint8_t c) {
  object *ptr = myalloc();
  ptr->type = CHARACTER;
  ptr->chars = c;
  return ptr;
}

object *cons (object *arg1, object *arg2) {
  object *ptr = myalloc();
  ptr->car = arg1;
  ptr->cdr = arg2;
  return ptr;
}

object *symbol (symbol_t name) {
  object *ptr = myalloc();
  ptr->type = SYMBOL;
  ptr->name = name;
  return ptr;
}

inline object *bsymbol (builtin_t name) {
  return intern(twist(name+BUILTINS));
}

object *intern (symbol_t name) {
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && obj->name == name) return obj;
  }
  return symbol(name);
}

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

object *internlong (char *buffer) {
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && longsymbolp(obj) && eqsymbols(obj, buffer)) return obj;
  }
  object *obj = lispstring(buffer);
  obj->type = SYMBOL;
  return obj;
}

object *stream (uint8_t streamtype, uint8_t address) {
  object *ptr = myalloc();
  ptr->type = STREAM;
  ptr->integer = streamtype<<8 | address;
  return ptr;
}

object *newstring () {
  object *ptr = myalloc();
  ptr->type = STRING;
  ptr->chars = 0;
  return ptr;
}

// Garbage collection

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

void sweep () {
  Freelist = NULL;
  Freespace = 0;
  for (int i=WORKSPACESIZE-1; i>=0; i--) {
    object *obj = &Workspace[i];
    if (!marked(obj)) myfree(obj); else unmark(obj);
  }
}

#if defined(printgcs)
unsigned int GCCount = 0;
#endif

void gc (object *form, object *env) {
  if (tstflag(DISABLEGC)) return;
  #if defined(printgcs)
  int start = Freespace;
  #endif
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  markobject(NeverGC);
  markobject(form);
  markobject(env);
  markobject(GlobalErrorString);
  sweep();
  #if defined(printgcs)
  pfl(pserial); pfstring(PSTR("{GC"), pserial); pint(++GCCount, pserial); pfstring(PSTR(": +"), pserial); pint(Freespace - start, pserial); pserial('}'); pfl(pserial);
  //pfl(pserial); pserial('{'); pint(Freespace - start, pserial); pserial('}');
  #endif
}

// Compact image

void movepointer (object *from, object *to) {
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    unsigned int type = (obj->type) & ~MARKBIT;
    if (marked(obj) && (type >= ARRAY || type==ZZERO || (type == SYMBOL && longsymbolp(obj)))) {
      if (car(obj) == (object *)((uintptr_t)from | MARKBIT))
        car(obj) = (object *)((uintptr_t)to | MARKBIT);
      if (cdr(obj) == from) cdr(obj) = to;
    }
  }
  // Fix strings and long symbols
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (marked(obj)) {
      unsigned int type = (obj->type) & ~MARKBIT;
      if (type == STRING || (type == SYMBOL && longsymbolp(obj))) {
        obj = cdr(obj);
        while (obj != NULL) {
          if (cdr(obj) == to) cdr(obj) = from;
          obj = (object *)((uintptr_t)(car(obj)) & ~MARKBIT);
        }
      }
    }
  }
}

uintptr_t compactimage (object **arg) {
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  object *firstfree = Workspace;
  while (marked(firstfree)) firstfree++;
  object *obj = &Workspace[WORKSPACESIZE-1];
  while (firstfree < obj) {
    if (marked(obj)) {
      car(firstfree) = car(obj);
      cdr(firstfree) = cdr(obj);
      unmark(obj);
      movepointer(obj, firstfree);
      if (GlobalEnv == obj) GlobalEnv = firstfree;
      if (GCStack == obj) GCStack = firstfree;
      if (*arg == obj) *arg = firstfree;
      while (marked(firstfree)) firstfree++;
    }
    obj--;
  }
  sweep();
  return firstfree - Workspace;
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

// Save-image and load-image

#if defined(sdcardsupport)
void SDWriteInt (File file, int data) {
  file.write(data & 0xFF); file.write(data>>8 & 0xFF);
  file.write(data>>16 & 0xFF); file.write(data>>24 & 0xFF);
}

int SDReadInt (File file) {
  uintptr_t b0 = file.read(); uintptr_t b1 = file.read();
  uintptr_t b2 = file.read(); uintptr_t b3 = file.read();
  return b0 | b1<<8 | b2<<16 | b3<<24;
}
#elif defined(LITTLEFS)
void FSWrite32 (File file, uint32_t data) {
  union { uint32_t data2; uint8_t u8[4]; };
  data2 = data;
  if (file.write(u8, 4) != 4) error2(SAVEIMAGE, PSTR("not enough room"));
}

uint32_t FSRead32 (File file) {
  union { uint32_t data; uint8_t u8[4]; };
  file.read(u8, 4);
  return data;
}
#else
void EpromWriteInt(int *addr, uintptr_t data) {
  EEPROM.write((*addr)++, data & 0xFF); EEPROM.write((*addr)++, data>>8 & 0xFF);
  EEPROM.write((*addr)++, data>>16 & 0xFF); EEPROM.write((*addr)++, data>>24 & 0xFF);
}

int EpromReadInt (int *addr) {
  uint8_t b0 = EEPROM.read((*addr)++); uint8_t b1 = EEPROM.read((*addr)++);
  uint8_t b2 = EEPROM.read((*addr)++); uint8_t b3 = EEPROM.read((*addr)++);
  return b0 | b1<<8 | b2<<16 | b3<<24;
}
#endif

unsigned int saveimage (object *arg) {
  unsigned int imagesize = compactimage(&arg);
  SD.begin();
  File file;
  if (stringp(arg)) {
    char buffer[BUFFERSIZE];
    file = SD.open(MakeFilename(arg, buffer), FILE_WRITE);
    arg = NULL;
  } else if (arg == NULL || listp(arg)) file = SD.open(autorunimagepath, FILE_WRITE);
  else error(SAVEIMAGE, PSTR("illegal argument"), arg);
  if (!file) error2(SAVEIMAGE, PSTR("problem saving to SD card"));
  SDWriteInt(file, (uintptr_t)arg);
  SDWriteInt(file, imagesize);
  SDWriteInt(file, (uintptr_t)GlobalEnv);
  SDWriteInt(file, (uintptr_t)GCStack);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    SDWriteInt(file, (uintptr_t)car(obj));
    SDWriteInt(file, (uintptr_t)cdr(obj));
  }
  file.close();
  return imagesize;
}

unsigned int loadimage (object *arg) {
  SD.begin();
  File file;
  char buffer[BUFFERSIZE];
  if (stringp(arg)) file = SD.open(MakeFilename(arg, buffer));
  else if (arg == NULL) file = SD.open(autorunimagepath);
  else error(LOADIMAGE, PSTR("illegal argument"), arg);
  if (!file) error2(LOADIMAGE, PSTR("problem loading from SD card"));
  SDReadInt(file);
  unsigned int imagesize = SDReadInt(file);
  GlobalEnv = (object *)SDReadInt(file);
  GCStack = (object *)SDReadInt(file);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    car(obj) = (object *)SDReadInt(file);
    cdr(obj) = (object *)SDReadInt(file);
  }
  file.close();
  gc(NULL, NULL);
  return imagesize;
}

void autorunimage () {
  SD.begin();
  File file = SD.open(autorunimagepath);
  if (!file) error2(NIL, PSTR("problem autorunning from SD card image"));
  object *autorun = (object *)SDReadInt(file);
  file.close();
  if (autorun != NULL) {
    loadimage(NULL);
    apply(NIL, autorun, NULL, NULL);
  }
}

// Tracing

int tracing (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) return i+1;
    i++;
  }
  return 0;
}

void trace (symbol_t name) {
  if (tracing(name)) error(TRACE, PSTR("already being traced"), symbol(name));
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == 0) { TraceFn[i] = name; TraceDepth[i] = 0; return; }
    i++;
  }
  error2(TRACE, PSTR("already tracing 3 functions"));
}

void untrace (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) { TraceFn[i] = 0; return; }
    i++;
  }
  error(UNTRACE, PSTR("not tracing"), symbol(name));
}

// Helper functions

bool consp (object *x) {
  if (x == NULL) return false;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define atom(x) (!consp(x))

bool listp (object *x) {
  if (x == NULL) return true;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define improperp(x) (!listp(x))

object *quoteit (builtin_t quoter, object *it) {
  return cons(bsymbol(quoter), cons(it, nil));
}

// Radix 40 encoding

builtin_t builtin (symbol_t name) {
  return (builtin_t)(untwist(name) - BUILTINS);
}

symbol_t sym (builtin_t x) {
  return twist(x + BUILTINS);
}

int8_t toradix40 (char ch) {
  if (ch == 0) return 0;
  if (ch >= '0' && ch <= '9') return ch-'0'+1;
  if (ch == '-') return 37; if (ch == '*') return 38; if (ch == '$') return 39;
  ch = ch | 0x20;
  if (ch >= 'a' && ch <= 'z') return ch-'a'+11;
  return -1; // Invalid
}

char fromradix40 (char n) {
  if (n >= 1 && n <= 9) return '0'+n-1;
  if (n >= 11 && n <= 36) return 'a'+n-11;
  if (n == 37) return '-'; if (n == 38) return '*'; if (n == 39) return '$';
  return 0;
}

uint32_t pack40 (char *buffer) {
  int x = 0;
  for (int i=0; i<6; i++) x = x * 40 + toradix40(buffer[i]);
  return x;
}

bool valid40 (char *buffer) {
  if (toradix40(buffer[0]) < 11) return false;
  for (int i=1; i<6; i++) if (toradix40(buffer[i]) < 0) return false;
  return true;
}

int8_t digitvalue (char d) {
  if (d>='0' && d<='9') return d-'0';
  d = d | 0x20;
  if (d>='a' && d<='f') return d-'a'+10;
  return 16;
}

int checkinteger (builtin_t name, object *obj) {
  if (!integerp(obj)) error(name, notaninteger, obj);
  return obj->integer;
}

int checkbitvalue (builtin_t name, object *obj) {
  if (!integerp(obj)) error(name, notaninteger, obj);
  int n = obj->integer;
  if (n & ~1) error(name, PSTR("argument is not a bit value"), obj);
  return n;
}

float checkintfloat (builtin_t name, object *obj){
  if (integerp(obj)) return obj->integer;
  if (!floatp(obj)) error(name, notanumber, obj);
  return obj->single_float;
}

int checkchar (builtin_t name, object *obj) {
  if (!characterp(obj)) error(name, PSTR("argument is not a character"), obj);
  return obj->chars;
}

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

void checkargs (builtin_t name, object *args) {
  int nargs = listlength(name, args);
  checkminmax(name, nargs);
}

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

object *add_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult + checkintfloat(ADD, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *subtract_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult - checkintfloat(SUBTRACT, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *negate (object *arg) {
  if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(-result);
    else return number(-result);
  } else if (floatp(arg)) return makefloat(-(arg->single_float));
  else error(SUBTRACT, notanumber, arg);
  return nil;
}

object *multiply_floats (object *args, float fresult) {
  while (args != NULL) {
   object *arg = car(args);
    fresult = fresult * checkintfloat(MULTIPLY, arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

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

int myround (float number) {
  return (number >= 0) ? (int)(number + 0.5) : (int)(number - 0.5);
}

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

int nextpower2 (int n) {
  n--; n |= n >> 1; n |= n >> 2; n |= n >> 4;
  n |= n >> 8; n |= n >> 16; n++;
  return n<2 ? 2 : n;
}

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

object **arrayref (object *array, int index, int size) {
  int mask = nextpower2(size)>>1;
  object **p = &car(cdr(array));
  while (mask) {
    if ((index & mask) == 0) p = &(car(*p)); else p = &(cdr(*p));
    mask = mask>>1;
  }
  return p;
}

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
        (index & (sizeof(int)==4 ? 0x1F : 0x0F)) & 1, pfun);      else printobject(*arrayref(array, index, size), pfun);
    } else { pfun('('); pslice(array, size, index, cdr(dims), pfun, bitp); pfun(')'); }
  }
}

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

void pstr (char c) {
  buildstring(c, &GlobalStringTail);
}

object *lispstring (char *s) {
  object *obj = newstring();
  object *tail = obj;
  while (true) {
    char ch = *s++;
    if (ch == 0) break;
    if (ch == '\\') ch = *s++;
    buildstring(ch, &tail);
  }
  return obj;
}

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

object *findpair (object *var, object *env) {
  symbol_t name = var->name;
  object *pair = value(name, env);
  if (pair == NULL) pair = value(name, GlobalEnv);
  return pair;
}

bool boundp (object *var, object *env) {
  symbol_t varname = var->name;
  if (value(varname, env) != NULL) return true;
  if (value(varname, GlobalEnv) != NULL) return true;
  return false;
}

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

object *carx (object *arg) {
  if (!listp(arg)) error(NIL, canttakecar, arg);
  if (arg == nil) return nil;
  return car(arg);
}

object *cdrx (object *arg) {
  if (!listp(arg)) error(NIL, canttakecdr, arg);
  if (arg == nil) return nil;
  return cdr(arg);
}

object *cxxxr (object *args, uint8_t pattern) {
  object *arg = first(args);
  while (pattern != 1) {
    if ((pattern & 1) == 0) arg = carx(arg); else arg = cdrx(arg);
    pattern = pattern>>1;
  }
  return arg;
}

// Mapping helper functions

void mapcarfun (object *result, object **tail) {
  object *obj = cons(result,NULL);
  cdr(*tail) = obj; *tail = obj;
}

void mapcanfun (object *result, object **tail) {
  if (cdr(*tail) != NULL) error(MAPCAN, notproper, *tail);
  while (consp(result)) {
    cdr(*tail) = result; *tail = result;
    result = cdr(result);
  }
}

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

// I2C interface

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
 else Wire.requestFrom(address, I2CCount);
 return ok;
}

bool I2Crestart (uint8_t address, uint8_t read) {
  int error = (Wire.endTransmission(false) != 0);
  if (read == 0) Wire.beginTransmission(address);
  else Wire.requestFrom(address, I2CCount);
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
  #if defined(sdcardsupport)
  else if (streamtype == SDSTREAM) gfun = (gfun_t)SDread;
  #endif
  else if (streamtype == WIFISTREAM) gfun = (gfun_t)WiFiread;
  else error2(NIL, PSTR("unknown stream type"));
  return gfun;
}

inline void spiwrite (char c) { SPI.transfer(c); }
inline void serial1write (char c) { Serial1.write(c); }
inline void WiFiwrite (char c) { client.write(c); }
#if defined(sdcardsupport)
inline void SDwrite (char c) { SDpfile.write(c); }
#endif
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
  #if defined(sdcardsupport)
  else if (streamtype == SDSTREAM) pfun = (pfun_t)SDwrite;
  #endif
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

object *sp_or (object *args, object *env) {
  while (args != NULL) {
    object *val = eval(car(args), env);
    if (val != NULL) return val;
    args = cdr(args);
  }
  return nil;
}

object *sp_defun (object *args, object *env) {
  (void) env;
  checkargs(DEFUN, args);
  object *var = first(args);
  if (!symbolp(var)) error(DEFUN, notasymbol, var);
  object *val = cons(bsymbol(LAMBDA), cdr(args));
  object *pair = value(var->name,GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

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

object *sp_return (object *args, object *env) {
  object *result = eval(tf_progn(args,env), env);
  setflag(RETURNFLAG);
  return result;
}

object *sp_push (object *args, object *env) {
  int bit;
  checkargs(PUSH, args); 
  object *item = eval(first(args), env);
  object **loc = place(PUSH, second(args), env, &bit);
  push(item, *loc);
  return *loc;
}

object *sp_pop (object *args, object *env) {
  int bit;
  checkargs(POP, args); 
  object **loc = place(POP, first(args), env, &bit);
  object *result = car(*loc);
  pop(*loc);
  return result;
}

// Accessors

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

object *sp_withi2c (object *args, object *env) {
  object *params = first(args);
  if (params == NULL) error2(WITHI2C, nostream);
  object *var = first(params);
  int address = checkinteger(WITHI2C, eval(second(params), env));
  params = cddr(params);
  if (address == 0 && params != NULL) params = cdr(params); // Ignore port
  int read = 0; // Write
  I2CCount = 0;
  if (params != NULL) {
    object *rw = eval(first(params), env);
    if (integerp(rw)) I2CCount = rw->integer;
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
      object *result = eval(car(processed), env);
      // Serial.print("**** Result: ");
      // printobject(result, pserial);
      // Serial.println();
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
      // Serial.print("**** Processed: ");
      // printobject(car(processed), pserial);
      // Serial.println();
      object *result = eval(car(processed), env);
      // Serial.print("**** Result: ");
      // printobject(result, pserial);
      // Serial.println();
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

object *sp_quasiquote (object *args, object *env) {
  checkargs(QUASIQUOTE, args);
  object *result = process_quasiquoted(car(args), 1, env);
  return car(result);
}

object *sp_unquote (object *args, object *env) {
  (void)args, (void)env;
  error2(UNQUOTE, PSTR("not supported outside a quasiquote form"));
  return nil; // unreachable, just to satisfy compiler
}

object *sp_unquote_splicing (object *args, object *env) {
  (void)args, (void)env;
  error2(UNQUOTESPLICING, PSTR("not supported outside a quasiquote form"));
  return nil; // unreachable, just to satisfy compiler
}

object *expand (object *body, object *env) {
  return eval(body, env);
}

//MOVE arg evaluation to sp_expand
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

object *tf_if (object *args, object *env) {
  if (args == NULL || cdr(args) == NULL) error2(IF, toofewargs);
  if (eval(first(args), env) != nil) return second(args);
  args = cddr(args);
  return (args != NULL) ? first(args) : nil;
}

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

object *tf_when (object *args, object *env) {
  if (args == NULL) error2(WHEN, noargument);
  if (eval(first(args), env) != nil) return tf_progn(cdr(args),env);
  else return nil;
}

object *tf_unless (object *args, object *env) {
  if (args == NULL) error2(UNLESS, noargument);
  if (eval(first(args), env) != nil) return nil;
  else return tf_progn(cdr(args),env);
}

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

object *fn_geterror (object *args, object *env) {
  (void)args, (void)env;
  return GlobalErrorString;
}

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

object *fn_intern (object *args, object *env) {
  checkargs(INTERN, args);
  if (!stringp(car(args))) error(INTERN, notastring, car(args));
  char b[BUFFERSIZE];
  char *cs = cstring(car(args), b, BUFFERSIZE);
  return internlong(cs);
}

object *fn_not (object *args, object *env) {
  (void) env;
  return (first(args) == nil) ? tee : nil;
}

object *fn_cons (object *args, object *env) {
  (void) env;
  return cons(first(args), second(args));
}

object *fn_atom (object *args, object *env) {
  (void) env;
  return atom(first(args)) ? tee : nil;
}

object *fn_listp (object *args, object *env) {
  (void) env;
  return listp(first(args)) ? tee : nil;
}

object *fn_consp (object *args, object *env) {
  (void) env;
  return consp(first(args)) ? tee : nil;
}

object *fn_symbolp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (arg == NULL || symbolp(arg)) ? tee : nil;
}

object *fn_arrayp (object *args, object *env) {
  (void) env;
  return arrayp(first(args)) ? tee : nil;
}

object *fn_boundp (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(BOUNDP, notasymbol, var);
  return boundp(var, env) ? tee : nil;
}

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

object *fn_streamp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return streamp(arg) ? tee : nil;
}

object *fn_eq (object *args, object *env) {
  (void) env;
  return eq(first(args), second(args)) ? tee : nil;
}

object *fn_equal (object *args, object *env) {
  (void) env;
  return equal(first(args), second(args)) ? tee : nil;
}

// List functions

object *fn_car (object *args, object *env) {
  (void) env;
  return carx(first(args));
}

object *fn_cdr (object *args, object *env) {
  (void) env;
  return cdrx(first(args));
}

object *fn_caar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b100);
}

object *fn_cadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b101);
}

object *fn_cdar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b110);
}

object *fn_cddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b111);
}

object *fn_caaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1000);
}

object *fn_caadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1001);;
}

object *fn_cadar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1010);
}

object *fn_caddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1011);
}

object *fn_cdaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1100);
}

object *fn_cdadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1101);
}

object *fn_cddar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1110);
}

object *fn_cdddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1111);
}

object *fn_length (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (listp(arg)) return number(listlength(LENGTH, arg));
  if (stringp(arg)) return number(stringlength(arg));
  if (!(arrayp(arg) && cdr(cddr(arg)) == NULL)) error(LENGTH, PSTR("argument is not a list, 1d array, or string"), arg);
  return number(abs(first(cddr(arg))->integer));
}

object *fn_arraydimensions (object *args, object *env) {
  (void) env;
  object *array = first(args);
  if (!arrayp(array)) error(ARRAYDIMENSIONS, PSTR("argument is not an array"), array);
  object *dimensions = cddr(array);
  return (first(dimensions)->integer < 0) ? cons(number(-(first(dimensions)->integer)), cdr(dimensions)) : dimensions;
}

object *fn_list (object *args, object *env) {
  (void) env;
  return args;
}

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

object *fn_aref (object *args, object *env) {
  (void) env;
  int bit;
  object *array = first(args);
  if (!arrayp(array)) error(AREF, PSTR("first argument is not an array"), array);
  object *loc = *getarray(AREF, array, cdr(args), 0, &bit);
  if (bit == -1) return loc;
  else return number((loc->integer)>>bit & 1);
}

object *fn_assoc (object *args, object *env) {
  (void) env;
  object *key = first(args);
  object *list = second(args);
  return assoc(key,list);
}

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

object *fn_funcall (object *args, object *env) {
  return apply(FUNCALL, first(args), cdr(args), env);
}

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

object *fn_mapcar (object *args, object *env) {
  return mapcarcan(MAPCAR, args, env, mapcarfun);
}

object *fn_mapcan (object *args, object *env) {
  return mapcarcan(MAPCAN, args, env, mapcanfun);
}

// Arithmetic functions

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

object *fn_random (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (integerp(arg)) return number(random(arg->integer));
  else if (floatp(arg)) return makefloat((float)rand()/(float)(RAND_MAX/(arg->single_float)));
  else error(RANDOM, notanumber, arg);
  return nil;
}

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

object *fn_numeq (object *args, object *env) {
  (void) env;
  return compare(NUMEQ, args, false, false, true);
}

object *fn_less (object *args, object *env) {
  (void) env;
  return compare(LESS, args, true, false, false);
}

object *fn_lesseq (object *args, object *env) {
  (void) env;
  return compare(LESSEQ, args, true, false, true);
}

object *fn_greater (object *args, object *env) {
  (void) env;
  return compare(GREATER, args, false, true, false);
}

object *fn_greatereq (object *args, object *env) {
  (void) env;
  return compare(GREATEREQ, args, false, true, true);
}

object *fn_plusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) > 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) > 0) ? tee : nil;
  else error(PLUSP, notanumber, arg);
  return nil;
}

object *fn_minusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) < 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) < 0) ? tee : nil;
  else error(MINUSP, notanumber, arg);
  return nil;
}

object *fn_zerop (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) == 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) == 0) ? tee : nil;
  else error(ZEROP, notanumber, arg);
  return nil;
}

object *fn_oddp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(ODDP, first(args));
  return ((arg & 1) == 1) ? tee : nil;
}

object *fn_evenp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(EVENP, first(args));
  return ((arg & 1) == 0) ? tee : nil;
}

// Number functions

object *fn_integerp (object *args, object *env) {
  (void) env;
  return integerp(first(args)) ? tee : nil;
}

object *fn_numberp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (integerp(arg) || floatp(arg)) ? tee : nil;
}

// Floating-point functions

object *fn_floatfn (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (floatp(arg)) ? arg : makefloat((float)(arg->integer));
}

object *fn_floatp (object *args, object *env) {
  (void) env;
  return floatp(first(args)) ? tee : nil;
}

object *fn_sin (object *args, object *env) {
  (void) env;
  return makefloat(sin(checkintfloat(SIN, first(args))));
}

object *fn_cos (object *args, object *env) {
  (void) env;
  return makefloat(cos(checkintfloat(COS, first(args))));
}

object *fn_tan (object *args, object *env) {
  (void) env;
  return makefloat(tan(checkintfloat(TAN, first(args))));
}

object *fn_asin (object *args, object *env) {
  (void) env;
  return makefloat(asin(checkintfloat(ASIN, first(args))));
}

object *fn_acos (object *args, object *env) {
  (void) env;
  return makefloat(acos(checkintfloat(ACOS, first(args))));
}

object *fn_atan (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float div = 1.0;
  args = cdr(args);
  if (args != NULL) div = checkintfloat(ATAN, first(args));
  return makefloat(atan2(checkintfloat(ATAN, arg), div));
}

object *fn_sinh (object *args, object *env) {
  (void) env;
  return makefloat(sinh(checkintfloat(SINH, first(args))));
}

object *fn_cosh (object *args, object *env) {
  (void) env;
  return makefloat(cosh(checkintfloat(COSH, first(args))));
}

object *fn_tanh (object *args, object *env) {
  (void) env;
  return makefloat(tanh(checkintfloat(TANH, first(args))));
}

object *fn_exp (object *args, object *env) {
  (void) env;
  return makefloat(exp(checkintfloat(EXP, first(args))));
}

object *fn_sqrt (object *args, object *env) {
  (void) env;
  return makefloat(sqrt(checkintfloat(SQRT, first(args))));
}

object *fn_log (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float fresult = log(checkintfloat(LOG, arg));
  args = cdr(args);
  if (args == NULL) return makefloat(fresult);
  else return makefloat(fresult / log(checkintfloat(LOG, first(args))));
}

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

object *fn_ceiling (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(ceil(checkintfloat(CEILING, arg) / checkintfloat(CEILING, first(args))));
  else return number(ceil(checkintfloat(CEILING, arg)));
}

object *fn_floor (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(floor(checkintfloat(FLOOR, arg) / checkintfloat(FLOOR, first(args))));
  else return number(floor(checkintfloat(FLOOR, arg)));
}

object *fn_truncate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number((int)(checkintfloat(TRUNCATE, arg) / checkintfloat(TRUNCATE, first(args))));
  else return number((int)(checkintfloat(TRUNCATE, arg)));
}

int myround (float number) {
  return (number >= 0) ? (int)(number + 0.5) : (int)(number - 0.5);
}

object *fn_round (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(myround(checkintfloat(ROUND, arg) / checkintfloat(ROUND, first(args))));
  else return number(myround(checkintfloat(ROUND, arg)));
}

// Characters

object *fn_char (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!stringp(arg)) error(CHAR, notastring, arg);
  object *n = second(args);
  char c = nthchar(arg, checkinteger(CHAR, n));
  if (c == 0) error(CHAR, indexrange, n);
  return character(c);
}

object *fn_charcode (object *args, object *env) {
  (void) env;
  return number(checkchar(CHARCODE, first(args)));
}

object *fn_codechar (object *args, object *env) {
  (void) env;
  return character(checkinteger(CODECHAR, first(args)));
}

object *fn_characterp (object *args, object *env) {
  (void) env;
  return characterp(first(args)) ? tee : nil;
}

// Strings

object *fn_stringp (object *args, object *env) {
  (void) env;
  return stringp(first(args)) ? tee : nil;
}

object *fn_stringeq (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGEQ, args, false, false, true) ? tee : nil;
}

object *fn_stringless (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGLESS, args, true, false, false) ? tee : nil;
}

object *fn_stringgreater (object *args, object *env) {
  (void) env;
  return stringcompare(STRINGGREATER, args, false, true, false) ? tee : nil;
}

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

object *fn_stringfn (object *args, object *env) {
  return fn_princtostring(args, env);
}

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

object *fn_readfromstring (object *args, object *env) {
  (void) env;
  object *arg = checkstring(READFROMSTRING, first(args));
  GlobalString = arg;
  GlobalStringIndex = 0;
  return read(gstr);
}

object *fn_princtostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring(PRINCTOSTRING);
  prin1object(arg, pstr);
  return obj;
}

object *fn_prin1tostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring(PRIN1TOSTRING);
  printobject(arg, pstr);
  return obj;
}

// Bitwise operators

object *fn_logand (object *args, object *env) {
  (void) env;
  int result = -1;
  while (args != NULL) {
    result = result & checkinteger(LOGAND, first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_logior (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result | checkinteger(LOGIOR, first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_logxor (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result ^ checkinteger(LOGXOR, first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_lognot (object *args, object *env) {
  (void) env;
  int result = checkinteger(LOGNOT, car(args));
  return number(~result);
}

object *fn_ash (object *args, object *env) {
  (void) env;
  int value = checkinteger(ASH, first(args));
  int count = checkinteger(ASH, second(args));
  if (count >= 0) return number(value << count);
  else return number(value >> abs(count));
}

object *fn_logbitp (object *args, object *env) {
  (void) env;
  int index = checkinteger(LOGBITP, first(args));
  int value = checkinteger(LOGBITP, second(args));
  return (bitRead(value, index) == 1) ? tee : nil;
}

// System functions

object *fn_eval (object *args, object *env) {
  return eval(first(args), env);
}

object *fn_globals (object *args, object *env) {
  (void) args;
  if (GlobalEnv == NULL) return nil;
  return fn_mapcar(cons(bsymbol(CAR),cons(GlobalEnv,nil)), env);
}

object *fn_locals (object *args, object *env) {
  (void) args;
  return env;
}

object *fn_makunbound (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(MAKUNBOUND, notasymbol, var);
  delassoc(var, &GlobalEnv);
  return var;
}

object *fn_break (object *args, object *env) {
  (void) args;
  pfstring(PSTR("\nBreak!\n"), pserial);
  BreakLevel++;
  repl(env);
  BreakLevel--;
  return nil;
}

object *fn_read (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return read(gfun);
}

object *fn_prin1 (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  printobject(obj, pfun);
  return obj;
}

object *fn_print (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  pln(pfun);
  printobject(obj, pfun);
  pfun(' ');
  return obj;
}

object *fn_princ (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  prin1object(obj, pfun);
  return obj;
}

object *fn_terpri (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  pln(pfun);
  return nil;
}

object *fn_readbyte (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  int c = gfun();
  return (c == -1) ? nil : number(c);
}

object *fn_readline (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return readstring('\n', gfun);
}

object *fn_writebyte (object *args, object *env) {
  (void) env;
  int value = checkinteger(WRITEBYTE, first(args));
  pfun_t pfun = pstreamfun(cdr(args));
  (pfun)(value);
  return nil;
}

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

object *fn_restarti2c (object *args, object *env) {
  (void) env;
  int stream = first(args)->integer;
  args = cdr(args);
  int read = 0; // Write
  I2CCount = 0;
  if (args != NULL) {
    object *rw = first(args);
    if (integerp(rw)) I2CCount = rw->integer;
    read = (rw != NULL);
  }
  int address = stream & 0xFF;
  if (stream>>8 != I2CSTREAM) error2(RESTARTI2C, PSTR("not an i2c stream"));
  return I2Crestart(address, read) ? tee : nil;
}

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

object *fn_room (object *args, object *env) {
  (void) args, (void) env;
  return number(Freespace);
}

object *fn_saveimage (object *args, object *env) {
  if (args != NULL) args = eval(first(args), env);
  return number(saveimage(args));
}

object *fn_loadimage (object *args, object *env) {
  (void) env;
  if (args != NULL) args = first(args);
  return number(loadimage(args));
}

object *fn_cls (object *args, object *env) {
  (void) args, (void) env;
  pserial(12);
  return nil;
}

// Arduino procedures

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

object *fn_digitalread (object *args, object *env) {
  (void) env;
  int pin = checkinteger(DIGITALREAD, first(args));
  if (digitalRead(pin) != 0) return tee; else return nil;
}

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

object *fn_analogreadresolution (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  analogReadResolution(checkinteger(ANALOGREADRESOLUTION, arg));
  return arg;
}

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

object *fn_delay (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  delay(checkinteger(DELAY, arg1));
  return arg1;
}

object *fn_millis (object *args, object *env) {
  (void) args, (void) env;
  return number(millis());
}

object *fn_sleep (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  sleep(checkinteger(SLEEP, arg1));
  return arg1;
}

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

object *fn_edit (object *args, object *env) {
  object *fun = first(args);
  object *pair = findvalue(EDIT, fun, env);
  clrflag(EXITEDITOR);
  object *arg = edit(eval(fun, env));
  cdr(pair) = arg;
  return arg;
}

// Pretty printer

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

object *fn_documentation (object *args, object *env) {
  return documentation(DOCUMENTATION, first(args), env);
}

// Wi-fi

object *fn_available (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2(AVAILABLE, PSTR("invalid stream"));
  return number(client.available());
}

object *fn_wifiserver (object *args, object *env) {
  (void) args, (void) env;
  server.begin();
  return nil;
}

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

object *fn_connected (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2(CONNECTED, PSTR("invalid stream"));
  return client.connected() ? tee : nil;
}

object *fn_wifilocalip (object *args, object *env) {
  (void) args, (void) env;
  return lispstring((char*)WiFi.localIP().toString().c_str());
}

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

object *fn_setcursor (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setCursor(checkinteger(SETCURSOR, first(args)), checkinteger(SETCURSOR, second(args)));
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

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

object *fn_settextsize (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setTextSize(checkinteger(SETTEXTSIZE, first(args)));
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

object *fn_settextwrap (object *args, object *env) {
  #if defined(gfxsupport)
  (void) env;
  tft.setTextWrap(first(args) != NULL);
  #else
  (void) args, (void) env;
  #endif
  return nil;
}

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
#define STRINGER(x, s) const char string_##x[] PROGMEM = s;
STRINGER(nil, "nil")
STRINGER(tee, "t")
STRINGER(nothing, "nothing")
STRINGER(andoptional, "&optional")
STRINGER(initialelement, ":initial-element")
STRINGER(elementtype, ":element-type")
STRINGER(bit, "bit")
STRINGER(andrest, "&rest")
STRINGER(lambda, "lambda")
STRINGER(let, "let")
STRINGER(letstar, "let*")
STRINGER(closure, "closure")
STRINGER(starpcstar, "*pc*")
STRINGER(SPECIALFORMS, "")
STRINGER(quote, "quote")
STRINGER(or, "or")
STRINGER(defun, "defun")
STRINGER(defvar, "defvar")
STRINGER(setq, "setq")
STRINGER(loop, "loop")
STRINGER(return, "return")
STRINGER(push, "push")
STRINGER(pop, "pop")
STRINGER(incf, "incf")
STRINGER(decf, "decf")
STRINGER(setf, "setf")
STRINGER(dolist, "dolist")
STRINGER(dotimes, "dotimes")
STRINGER(trace, "trace")
STRINGER(untrace, "untrace")
STRINGER(formillis, "for-millis")
STRINGER(time, "time")
STRINGER(withoutputtostring, "with-output-to-string")
STRINGER(withserial, "with-serial")
STRINGER(withi2c, "with-i2c")
STRINGER(withspi, "with-spi")
STRINGER(withsdcard, "with-sd-card")
STRINGER(withgfx, "with-gfx")
STRINGER(withclient, "with-client")
STRINGER(unwindprotect, "unwind-protect")
STRINGER(ignoreerrors, "ignore-errors")
//STRINGER(error, "error")
STRINGER(MACRO_SENTINEL, "macro")
STRINGER(defmacro, "defmacro")
STRINGER(quasiquote, "quasiquote")
STRINGER(unquote, "unquote")
STRINGER(unquotesplicing, "unquote-splicing")
STRINGER(expand, "expand")
STRINGER(TAILFORMS, "")
STRINGER(progn, "progn")
STRINGER(if, "if")
STRINGER(cond, "cond")
STRINGER(when, "when")
STRINGER(unless, "unless")
STRINGER(case, "case")
STRINGER(and, "and")
STRINGER(documentation, "?")
STRINGER(FUNCTIONS, "")
STRINGER(geterror, "get-error")
STRINGER(throw, "throw")
STRINGER(intern, "intern")
STRINGER(not, "not")
STRINGER(null, "null")
STRINGER(cons, "cons")
STRINGER(atom, "atom")
STRINGER(listp, "listp")
STRINGER(consp, "consp")
STRINGER(symbolp, "symbolp")
STRINGER(arrayp, "arrayp")
STRINGER(boundp, "boundp")
STRINGER(set, "set")
STRINGER(streamp, "streamp")
STRINGER(eq, "eq")
STRINGER(equal, "equal")
STRINGER(car, "car")
STRINGER(first, "first")
STRINGER(cdr, "cdr")
STRINGER(rest, "rest")
STRINGER(caar, "caar")
STRINGER(cadr, "cadr")
STRINGER(second, "second")
STRINGER(cdar, "cdar")
STRINGER(cddr, "cddr")
STRINGER(caaar, "caaar")
STRINGER(caadr, "caadr")
STRINGER(cadar, "cadar")
STRINGER(caddr, "caddr")
STRINGER(third, "third")
STRINGER(cdaar, "cdaar")
STRINGER(cdadr, "cdadr")
STRINGER(cddar, "cddar")
STRINGER(cdddr, "cdddr")
STRINGER(length, "length")
STRINGER(arraydimensions, "array-dimensions")
STRINGER(list_, "list")
STRINGER(makearray, "make-array")
STRINGER(reverse, "reverse")
STRINGER(nth, "nth")
STRINGER(aref, "aref")
STRINGER(assoc, "assoc")
STRINGER(member, "member")
STRINGER(apply, "apply")
STRINGER(funcall, "funcall")
STRINGER(append, "append")
STRINGER(mapc, "mapc")
STRINGER(mapcar, "mapcar")
STRINGER(mapcan, "mapcan")
STRINGER(plus, "+")
STRINGER(minus, "-")
STRINGER(times, "*")
STRINGER(divide, "/")
STRINGER(mod, "mod")
STRINGER(oneplus, "1+")
STRINGER(oneminus, "1-")
STRINGER(abs, "abs")
STRINGER(random, "random")
STRINGER(max, "max")
STRINGER(min, "min")
STRINGER(noteq, "/=")
STRINGER(numeq, "=")
STRINGER(less, "<")
STRINGER(lesseq, "<=")
STRINGER(greater, ">")
STRINGER(greatereq, ">=")
STRINGER(plusp, "plusp")
STRINGER(minusp, "minusp")
STRINGER(zerop, "zerop")
STRINGER(oddp, "oddp")
STRINGER(evenp, "evenp")
STRINGER(integerp, "integerp")
STRINGER(numberp, "numberp")
STRINGER(float, "float")
STRINGER(floatp, "floatp")
STRINGER(sin, "sin")
STRINGER(cos, "cos")
STRINGER(tan, "tan")
STRINGER(asin, "asin")
STRINGER(acos, "acos")
STRINGER(atan, "atan")
STRINGER(sinh, "sinh")
STRINGER(cosh, "cosh")
STRINGER(tanh, "tanh")
STRINGER(exp, "exp")
STRINGER(sqrt, "sqrt")
STRINGER(log, "log")
STRINGER(expt, "expt")
STRINGER(ceiling, "ceiling")
STRINGER(floor, "floor")
STRINGER(truncate, "truncate")
STRINGER(round, "round")
STRINGER(char, "char")
STRINGER(charcode, "char-code")
STRINGER(codechar, "code-char")
STRINGER(characterp, "characterp")
STRINGER(stringp, "stringp")
STRINGER(stringeq, "string=")
STRINGER(stringlt, "string<")
STRINGER(stringgt, "string>")
STRINGER(sort, "sort")
STRINGER(string, "string")
STRINGER(concatenate, "concatenate")
STRINGER(subseq, "subseq")
STRINGER(readfromstring, "read-from-string")
STRINGER(princtostring, "princ-to-string")
STRINGER(prin1tostring, "prin1-to-string")
STRINGER(logand, "logand")
STRINGER(logior, "logior")
STRINGER(logxor, "logxor")
STRINGER(lognot, "lognot")
STRINGER(ash, "ash")
STRINGER(logbitp, "logbitp")
STRINGER(eval, "eval")
STRINGER(globals, "globals")
STRINGER(locals, "locals")
STRINGER(makunbound, "makunbound")
STRINGER(break, "break")
STRINGER(read, "read")
STRINGER(prin1, "prin1")
STRINGER(print, "print")
STRINGER(princ, "princ")
STRINGER(terpri, "terpri")
STRINGER(readbyte, "read-byte")
STRINGER(readline, "read-line")
STRINGER(writebyte, "write-byte")
STRINGER(writestring, "write-string")
STRINGER(writeline, "write-line")
STRINGER(restarti2c, "restart-i2c")
STRINGER(gc, "gc")
STRINGER(room, "room")
STRINGER(saveimage, "save-image")
STRINGER(loadimage, "load-image")
STRINGER(cls, "cls")
STRINGER(pinmode, "pinmode")
STRINGER(digitalread, "digitalread")
STRINGER(digitalwrite, "digitalwrite")
STRINGER(analogread, "analogread")
STRINGER(analogreadresolution, "analogreadresolution")
STRINGER(analogwrite, "analogwrite")
STRINGER(delay, "delay")
STRINGER(millis, "millis")
STRINGER(sleep, "sleep")
STRINGER(note, "note")
STRINGER(register, "register")
STRINGER(edit, "edit")
STRINGER(pprint, "pprint")
STRINGER(pprintall, "pprintall")
STRINGER(format, "format")
STRINGER(require, "require")
STRINGER(listlibrary, "list-library")
STRINGER(available, "available")
STRINGER(wifiserver, "wifi-server")
STRINGER(wifisoftap, "wifi-softap")
STRINGER(connected, "connected")
STRINGER(wifilocalip, "wifi-localip")
STRINGER(wificonnect, "wifi-connect")
STRINGER(drawpixel, "draw-pixel")
STRINGER(drawline, "draw-line")
STRINGER(drawrect, "draw-rect")
STRINGER(fillrect, "fill-rect")
STRINGER(drawcircle, "draw-circle")
STRINGER(fillcircle, "fill-circle")
STRINGER(drawroundrect, "draw-round-rect")
STRINGER(fillroundrect, "fill-round-rect")
STRINGER(drawtriangle, "draw-triangle")
STRINGER(filltriangle, "fill-triangle")
STRINGER(drawchar, "draw-char")
STRINGER(setcursor, "set-cursor")
STRINGER(settextcolor, "set-text-color")
STRINGER(settextsize, "set-text-size")
STRINGER(settextwrap, "set-text-wrap")
STRINGER(fillscreen, "fill-screen")
STRINGER(setrotation, "set-rotation")
STRINGER(invertdisplay, "invert-display")
STRINGER(SPECIALKEYWORDS, "")
STRINGER(ledbuiltin, ":led-builtin")
STRINGER(high, ":high")
STRINGER(low, ":low")
STRINGER(input, ":input")
STRINGER(inputpullup, ":input-pullup")
STRINGER(inputpulldown, ":input-pulldown")
STRINGER(output, ":output")
STRINGER(USERFUNCTIONS, "")

// Insert your own function names here
STRINGER(batteryvoltage, "battery:voltage")
STRINGER(batterypercentage, "battery:percentage")
STRINGER(batterychangerate, "battery:change-rate")

// Documentation strings
/*
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
const char doc176[] PROGMEM = "(save-image [symbol])\n"
"Saves the current uLisp image to non-volatile memory or SD card so it can be loaded using load-image.";
const char doc177[] PROGMEM = "(load-image [filename])\n"
"Loads a saved uLisp image from non-volatile memory or SD card.";
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
*/

//Insert your own docstrings here:

// Built-in symbol lookup table
const tbl_entry_t lookup_table[] PROGMEM = {
  { string_nil, NULL, 0x00, NULL },
  { string_tee, NULL, 0x00, NULL },
  { string_nothing, NULL, 0x00,, NULL },
  { string_andoptional, NULL, 0x00, NULL },
  { string_initialelement, NULL, 0x00, NULL },
  { string_elementtype, NULL, 0x00, NULL },
  { string_bit, NULL, 0x00, NULL },
  { string_andrest, NULL, 0x00, NULL },
  { string_lambda, NULL, 0x0F, NULL },
  { string_let, NULL, 0x0F, NULL },
  { string_letstar, NULL, 0x0F, NULL },
  { string_closure, NULL, 0x0F, NULL },
  { string_starpcstar, NULL, 0x0F, NULL },
  { string_SPECIALFORMS, NULL, 0x00, NULL },
  { string_quote, sp_quote, 0x11, NULL },
  { string_or, sp_or, 0x0F, NULL },
  { string_defun, sp_defun, 0x2F, NULL },
  { string_defvar, sp_defvar, 0x13, NULL },
  { string_setq, sp_setq, 0x2F, NULL },
  { string_loop, sp_loop, 0x0F, NULL },
  { string_return, sp_return, 0x0F, NULL },
  { string_push, sp_push, 0x22, NULL },
  { string_pop, sp_pop, 0x11, NULL },
  { string_incf, sp_incf, 0x12, NULL },
  { string_decf, sp_decf, 0x12, NULL },
  { string_setf, sp_setf, 0x2F, NULL },
  { string_dolist, sp_dolist, 0x1F, NULL },
  { string_dotimes, sp_dotimes, 0x1F, NULL },
  { string_trace, sp_trace, 0x01, NULL },
  { string_untrace, sp_untrace, 0x01, NULL },
  { string_formillis, sp_formillis, 0x1F, NULL },
  { string_time, sp_time, 0x11, NULL },
  { string_withoutputtostring, sp_withoutputtostring, 0x1F, NULL },
  { string_withserial, sp_withserial, 0x1F, NULL },
  { string_withi2c, sp_withi2c, 0x1F, NULL },
  { string_withspi, sp_withspi, 0x1F, NULL },
  { string_withsdcard, sp_withsdcard, 0x2F, NULL },
  { string_withgfx, sp_withgfx, 0x1F, NULL },
  { string_withclient, sp_withclient, 0x12, NULL },
  { string_unwindprotect, sp_unwindprotect, 0x1F, NULL },
  { string_ignoreerrors, sp_ignoreerrors, 0x0F, NULL },
//  { string_error, sp_error, 0x1F, NULL },
  { string_MACRO_SENTINEL, NULL, 0x00, NULL },
  { string_defmacro, sp_defmacro, 0x0F, NULL },
  { string_quasiquote, sp_quasiquote, 0x11, NULL },
  { string_unquote, sp_unquote, 0x1, NULL },
  { string_unquotesplicing, sp_unquote_splicing, 0x11, NULL },
  { string_expand, sp_expand, 0x1F, NULL },
  { string_TAILFORMS, NULL, 0x00, NULL },
  { string_progn, tf_progn, 0x0F, NULL },
  { string_if, tf_if, 0x23, NULL },
  { string_cond, tf_cond, 0x0F, NULL },
  { string_when, tf_when, 0x1F, NULL },
  { string_unless, tf_unless, 0x1F, NULL },
  { string_case, tf_case, 0x1F, NULL },
  { string_and, tf_and, 0x0F, NULL },
  { string_FUNCTIONS, NULL, 0x00, NULL },
  { string_geterror, fn_geterror, 0x00, NULL },
  { string_throw, fn_throw, 0x11, NULL },
  { string_intern, fn_intern, 0x11, NULL },
  { string_not, fn_not, 0x11, NULL },
  { string_null, fn_not, 0x11, NULL },
  { string_cons, fn_cons, 0x22, NULL },
  { string_atom, fn_atom, 0x11, NULL },
  { string_listp, fn_listp, 0x11, NULL },
  { string_consp, fn_consp, 0x11, NULL },
  { string_symbolp, fn_symbolp, 0x11, NULL },
  { string_arrayp, fn_arrayp, 0x11, NULL },
  { string_boundp, fn_boundp, 0x11, NULL },
  { string_set, fn_setfn, 0x2F, NULL },
  { string_streamp, fn_streamp, 0x11, NULL },
  { string_eq, fn_eq, 0x22, NULL },
  { string_equal, fn_equal, 0x22, NULL },
  { string_car, fn_car, 0x11, NULL },
  { string_first, fn_car, 0x11, NULL },
  { string_cdr, fn_cdr, 0x11, NULL },
  { string_rest, fn_cdr, 0x11, NULL },
  { string_caar, fn_caar, 0x11, NULL },
  { string_cadr, fn_cadr, 0x11, NULL },
  { string_second, fn_cadr, 0x11, NULL },
  { string_cdar, fn_cdar, 0x11, NULL },
  { string_cddr, fn_cddr, 0x11, NULL },
  { string_caaar, fn_caaar, 0x11, NULL },
  { string_caadr, fn_caadr, 0x11, NULL },
  { string_cadar, fn_cadar, 0x11, NULL },
  { string_caddr, fn_caddr, 0x11, NULL },
  { string_third, fn_caddr, 0x11, NULL },
  { string_cdaar, fn_cdaar, 0x11, NULL },
  { string_cdadr, fn_cdadr, 0x11, NULL },
  { string_cddar, fn_cddar, 0x11, NULL },
  { string_cdddr, fn_cdddr, 0x11, NULL },
  { string_length, fn_length, 0x11, NULL },
  { string_arraydimensions, fn_arraydimensions, 0x11, NULL },
  { string_list_, fn_list, 0x0F, NULL },
  { string_makearray, fn_makearray, 0x15, NULL },
  { string_reverse, fn_reverse, 0x11, NULL },
  { string_nth, fn_nth, 0x22, NULL },
  { string_aref, fn_aref, 0x2F, NULL },
  { string_assoc, fn_assoc, 0x22, NULL },
  { string_member, fn_member, 0x22, NULL },
  { string_apply, fn_apply, 0x2F, NULL },
  { string_funcall, fn_funcall, 0x1F, NULL },
  { string_append, fn_append, 0x0F, NULL },
  { string_mapc, fn_mapc, 0x2F, NULL },
  { string_mapcar, fn_mapcar, 0x2F, NULL },
  { string_mapcan, fn_mapcan, 0x2F, NULL },
  { string_plus, fn_add, 0x0F, NULL },
  { string_minus, fn_subtract, 0x1F, NULL },
  { string_times, fn_multiply, 0x0F, NULL },
  { string_divide, fn_divide, 0x1F, NULL },
  { string_mod, fn_mod, 0x22, NULL },
  { string_oneplus, fn_oneplus, 0x11, NULL },
  { string_oneminus, fn_oneminus, 0x11, NULL },
  { string_abs, fn_abs, 0x11, NULL },
  { string_random, fn_random, 0x11, NULL },
  { string_max, fn_maxfn, 0x1F, NULL },
  { string_min, fn_minfn, 0x1F, NULL },
  { string_noteq, fn_noteq, 0x1F, NULL },
  { string_numeq, fn_numeq, 0x1F, NULL },
  { string_less, fn_less, 0x1F, NULL },
  { string_lesseq, fn_lesseq, 0x1F, NULL },
  { string_greater, fn_greater, 0x1F, NULL },
  { string_greatereq, fn_greatereq, 0x1F, NULL },
  { string_plusp, fn_plusp, 0x11, NULL },
  { string_minusp, fn_minusp, 0x11, NULL },
  { string_zerop, fn_zerop, 0x11, NULL },
  { string_oddp, fn_oddp, 0x11, NULL },
  { string_evenp, fn_evenp, 0x11, NULL },
  { string_integerp, fn_integerp, 0x11, NULL },
  { string_numberp, fn_numberp, 0x11, NULL },
  { string_float, fn_floatfn, 0x11, NULL },
  { string_floatp, fn_floatp, 0x11, NULL },
  { string_sin, fn_sin, 0x11, NULL },
  { string_cos, fn_cos, 0x11, NULL },
  { string_tan, fn_tan, 0x11, NULL },
  { string_asin, fn_asin, 0x11, NULL },
  { string_acos, fn_acos, 0x11, NULL },
  { string_atan, fn_atan, 0x12, NULL },
  { string_sinh, fn_sinh, 0x11, NULL },
  { string_cosh, fn_cosh, 0x11, NULL },
  { string_tanh, fn_tanh, 0x11, NULL },
  { string_exp, fn_exp, 0x11, NULL },
  { string_sqrt, fn_sqrt, 0x11, NULL },
  { string_log, fn_log, 0x12, NULL },
  { string_expt, fn_expt, 0x22, NULL },
  { string_ceiling, fn_ceiling, 0x12, NULL },
  { string_floor, fn_floor, 0x12, NULL },
  { string_truncate, fn_truncate, 0x12, NULL },
  { string_round, fn_round, 0x12, NULL },
  { string_char, fn_char, 0x22, NULL },
  { string_charcode, fn_charcode, 0x11, NULL },
  { string_codechar, fn_codechar, 0x11, NULL },
  { string_characterp, fn_characterp, 0x11, NULL },
  { string_stringp, fn_stringp, 0x11, NULL },
  { string_stringeq, fn_stringeq, 0x22, NULL },
  { string_stringlt, fn_stringless, 0x22, NULL },
  { string_stringgt, fn_stringgreater, 0x22, NULL },
  { string_sort, fn_sort, 0x22, NULL },
  { string_string, fn_stringfn, 0x11, NULL },
  { string_concatenate, fn_concatenate, 0x1F, NULL },
  { string_subseq, fn_subseq, 0x23, NULL },
  { string_readfromstring, fn_readfromstring, 0x11, NULL },
  { string_princtostring, fn_princtostring, 0x11, NULL },
  { string_prin1tostring, fn_prin1tostring, 0x11, NULL },
  { string_logand, fn_logand, 0x0F, NULL },
  { string_logior, fn_logior, 0x0F, NULL },
  { string_logxor, fn_logxor, 0x0F, NULL },
  { string_lognot, fn_lognot, 0x11, NULL },
  { string_ash, fn_ash, 0x22, NULL },
  { string_logbitp, fn_logbitp, 0x22, NULL },
  { string_eval, fn_eval, 0x11, NULL },
  { string_globals, fn_globals, 0x00, NULL },
  { string_locals, fn_locals, 0x00, NULL },
  { string_makunbound, fn_makunbound, 0x11, NULL },
  { string_break, fn_break, 0x00, NULL },
  { string_read, fn_read, 0x01, NULL },
  { string_prin1, fn_prin1, 0x12, NULL },
  { string_print, fn_print, 0x12, NULL },
  { string_princ, fn_princ, 0x12, NULL },
  { string_terpri, fn_terpri, 0x01, NULL },
  { string_readbyte, fn_readbyte, 0x02, NULL },
  { string_readline, fn_readline, 0x01, NULL },
  { string_writebyte, fn_writebyte, 0x12, NULL },
  { string_writestring, fn_writestring, 0x12, NULL },
  { string_writeline, fn_writeline, 0x12, NULL },
  { string_restarti2c, fn_restarti2c, 0x12, NULL },
  { string_gc, fn_gc, 0x00, NULL },
  { string_room, fn_room, 0x00, NULL },
  { string_saveimage, fn_saveimage, 0x01, NULL },
  { string_loadimage, fn_loadimage, 0x01, NULL },
  { string_cls, fn_cls, 0x00, NULL },
  { string_pinmode, fn_pinmode, 0x22, NULL },
  { string_digitalread, fn_digitalread, 0x11, NULL },
  { string_digitalwrite, fn_digitalwrite, 0x22, NULL },
  { string_analogread, fn_analogread, 0x11, NULL },
  { string_analogreadresolution, fn_analogreadresolution, 0x11, NULL },
  { string_analogwrite, fn_analogwrite, 0x22, NULL },
  { string_delay, fn_delay, 0x11, NULL },
  { string_millis, fn_millis, 0x00, NULL },
  { string_sleep, fn_sleep, 0x11, NULL },
  { string_note, fn_note, 0x03, NULL },
  { string_register, fn_register, 0x12, NULL },
  { string_edit, fn_edit, 0x11, NULL },
  { string_pprint, fn_pprint, 0x12, NULL },
  { string_pprintall, fn_pprintall, 0x01, NULL },
  { string_format, fn_format, 0x2F, NULL },
  { string_require, fn_require, 0x11, NULL },
  { string_listlibrary, fn_listlibrary, 0x00, NULL },
  { string_available, fn_available, 0x11, NULL },
  { string_wifiserver, fn_wifiserver, 0x00, NULL },
  { string_wifisoftap, fn_wifisoftap, 0x04, NULL },
  { string_connected, fn_connected, 0x11, NULL },
  { string_wifilocalip, fn_wifilocalip, 0x00, NULL },
  { string_wificonnect, fn_wificonnect, 0x02, NULL },
  { string_drawpixel, fn_drawpixel, 0x23, NULL },
  { string_drawline, fn_drawline, 0x45, NULL },
  { string_drawrect, fn_drawrect, 0x45, NULL },
  { string_fillrect, fn_fillrect, 0x45, NULL },
  { string_drawcircle, fn_drawcircle, 0x34, NULL },
  { string_fillcircle, fn_fillcircle, 0x34, NULL },
  { string_drawroundrect, fn_drawroundrect, 0x56, NULL },
  { string_fillroundrect, fn_fillroundrect, 0x56, NULL },
  { string_drawtriangle, fn_drawtriangle, 0x67, NULL },
  { string_filltriangle, fn_filltriangle, 0x67, NULL },
  { string_drawchar, fn_drawchar, 0x36, NULL },
  { string_setcursor, fn_setcursor, 0x22, NULL },
  { string_settextcolor, fn_settextcolor, 0x12, NULL },
  { string_settextsize, fn_settextsize, 0x11, NULL },
  { string_settextwrap, fn_settextwrap, 0x11, NULL },
  { string_fillscreen, fn_fillscreen, 0x01, NULL },
  { string_setrotation, fn_setrotation, 0x11, NULL },
  { string_invertdisplay, fn_invertdisplay, 0x11, NULL },
  { string_SPECIALKEYWORDS, NULL, 0x00, NULL },
  { string_ledbuiltin, (fn_ptr_type)LED_BUILTIN, 0, NULL },
  { string_high, (fn_ptr_type)HIGH, DIGITALWRITE, NULL },
  { string_low, (fn_ptr_type)LOW, DIGITALWRITE, NULL },
  { string_input, (fn_ptr_type)INPUT, PINMODE, NULL },
  { string_inputpullup, (fn_ptr_type)INPUT_PULLUP, PINMODE, NULL },
  { string_inputpulldown, (fn_ptr_type)INPUT_PULLDOWN, PINMODE, NULL },
  { string_output, (fn_ptr_type)OUTPUT, PINMODE, NULL },
  { string_USERFUNCTIONS, NULL, 0x00, NULL },

// Insert your own table entries here
  { string_batteryvoltage, fn_batteryvoltage, 0x00, NULL },
  { string_batterypercentage, fn_batterypercentage, 0x00, NULL },
  { string_batterychangerate, fn_batterychangerate, 0x00, NULL },

};

// Table lookup functions

builtin_t lookupbuiltin (char* n) {
  int entry = 0;
  while (entry < ENDFUNCTIONS) {
    if (strcasecmp_P(n, lookup_table[entry].string) == 0)
      return (builtin_t)entry;
    entry++;
  }
  return ENDFUNCTIONS;
}

intptr_t lookupfn (builtin_t name) {
  return (intptr_t)pgm_read_ptr(&lookup_table[name].fptr);
}

uint8_t getminmax (builtin_t name) {
  uint8_t minmax = pgm_read_byte(&lookup_table[name].minmax);
  return minmax;
}

void checkminmax (builtin_t name, int nargs) {
  uint8_t minmax = getminmax(name);
  if (nargs<(minmax >> 4)) error2(name, toofewargs);
  if ((minmax & 0x0f) != 0x0f && nargs>(minmax & 0x0f)) error2(name, toomanyargs);
}

char *lookupdoc (builtin_t name) {
  return (char*)lookup_table[name].doc;
}

void testescape () {
  if (Serial.read() == '~') error2(NIL, PSTR("escape!"));
}

// Main evaluator


object *eval (object *form, object *env) {
  int TC=0;
  EVAL:
  yield();
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

  // bool is_macro = consp(car(head)) && (isbuiltin(car(car(head)), MACRO));

  while (form != NULL) {
    object *obj;
    // don't evaluate args to a macro
    // if (is_macro) obj = cons(car(form),NULL);
    /*else */obj = cons(eval(car(form),env),NULL);
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

void pserial (char c) {
  LastPrint = c;
  if (c == '\n') Serial.write('\r');
  Serial.write(c);
}

const char ControlCodes[] PROGMEM = "Null\0SOH\0STX\0ETX\0EOT\0ENQ\0ACK\0Bell\0Backspace\0Tab\0Newline\0VT\0"
"Page\0Return\0SO\0SI\0DLE\0DC1\0DC2\0DC3\0DC4\0NAK\0SYN\0ETB\0CAN\0EM\0SUB\0Escape\0FS\0GS\0RS\0US\0Space\0";

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

void pstring (char *s, pfun_t pfun) {
  while (*s) pfun(*s++);
}

void plispstring (object *form, pfun_t pfun) {
  plispstr(form->name, pfun);
}

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

void printstring (object *form, pfun_t pfun) {
  if (tstflag(PRINTREADABLY)) pfun('"');
  plispstr(form->name, pfun);
  if (tstflag(PRINTREADABLY)) pfun('"');
}

void pbuiltin (builtin_t name, pfun_t pfun) {
  int p = 0;
  PGM_P s = (char*)pgm_read_dword(&lookup_table[name].string);
  while (1) {
    char c = pgm_read_byte(&s[p++]);
    if (c == 0) return;
    pfun(c);
  }
}

void pradix40 (symbol_t name, pfun_t pfun) {
  uint32_t x = untwist(name);
  for (int d=102400000; d>0; d = d/40) {
    uint32_t j = x/d;
    char c = fromradix40(j);
    if (c == 0) return;
    pfun(c); x = x - j*d;
  }
}

void printsymbol (object *form, pfun_t pfun) {
  psymbol(form->name, pfun);
}

void psymbol (symbol_t name, pfun_t pfun) {
  if ((name & 0x03) == 0) plispstr(name, pfun);
  else {
    uint32_t value = untwist(name);
    if (value < PACKEDS) error2(NIL, PSTR("invalid symbol"));
    else if (value >= BUILTINS) pbuiltin((builtin_t)(value-BUILTINS), pfun);
    else pradix40(name, pfun);
  }
}

void pfstring (PGM_P s, pfun_t pfun) {
  int p = 0;
  while (1) {
    char c = pgm_read_byte(&s[p++]);
    if (c == 0) return;
    pfun(c);
  }
}

void pint (int i, pfun_t pfun) {
  uint32_t j = i;
  if (i<0) { pfun('-'); j=-i; }
  pintbase(j, 10, pfun);
}

void pintbase (uint32_t i, uint8_t base, pfun_t pfun) {
  bool lead = false; uint32_t p = 1000000000;
  if (base == 2) p = 0x80000000; else if (base == 16) p = 0x10000000;
  for (uint32_t d = p; d > 0; d /= base) {
    uint32_t j = i/d;
    if (j != 0 || lead || d == 1) { pfun((j<10) ? j+'0' : j+'W'); lead = true; }
    i = i - j*d;
  }
}

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

void pfloat (float f, pfun_t pfun) {
  if (isnan(f)) { pfstring(PSTR("NaN"), pfun); return; }
  if (f == 0.0) { pfun('0'); return; }
  if (f < 0) { pfun('-'); f = -f; }
  if (isinf(f)) { pfstring(PSTR("Infinity"), pfun); return; }
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

inline void pln (pfun_t pfun) {
  pfun('\n');
}

void pfl (pfun_t pfun) {
  if (LastPrint != '\n') pfun('\n');
}

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

void pstream (object *form, pfun_t pfun) {
  pfun('<');
  pfstring(streamname[(form->integer)>>8], pfun);
  pfstring(PSTR("-stream "), pfun);
  pint(form->integer & 0xFF, pfun);
  pfun('>');
}

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

void prin1object (object *form, pfun_t pfun) {
  uint16_t temp = Flags;
  clrflag(PRINTREADABLY);
  printobject(form, pfun);
  Flags = temp;
}

// Read functions

int glibrary () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  char c = pgm_read_byte(&LispLibrary[GlobalStringIndex++]);
  return (c != 0) ? c : -1; // -1?
}

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

// For line editor
const int TerminalWidth = 80;
volatile int WritePtr = 0, ReadPtr = 0;
const int KybdBufSize = 333; // 42*8 - 3
char KybdBuf[KybdBufSize];
volatile uint8_t KybdAvailable = 0;

// Parenthesis highlighting
void esc (int p, char c) {
  Serial.write('\e'); Serial.write('[');
  Serial.write((char)('0'+ p/100));
  Serial.write((char)('0'+ (p/10) % 10));
  Serial.write((char)('0'+ p % 10));
  Serial.write(c);
}

void hilight (char c) {
  Serial.write('\e'); Serial.write('['); Serial.write(c); Serial.write('m');
}

void Highlight (int p, int wp, uint8_t invert) {
  wp = wp + 2; // Prompt
#if defined (printfreespace)
  int f = Freespace;
  while (f) { wp++; f=f/10; }
#endif
  int line = wp/TerminalWidth;
  int col = wp%TerminalWidth;
  int targetline = (wp - p)/TerminalWidth;
  int targetcol = (wp - p)%TerminalWidth;
  int up = line-targetline, left = col-targetcol;
  if (p) {
    if (up) esc(up, 'A');
    if (col > targetcol) esc(left, 'D'); else esc(-left, 'C');
    if (invert) hilight('7');
    Serial.write('('); Serial.write('\b');
    // Go back
    if (up) esc(up, 'B'); // Down
    if (col > targetcol) esc(left, 'C'); else esc(-left, 'D');
    Serial.write('\b'); Serial.write(')');
    if (invert) hilight('0');
  }
}

void processkey (char c) {
  if (c == 27) { setflag(ESCAPE); return; }    // Escape key
#if defined(vt100)
  static int parenthesis = 0, wp = 0;
  // Undo previous parenthesis highlight
  Highlight(parenthesis, wp, 0);
  parenthesis = 0;
#endif
  // Edit buffer
  if (c == '\n' || c == '\r') {
    pserial('\n');
    KybdAvailable = 1;
    ReadPtr = 0;
    return;
  }
  if (c == 8 || c == 0x7f) {     // Backspace key
    if (WritePtr > 0) {
      WritePtr--;
      Serial.write(8); Serial.write(' '); Serial.write(8);
      if (WritePtr) c = KybdBuf[WritePtr-1];
    }
  } else if (WritePtr < KybdBufSize) {
    KybdBuf[WritePtr++] = c;
    Serial.write(c);
  }
#if defined(vt100)
  // Do new parenthesis highlight
  if (c == ')') {
    int search = WritePtr-1, level = 0;
    while (search >= 0 && parenthesis == 0) {
      c = KybdBuf[search--];
      if (c == ')') level++;
      if (c == '(') {
        level--;
        if (level == 0) {parenthesis = WritePtr-search-1; wp = WritePtr; }
      }
    }
    Highlight(parenthesis, wp, 1);
  }
#endif
  return;
}

int gserial () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
#if defined(lineeditor)
  while (!KybdAvailable) {
    while (!Serial.available());
    char temp = Serial.read();
    processkey(temp);
  }
  if (ReadPtr != WritePtr) return KybdBuf[ReadPtr++];
  KybdAvailable = 0;
  WritePtr = 0;
  return '\n';
#else
  unsigned long start = millis();
  while (!Serial.available()) { delay(1); if (millis() - start > 1000) clrflag(NOECHO); }
  char temp = Serial.read();
  if (temp != '\n' && !tstflag(NOECHO)) pserial(temp);
  return temp;
#endif
}

object *nextitem (gfun_t gfun) {
  int ch = gfun();
  while (issp(ch)) ch = gfun();

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

  while (!issp(ch) && ch != ')' && ch != '(' && index < bufmax) {
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

void initgfx () {
#if defined(gfxsupport)
  Wire.begin();
  tft.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  tft.fillScreen(COLOR_BLACK);
  tft.display();
#endif
}

void initenv () {
  GlobalEnv = NULL;
  tee = bsymbol(TEE);
}

#if defined(runfromsd)
const char SDMain[] PROGMEM = "(with-sd-card (file \"" sdmainfile "\") (loop (let ((form (read file))) (unless form (return)) (eval form))))";
int SDMainIndex = 0;

int gsdmain() {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  char c = pgm_read_byte(&SDMain[SDMainIndex++]);
  return (c != 0) ? c : -1; // -1?
}

void sdmain() {
    SD.begin();
    if (ulisp_setup_error_handling()) return;
    object *line = read(gsdmain);
    push(line, GCStack);
    (void)eval(line, NULL);
    pop(GCStack);
}
#endif

bool ulisp_setup_error_handling() {
  if (setjmp(toplevel_errorbuffer)) return true;
  return false;
}

void ulisp_init() {
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
  #if defined(runfromsd)
  sdmain();
  #endif
  pfstring(PSTR("\n\nuLisp 4.3 "), pserial); pln(pserial);
}

// Read/Evaluate/Print loop

void repl (object *env) {
  for (;;) {
    randomSeed(micros());
    gc(NULL, env);
    #if defined (printfreespace)
    pint(Freespace, pserial);
    #endif
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

void loop () {
  if (!ulisp_setup_error_handling()) {
    #if defined(resetautorun)
    volatile int autorun = 12; // Fudge to keep code size the same
    #else
    volatile int autorun = 13;
    #endif
    if (autorun == 12) autorunimage();
  }
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
  repl(NULL);
}
