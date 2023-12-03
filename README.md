# ulisp-esp32

A (patched) version of the Lisp programming language for ESP32-based boards.
Heavily customized to fit my use case but most of the original remains.
For more about the original ulisp-esp see <http://www.ulisp.com/show?3M#esp-version>

This is based off of uLisp 4.4d. For the old patches (some of which don't work) for
uLisp 4.3a please see the [4.3a-old](https://github.com/dragoncoder047/ulisp-esp32/tree/4.3a-old) branch.

Patches:

* Deleted: load/save/autorunimage support
* Modified: garbage collect message
* Deleted: line-editor support
* Added: Lisp `:keywords` that auto-quote themselves
* Added: Ability to add multiple (more than one) extension tables (using `calloc()`) *may not be portable to other platforms*
* Added: Lisp `(throw)` and `(catch)`
* Added: backquote/unquote/unquote-splicing
* Added: **EXPERIMENTAL** macros/defmacro/macroexpand *no support for destructuring lambda lists yet*
* Added: Auto-run contents of `main.lisp` (on microSD card) at startup
* Modified: SD-card functions now include filename in error messages
* Fixed: special forms don't need to call `checkargs()` because it is automatically called

Extensions (`extensions.hpp`):

* `now` (provided by David)
* `gensym`
* `intern`
* `sizeof`
* Everything from the [ulisp-bignums](https://github.com/technoblogy/ulisp-bignums) extension

## `term.py` -- enhanced uLisp interface

This provides a cleaner interface to use uLisp in compared to the stupid Arduino serial monitor.

Dependencies:

* A VT100-compliant terminal
* Python 3
* [pyserial](https://pypi.org/project/pyserial/) (to communicate with your microcontroller)
* [prompt_toolkit](https://pypi.org/project/prompt-toolkit/) (to draw the interface)
* [Pygments](https://pypi.org/project/Pygments/) (for syntax highliting)

To run:

```bash
# use default port and baud (/dev/ttyUSB0 and 115200)
python3 term.py
# specify port and baud
python3 term.py -p COM3 -b 9600
```

UI Overview:

```txt
----------------------------------------------------
|                    ^|                           ^|
|                     |                            |
|       LISP          |       SERIAL               |
|       BUFFER        |       MONITOR              |
|                     |                            |
|                     |                            |
|                    v|                           v|
|--------------------------------------------------|
|cmd>  COMMAND AREA                                |
|--------------------------------------------------|
| STATUS BAR                          RIGHT STATUS |
| MEMORY USAGE                        LAST GC INFO |
----------------------------------------------------
```

* Lisp Buffer: You can type Lisp code in here.
* Serial Monitor: This shows the output from the serial port.
* Command Area: You can type one-line Lisp commands in here, or you can type "special" commands (press <small>ENTER</small> to run them):
    * `.reset`: Trips the RTS line of the serial port, to reset your microcontroller if it locks up and `~` doesn't work.
    * `.run`: Sends the contents of the Lisp Buffer to the serial port, and then empty the Lisp Buffer.
    * `.quit`: Closes the serial port, and exits from the application.
* Status Bar: Shows whether the program is running, waiting for input at the REPL, crashed because of an error, etc.
* Right Status: Doesn't do anything on its own, but if your program prints out something of the form `$!rs=foo!$`, it will hide that string in the Serial Monitor, and put `foo` in the Right Status area. This is useful if you want to monitor the state of a pin in a loop, and you don't want to overload the Serial Monitor with a barrage of text.
* Memory Usage: Shows the percentage of memory used by your program in a couple of different ways and also changes color depending on how much memory is used. This is updated after every garbage collection.
* Last GC Info: Shows how many garbage collections have been done since the start of the program, and how much was freed on the most recent GC.
