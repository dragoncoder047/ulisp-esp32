# ulisp-esp32

A version of the Lisp programming language for ESP32-based boards.

For more information see http://www.ulisp.com/show?21T5

Patches from original:

* Deleted all code not for ESP32 specifically (shorter and easier to maintain)
* Run-from-SD on startup
* Different printgcs message
* Used actual name instead of `stringNNN` in lookup table
* Goheeca and Max-Gerd Retzlaff's error-handling code (https://github.com/Goheeca/redbear_duo-uLisp/commit/4894c13 and http://forum.ulisp.com/t/error-handling-in-ulisp/691/7), but replaced `sp_error` with `fn_throw` (`sp_error` caused a segfault)
* Dave Astels' `defmacro`, `intern`, and generic `:keyword` support (http://forum.ulisp.com/t/ive-added-a-few-things-that-might-be-interesting/456)

New custom functions:

* `battery:voltage`, `battery:percentage`, `battery:change-rate` (reading from the MAX17048 on a SparkFun Thing Plus C)
