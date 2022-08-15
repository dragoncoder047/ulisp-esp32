// ListLibrary.h - Version 1 - 8th February 2021
// execfile, member*, import, and dump added by dragoncoder047

const char LispLibrary[] PROGMEM =

// count
// Counts the number of items eq to x in lst.

"(defun count (x lst)"
"  (if (null lst) 0"
"    (+ (if (eq x (car lst)) 1 0) (count x (cdr lst)))))"

// count-if
// Counts the number of items in lst for which tst is true.

"(defun count-if (tst lst)"
"  (if (null lst) 0"
"    (+ (if (funcall tst (car lst)) 1 0) (count-if tst (cdr lst)))))"

// count-if-not
// Counts the number of items in lst for which tst is false.

"(defun count-if-not (tst lst)"
"  (if (null lst) 0"
"    (+ (if (funcall tst (car lst)) 0 1) (count-if-not tst (cdr lst)))))"

// eql
// In uLisp eq and eql are equivalent.

"(defvar eql eq)"

// every
// Returns t if tst is true for every item in lst, or returns nil on the first false item.

"(defun every (tst lst)"
"  (if (null lst) t"
"    (and (funcall tst (car lst)) (every tst (cdr lst)))))"

// find
// Returns x if x is in lst, or nil otherwise.

"(defun find (x lst) (car (member x lst)))"

// find-if
// Returns the first item for which tst is true in lst, or nil otherwise.

"(defun find-if (tst lst)"
"  (cond"
"   ((null lst) nil)"
"   ((funcall tst (car lst)) (car lst))"
"   (t (find-if tst (cdr lst)))))"

// find-if-not
// Returns the first item for which tst is false in lst, or nil otherwise.

"(defun find-if-not (tst lst)"
"  (cond"
"   ((null lst) nil)"
"   ((not (funcall tst (car lst))) (car lst))"
"   (t (find-if-not tst (cdr lst)))))"

// fourth
// Returns the fourth item in a list.

"(defun fourth (lst) (car (cdddr lst)))"

// identity
// Simply returns its argument.

"(defun identity (x) x)"

// last
// Returns the last cdr of lst.

"(defun last (lst)"
" (if (null (cdr lst)) lst"
"   (last (cdr lst))))"

// mapl
// Applies fn to successive cdrs of lst, and returns lst.

"(defun mapl (fn lst)"
"  (require 'mapl2)"
"  (mapl2 fn lst)"
"  lst)"

"(defun mapl2 (fn lst)"
"  (cond"
"   ((null lst) nil)"
"   (t (funcall fn lst)"
"      (mapl2 fn (cdr lst)))))"

// maplist
// Applies fn to successive cdrs of lst, and returns a list of the results.

"(defun maplist (fn lst)"
"  (if (null lst) nil"
"   (cons (funcall fn lst) (maplist fn (cdr lst)))))"

// nconc
// Destructively appends its arguments together, which must be lists.

"(defun nconc (&rest ls)"
"  (mapcan (lambda (x) x) ls))"

// nthcdr
// Returns the nth cdr of lst.

"(defun nthcdr (n lst)"
"  (if (zerop n) lst"
"    (nthcdr (1- n) (cdr lst))))"

// position
// Returns the position of the first x in lst, or nil if it's not found.

"(defun position (x lst &optional (n 0))"
"  (cond"
"   ((null lst) nil)"
"   ((eq x (car lst)) n)"
"   (t (position x (cdr lst) (1+ n)))))"

// position-if
// Returns the position of the first item in lst for which tst is true, or nil if none is found.

"(defun position-if (tst lst &optional (n 0))"
"  (cond"
"   ((null lst) nil)"
"   ((funcall tst (car lst)) n)"
"   (t (position-if tst (cdr lst) (1+ n)))))"

// position-if-not
// Returns the position of the first item in lst for which tst is false, or nil if none is found.

"(defun position-if-not (tst lst &optional (n 0))"
"  (cond"
"   ((null lst) nil)"
"   ((not (funcall tst (car lst))) n)"
"   (t (position-if-not tst (cdr lst) (1+ n)))))"

// reduce
// Returns the result of applying fn to successive pairs of items from lst.

"(defun reduce (fn lst)"
"  (if (null (cdr lst)) (car lst)"
"    (funcall fn (car lst) (reduce fn (cdr lst)))))"

// remove
// Removes all occurrences of x from lst.

"(defun remove (x lst)"
"  (mapcan (lambda (y) (unless (eq x y) (list y))) lst))"

// remove-if
// Removes all items from lst for which tst is true.

"(defun remove-if (tst lst)"
"  (mapcan (lambda (x) (unless (funcall tst x) (list x))) lst))"

// remove-if-not
// Removes all items from lst for which tst is false.

"(defun remove-if-not (tst lst)"
"  (mapcan (lambda (x) (when (funcall tst x) (list x))) lst))"

// subseq (for lists)
// Returns the subsequence of lst from item n to item m-1.

"(defun subseq* (lst n m)"
"  (cond"
"   ((> n 0) (subseq* (cdr lst) (1- n) (1- m)))"
"   ((zerop m) nil)"
"   (t (cons (car lst) (subseq* (cdr lst) 0 (1- m))))))"

// third
// Returns the third item in a list.

"(defvar third caddr)"

// execfile
// Loads the file off the SD card and runs it.
// Does not resolve circular dependencies.

"(defun execfile (filename)"
"  (with-sd-card (fp filename)"
"    (loop"
"      (let ((form (read fp)))"
"        (unless form (return))"
"        (eval form)))"
"    filename))"

// member*
// Returns t if lst has the element elem, using equal for comparison.

"(defun member* (elem lst)"
"  (require 'equal)"
"  (let ((found nil))"
"    (dolist (x lst)"
"      (when (equal x elem) (setq found t) (return)))"
"    found))"

// import
// Runs the file, but only if it has not been run before. This allows circular dependencies.

"(defun import (filename)"
"  (require 'execfile)"
"  (require 'member*)"
"  (require 'imported)"
"  (if (member* filename imported) nil"
"    (progn"
"     (push filename imported)"
"     (execfile filename))))"

"(defvar imported nil)"

// dump
// Dumps the workspace into the specified file (default is "dump.lisp")

"(defun dump (&optional (filename \"dump.lisp\"))"
"  (with-sd-card (fp filename)"
"    (pprintall fp)))"

"\0"

;
