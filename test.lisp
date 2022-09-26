(defmacro foo (aa bb)
    `(defmacro ,aa ()
        `(princ ,,bb)))
(foo bar "baz")
