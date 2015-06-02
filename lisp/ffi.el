;;; ffi.el --- ffi stuff  -*- lexical-binding: t -*-

(defun ffi--cif-from-type (type)
 (cond
  ((and (listp type) (eq (car type) '->))
   (ffi-make-cif (cdr (butlast type)) (car (last type))))))

(defun ffi-callback (fn-sym type)
 "create a callback that calls FN-SYM with function interface
described by TYPE (see FFI-GET-OBJ for details on TYPE). Right
now this function will only accept a symbol as a callback,
closures and lambda in the future hopefully.

Returns a ffi pointer suitable for passing to native code.

Returns nil on failure"
 (pcase type
  (`(-> . ,_)
   (let ((cif (ffi--cif-from-type type)))
    (ffi-closure fn-sym cif)))
  (_ (error "unhandled type %s" type))))

(defun ffi-get-obj (objname lib type &optional failure-thunk)
 "looks up symbol OBJMANE in LIB (or globally if nil)

Returns a lisp representation of the object (currently only
functions are implemented)

TYPE can take the following forms:
  (-> TYPE... TYPE)
  (struct TYPE...)
  void
  int8
  int16
  int32
  int64
  uint8
  uint16
  uint32
  uint64
  char
  short
  int
  long
  uchar
  ushort
  uint
  ulong
  float
  double
  longdouble
  pointer"

 (let ((obj (ffi-obj objname lib)))
  (pcase type
   (`(-> . ,_)
    (let ((cif (ffi--cif-from-type type)))
     (lambda (&rest args)
      (apply #'ffi-call cif obj args))))
   (_ (error "unhandled type %s" type)))))

(provide 'ffi)

;;; ffi.el ends here
