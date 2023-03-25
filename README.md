# ulisp-esp32

A (patched) version of the Lisp programming language for ESP32-based boards.
Heavily customized to fit my use case but most of the original remains.
For more about the original ulisp-esp see <http://www.ulisp.com/show?21T5>

This is based off of uLisp 4.4. For the old patches (some of which don't work) for
uLisp 4.3a please see the [4.3a-old](https://github.com/dragoncoder047/ulisp-esp32/tree/4.3a-old) branch.

Patches:

* Deleted load/save/autorunimage support
* different garbage collect message
