#include <stdio.h>
#include <config.h>

#include "lisp.h"
#include <ffi/ffi.h>
#include <dlfcn.h>
#include <math.h>
#include <sys/mman.h>

typedef struct {
  ffi_cif cif;
  ffi_type *arg_types[0];
} ffi_cif_and_arg_types;

INLINE static void *
lisp_to_ptr(Lisp_Object l)
{
  if (STRINGP(l))
    return XSTRING(l)->data;
  else if (EQ(l, Qnil))
    return NULL;
  else
    {
      intptr_t ret;
      CONS_TO_INTEGER(l, intptr_t, ret);
      return (void*)ret;
    }
}

INLINE static Lisp_Object
ptr_to_lisp(void *p)
{
  if (!p)
    return Qnil;
  return INTEGER_TO_CONS((intptr_t)p);
}

static ffi_type *
get_type (Lisp_Object desc)
{
  if (EQ(desc, Qvoid)) return &ffi_type_void;

  else if (EQ(desc, Qint8)) return &ffi_type_sint8;
  else if (EQ(desc, Qint16)) return &ffi_type_sint16;
  else if (EQ(desc, Qint32)) return &ffi_type_sint32;
  else if (EQ(desc, Qint64)) return &ffi_type_sint64;

  else if (EQ(desc, Quint8)) return &ffi_type_uint8;
  else if (EQ(desc, Quint16)) return &ffi_type_uint16;
  else if (EQ(desc, Quint32)) return &ffi_type_uint32;
  else if (EQ(desc, Quint64)) return &ffi_type_uint64;

  else if (EQ(desc, Qchar)) return &ffi_type_schar;
  else if (EQ(desc, Qshort)) return &ffi_type_sshort;
  else if (EQ(desc, Qint)) return &ffi_type_sint;
  else if (EQ(desc, Qlong)) return &ffi_type_slong;

  else if (EQ(desc, Quchar)) return &ffi_type_uchar;
  else if (EQ(desc, Qushort)) return &ffi_type_ushort;
  else if (EQ(desc, Quint)) return &ffi_type_uint;
  else if (EQ(desc, Qulong)) return &ffi_type_ulong;

  else if (EQ(desc, Qfloat)) return &ffi_type_float;
  else if (EQ(desc, Qdouble)) return &ffi_type_double;
  else if (EQ(desc, Qlongdouble)) return &ffi_type_longdouble;

  else if (EQ(desc, Qpointer)) return &ffi_type_pointer;

  else if (CONSP(desc) && EQ(XCAR(desc), Qstruct))
    {
      int len = XINT(Flength(desc));
      // FIXME: MEMORY LEAK!
      struct {
        ffi_type type;
        ffi_type * slots[0];
      } *ret = malloc(sizeof(ret->type) + len * sizeof(ret->slots[0]));

      ret->type.type = FFI_TYPE_STRUCT;
      ret->type.elements = ret->slots;

      // libffi fills in these fields for us
      ret->type.size = 0;
      ret->type.alignment = 0;

      ffi_type **slot;
      Lisp_Object iter;
      for (slot = ret->slots, iter = XCDR(desc); CONSP(iter); iter = XCDR(iter), slot++)
        {
          *slot = get_type (XCAR (iter));
        }
      // null terminate array
      *slot = 0;
      return &ret->type;
    }
  else error("unknown type %s", SDATA (Fprin1_to_string (desc, Qt)));
}

static void
lisp_to_c (ffi_type *type, Lisp_Object obj, void *loc)
{
  switch (type->type)
    {
    case FFI_TYPE_VOID:
      break;

    case FFI_TYPE_INT:
      CHECK_NUMBER(obj);
      *(int *)loc = XINT(obj);
      break;

    case FFI_TYPE_FLOAT:
      CHECK_NUMBER_OR_FLOAT(obj);
      *(float *)loc = extract_float(obj);
      break;

    case FFI_TYPE_DOUBLE:
      CHECK_NUMBER_OR_FLOAT(obj);
      *(double *)loc = extract_float(obj);
      break;

    case FFI_TYPE_LONGDOUBLE:
      CHECK_NUMBER_OR_FLOAT(obj);
      *(long double *)loc = extract_float(obj);
      break;

    case FFI_TYPE_UINT8:
      CHECK_NUMBER(obj);
      *(uint8_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_SINT8:
      CHECK_NUMBER(obj);
      *(int8_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_UINT16:
      CHECK_NUMBER(obj);
      *(uint16_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_SINT16:
      CHECK_NUMBER(obj);
      *(int16_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_UINT32:
      CHECK_NUMBER(obj);
      *(uint32_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_SINT32:
      CHECK_NUMBER(obj);
      *(int32_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_UINT64:
      CHECK_NUMBER(obj);
      *(uint64_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_SINT64:
      CHECK_NUMBER(obj);
      *(int64_t *)loc = XINT(obj);
      break;

    case FFI_TYPE_POINTER:
      *(void **)loc = lisp_to_ptr(obj);
      break;

    case FFI_TYPE_STRUCT:
      {
        CHECK_VECTOR(obj);
        ptrdiff_t vsize = ASIZE(obj);

        ptrdiff_t i;
        for (i = 0; i < vsize && type->elements[i]; i++)
          {
            lisp_to_c(type->elements[i], AREF(obj, i), loc);
            loc = ((uint8_t*)loc) + type->elements[i]->size;
          }

        if (i != vsize || type->elements[i])
          {
            error("ffi: struct element count mismatch");
          }

        break;
      }

    default:
      error("lisp_to_c: type %d unimplemented", type->type);
    }
}

static Lisp_Object
c_to_lisp (ffi_type *type, void *loc)
{
  switch(type->type)
    {
    case FFI_TYPE_VOID: return Qnil;
    case FFI_TYPE_INT: return make_number(*(int *)loc);
    case FFI_TYPE_FLOAT: return make_float(*(float *)loc);
    case FFI_TYPE_DOUBLE: return make_float(*(double *)loc);
    case FFI_TYPE_LONGDOUBLE: return make_float(*(long double *)loc);
    case FFI_TYPE_UINT8: return make_number(*(uint8_t *)loc);
    case FFI_TYPE_SINT8: return make_number(*(int8_t *)loc);
    case FFI_TYPE_UINT16: return make_number(*(uint16_t *)loc);
    case FFI_TYPE_SINT16: return make_number(*(int16_t *)loc);
    case FFI_TYPE_UINT32: return make_number(*(uint32_t *)loc);
    case FFI_TYPE_SINT32: return make_number(*(int32_t *)loc);
    case FFI_TYPE_UINT64: return make_number(*(uint64_t *)loc);
    case FFI_TYPE_SINT64: return make_number(*(int64_t *)loc);
    case FFI_TYPE_POINTER: return ptr_to_lisp(*(void **)loc);

    case FFI_TYPE_STRUCT:
      {
        ptrdiff_t length = 0;
        while (type->elements[length])
          length++;

        Lisp_Object ret = make_uninit_vector(length);

        for (ptrdiff_t i = 0; i < length; i++)
          {
            ASET(ret, i, c_to_lisp(type->elements[i], loc));
            loc = ((char *)loc) + type->elements[i]->size;
          }
        return ret;
      }

    default:
      error("c_to_lisp: type %d unimplemented", type->type);
    }
}

DEFUN("ffi-call", Fffi_call, Sffi_call, 2, MANY, 0,
      doc: /* Call native function at FN-PTR with ARGS using
information stored in CIF.
usage: (ffi-call CIF FN-PTR ARGS...) */)
  (ptrdiff_t argc, Lisp_Object *argv)
{
  CHECK_STRING(argv[0]);

  ffi_cif_and_arg_types *cif_and_args = (ffi_cif_and_arg_types*)SDATA(argv[0]);
  ffi_cif *cif = &cif_and_args->cif;
  // restore the pointer
  cif->arg_types = cif_and_args->arg_types;

  void *obj = lisp_to_ptr(argv[1]);

  if (argc - 2 != cif->nargs)
    {
      error ("ffi arity mismatch: expected %d, got %d", (int)cif->nargs, (int)(argc - 2));
    }

  void *input[cif->nargs];
  for (int i = 0; i < cif->nargs; i++) {
    ffi_type *type = cif->arg_types[i];
    input[i] = alloca(type->size);
    lisp_to_c(type, argv[i + 2], input[i]);
  }
  void *result = alloca(cif->rtype->size);
  ffi_call(cif, obj, result, input);

  return c_to_lisp(cif->rtype, result);
}

DEFUN("ffi-lib", Fffi_lib, Sffi_lib, 1, 1, 0,
      doc: /* Loads dynamic library at PATH and returns a handle to it, or nil if open failed. */)
  (Lisp_Object path)
{
  CHECK_STRING(path);
  char *cpath = SSDATA(path);
  void *handle = dlopen(cpath, RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    {
      return Qnil;
    }
  return ptr_to_lisp(handle);
}

DEFUN("ffi-obj", Fffi_obj, Sffi_obj, 2, 2, 0,
      doc: /* You probably want to use FFI-GET-OBJ instead, it's much nicer.

Get a handle to symbol OBJNAME from dynamic library LIB. If LIB is
nil, search all open images. Returns nil if load fails. */)
  (Lisp_Object objname, Lisp_Object lib)
{
  CHECK_STRING(objname);

  void *libp = lisp_to_ptr(lib);
  if (!libp) {
    libp = RTLD_DEFAULT;
  }

  void *dlobj = dlsym(libp, SSDATA(objname));
  return ptr_to_lisp(dlobj);
}

DEFUN("ffi-make-cif", Fffi_make_cif, Sffi_make_cif, 2, 2, 0,
      doc: /* Create an opaque Call InterFace that encodes argument
and return types for calling to and from native code. Returns nil on failure. */)
  (Lisp_Object arg_types, Lisp_Object ret_type)
{
  ffi_abi abi = FFI_DEFAULT_ABI;
  ffi_type *rtype = get_type(ret_type);
  int arg_count = XFASTINT(Flength(arg_types));

  ptrdiff_t caa_len = sizeof(ffi_cif_and_arg_types) + sizeof(ffi_type) * arg_count;
  Lisp_Object ret = make_uninit_string(caa_len);
  ffi_cif_and_arg_types *cif_and_args = (void *)SDATA(ret);

  Lisp_Object iter;
  int i;
  for (i = 0, iter = arg_types; iter != Qnil; i++, iter = XCDR(iter))
    {
      cif_and_args->arg_types[i] = get_type(XCAR(iter));
    }


  if (FFI_OK != ffi_prep_cif(&cif_and_args->cif, abi, arg_count, rtype, cif_and_args->arg_types))
    {
      return Qnil;
    }

  return ret;
}

static void
closure_target(ffi_cif *cif, void *result, void **args, void *userdata)
{
  int largc = cif->nargs + 1;
  Lisp_Object largs[largc];
  largs[0] = (Lisp_Object)userdata;
  for (int i = 1; i < largc; i++)
    {
      largs[i] = c_to_lisp(cif->arg_types[i - 1], args[i - 1]);
    }
  lisp_to_c(cif->rtype, Ffuncall(largc, largs), result);
}

DEFUN("ffi-closure", Fffi_closure, Sffi_closure, 2, 2, 0,
      doc: /* create a callback that calls FN_SYMBOL with function
interface described in CIF. Despite this function having closure in
the name, right now it will only accept a symbol as a
callback. Returns nil on failure */)
  (Lisp_Object fn_symbol, Lisp_Object cif)
{
  CHECK_SYMBOL(fn_symbol); // TODO: make less restrictive and figure out GC
  CHECK_STRING(cif);

  ffi_cif_and_arg_types *cif_and_args = (ffi_cif_and_arg_types*)SDATA(cif);
  ffi_cif *real_cif = &cif_and_args->cif;
  // restore the pointer
  real_cif->arg_types = cif_and_args->arg_types;

  // FIXME: MEMORY LEAK!
  ffi_closure *closure;
  if ((closure = mmap(NULL, sizeof(*closure), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == (void*)-1)
    {
      return Qnil;
    }
  if (ffi_prep_closure(closure, real_cif, closure_target, (void*)fn_symbol) != FFI_OK)
    {
      munmap(closure, sizeof(*closure));
      return Qnil;
    }
  if (mprotect(closure, sizeof(*closure), PROT_READ | PROT_EXEC) == -1)
    {
      munmap(closure, sizeof(*closure));
      return Qnil;
    }

  return ptr_to_lisp(closure);
}

DEFUN("ffi-cstring-to-string", Fffi_cstring_to_string, Sffi_cstring_to_string, 1, 1, 0,
      doc: /* turns a pointer LPTR returned from the ffi into an emacs string (or nil) */)
  (Lisp_Object lptr)
{
  void * ptr = lisp_to_ptr(lptr);
  if (!ptr)
    {
      return Qnil;
    }
  return build_string(ptr);
}

DEFUN("ffi-value-to-string", Fffi_value_to_string, Sffi_value_to_string, 1, 1, 0,
      doc: /* turns the integer value of LPTR into an emacs string */)
     (Lisp_Object lptr)
{
  void * ptr = lisp_to_ptr(lptr);
  return make_unibyte_string((void*)&ptr, sizeof(ptr));
}

void
syms_of_ffi (void)
{
  DEFSYM (Qvoid, "void");

  DEFSYM (Qint8, "int8");
  DEFSYM (Qint16, "int16");
  DEFSYM (Qint32, "int32");
  DEFSYM (Qint64, "int64");

  DEFSYM (Quint8, "uint8");
  DEFSYM (Quint16, "uint16");
  DEFSYM (Quint32, "uint32");
  DEFSYM (Quint64, "uint64");

  DEFSYM (Qchar, "char");
  DEFSYM (Qshort, "short");
  DEFSYM (Qint, "int");
  DEFSYM (Qlong, "long");

  DEFSYM (Quchar, "uchar");
  DEFSYM (Qushort, "ushort");
  DEFSYM (Quint, "uint");
  DEFSYM (Qulong, "ulong");

  DEFSYM (Qfloat, "float");
  DEFSYM (Qdouble, "double");
  DEFSYM (Qlongdouble, "longdouble");

  DEFSYM (Qpointer, "pointer");

  DEFSYM (Qstruct, "struct");

  defsubr (&Sffi_call);
  defsubr (&Sffi_lib);
  defsubr (&Sffi_obj);
  defsubr (&Sffi_make_cif);
  defsubr (&Sffi_closure);
  defsubr (&Sffi_cstring_to_string);
  defsubr (&Sffi_value_to_string);
}
