/*
  Another Python Sqlite Wrapper

  This wrapper aims to be the minimum necessary layer over SQLite 3
  itself.

  It assumes we are running as 32 bit int with a 64 bit long long type
  available.

  Copyright (C) 2004-2008 Roger Binns <rogerb@rogerbinns.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.
 
  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:
 
  1. The origin of this software must not be misrepresented; you must
     not claim that you wrote the original software. If you use this
     software in a product, an acknowledgment in the product
     documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must
     not be misrepresented as being the original software.

  3. This notice may not be removed or altered from any source
     distribution.
 
*/

/* Fight with setuptools over ndebug */
#ifdef APSW_NO_NDEBUG
#ifdef NDEBUG
#undef NDEBUG
#endif
#endif

/* SQLite amalgamation */
#ifdef APSW_USE_SQLITE_AMALGAMATION
/* See SQLite ticket 2554 */
#define SQLITE_API static
#define SQLITE_EXTERN static
#include APSW_USE_SQLITE_AMALGAMATION

/* Fight with SQLite over ndebug */
#ifdef APSW_NO_NDEBUG
#ifdef NDEBUG
#undef NDEBUG
#endif
#endif

#else
/* SQLite 3 headers */
#include "sqlite3.h"
#endif

#if SQLITE_VERSION_NUMBER < 3006002
#error Your SQLite version is too old.  It must be at least 3.6.2
#endif

/* system headers */
#include <assert.h>
#include <stdarg.h>

/* Get the version number */
#include "apswversion.h"

/* Python headers */
#include <Python.h>
#include <pythread.h>
#include "structmember.h"

#ifdef APSW_TESTFIXTURES
/* Fault injection */
#define APSW_FAULT_INJECT(name,good,bad)          \
do {                                              \
  if(APSW_Should_Fault(#name))                    \
    {                                             \
      do { bad ; } while(0);                      \
    }                                             \
  else                                            \
    {                                             \
      do { good ; } while(0);                     \
    }                                             \
 } while(0)

static int APSW_Should_Fault(const char *);

/* Are we Python 2.x (x>=5) and doing 64 bit? - _LP64 is best way I can find as sizeof isn't valid in cpp #if */
#if  PY_VERSION_HEX>=0x02050000 && defined(_LP64) && _LP64
#define APSW_TEST_LARGE_OBJECTS
#endif

#else /* APSW_TESTFIXTURES */
#define APSW_FAULT_INJECT(name,good,bad)        \
  do { good ; } while(0)

#endif

/* The encoding we use with SQLite.  SQLite supports either utf8 or 16
   bit unicode (host byte order).  If the latter is used then all
   functions have "16" appended to their name.  The encoding used also
   affects how strings are stored in the database.  We use utf8 since
   it is more space efficient, and Python can't make its mind up about
   Unicode (it uses 16 or 32 bit unichars and often likes to use Byte
   Order Markers as well). */
#define STRENCODING "utf-8"

/* The module object */
static PyObject *apswmodule;

/* Everything except the module itself is in seperate files */

/* Augment tracebacks */
#include "traceback.c"

/* Make various versions of Python code compatible with each other */
#include "pyutil.c"

/* A list of pointers (used by Connection to keep track of Cursors) */
#include "pointerlist.c"

/* Exceptions we can raise */
#include "exceptions.c"

/* various utility functions and macros */
#include "util.c"

/* buffer used in statement cache */
#include "apswbuffer.c"

/* The statement cache */
#include "statementcache.c"

/* connections */
#include "connection.c"

/* Zeroblob and blob */
#include "blob.c"

/* cursors */
#include "cursor.c"

/* virtual tables */
#include "vtable.c"

/* virtual file system */
#include "vfs.c"


/* MODULE METHODS */
static PyObject *
getsqliteversion(void)
{
  return MAKESTR(sqlite3_libversion());
}

static PyObject *
getapswversion(void)
{
  return MAKESTR(APSW_VERSION);
}

static PyObject *
enablesharedcache(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int setting,res;
  if(!PyArg_ParseTuple(args, "i:enablesharedcache(boolean)", &setting))
    return NULL;

  APSW_FAULT_INJECT(EnableSharedCacheFail,res=sqlite3_enable_shared_cache(setting),res=SQLITE_NOMEM);
  SET_EXC(res, NULL);

  if(res!=SQLITE_OK)
    return NULL;

  Py_RETURN_NONE;
}

static PyObject *
initialize(void)
{
  int res;

  res=sqlite3_initialize();
  APSW_FAULT_INJECT(InitializeFail, ,res=SQLITE_NOMEM);
  SET_EXC(res, NULL);

  if(res!=SQLITE_OK)
    return NULL;

  Py_RETURN_NONE;
}

static PyObject *
sqliteshutdown(void)
{
  int res;
  
  APSW_FAULT_INJECT(ShutdownFail, res=sqlite3_shutdown(), res=SQLITE_NOMEM);
  SET_EXC(res, NULL);

  if(res!=SQLITE_OK)
    return NULL;

  Py_RETURN_NONE;
}

#ifdef EXPERIMENTAL
static PyObject *
config(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int res, optdup;
  long opt;

  if(PyTuple_GET_SIZE(args)<1 || !PyIntLong_Check(PyTuple_GET_ITEM(args, 0)))
    {
      PyErr_Format(PyExc_TypeError, "There should be at least one argument with the first being a number");
      return NULL;
    }
  opt=PyIntLong_AsLong(PyTuple_GET_ITEM(args,0));
  if(PyErr_Occurred())
    return NULL;

  switch(opt)
    {
    case SQLITE_CONFIG_SINGLETHREAD:
    case SQLITE_CONFIG_MULTITHREAD:
    case SQLITE_CONFIG_SERIALIZED:
      if(!PyArg_ParseTuple(args, "i", &optdup))
        return NULL;
      assert(opt==optdup);
      res=sqlite3_config( (int)opt );
      break;
      
    case SQLITE_CONFIG_MEMSTATUS:
      {
        int boolval;
        if(!PyArg_ParseTuple(args, "ii", &optdup, &boolval))
          return NULL;
        assert(opt==optdup);
        res=sqlite3_config( (int)opt, boolval);
        break;
      }
      
    default:
      PyErr_Format(PyExc_TypeError, "Unknown config type %d", (int)opt);
      return NULL;
    }

  SET_EXC(res, NULL);

  if(res!=SQLITE_OK)
    return NULL;

  Py_RETURN_NONE;
}
#endif /* EXPERIMENTAL */

static PyObject*
memoryused(void)
{
  return PyLong_FromLongLong(sqlite3_memory_used());
}

static PyObject*
memoryhighwater(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int reset=0;

  if(!PyArg_ParseTuple(args, "|i:memoryhighwater(reset=False)", &reset))
    return NULL;

  return PyLong_FromLongLong(sqlite3_memory_highwater(reset));
}

static PyObject*
softheaplimit(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int limit;

  if(!PyArg_ParseTuple(args, "i", &limit))
    return NULL;

  sqlite3_soft_heap_limit(limit);

  Py_RETURN_NONE;
}

static PyObject*
randomness(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int amount;
  PyObject *bytes;

  if(!PyArg_ParseTuple(args, "i", &amount))
    return NULL;
  if(amount<0)
    {
      PyErr_Format(PyExc_ValueError, "Can't have negative number of bytes");
      return NULL;
    }
  bytes=PyBytes_FromStringAndSize(NULL, amount);
  if(!bytes) return bytes;
  sqlite3_randomness(amount, PyBytes_AS_STRING(bytes));
  return bytes;
}

static PyObject*
releasememory(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int amount;

  if(!PyArg_ParseTuple(args, "i", &amount))
    return NULL;

  return PyInt_FromLong(sqlite3_release_memory(amount));
}

static PyObject *
status(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  int res, op, current=0, highwater=0, reset=0;

  if(!PyArg_ParseTuple(args, "i|i:status(op, reset=False)", &op, &reset))
    return NULL;

  res=sqlite3_status(op, &current, &highwater, reset);
  SET_EXC(res, NULL);

  if(res!=SQLITE_OK)
    return NULL;

  return Py_BuildValue("(ii)", current, highwater);
}

static PyObject *
vfsnames(APSW_ARGUNUSED PyObject *self)
{
  PyObject *result=NULL, *str=NULL;
  sqlite3_vfs *vfs=sqlite3_vfs_find(0);

  result=PyList_New(0);
  if(!result) goto error;

  while(vfs)
    {
      APSW_FAULT_INJECT(vfsnamesfails, 
                        str=convertutf8string(vfs->zName),
                        str=PyErr_NoMemory());
      if(!str) goto error;
      if(PyList_Append(result, str)) goto error;
      Py_DECREF(str);
      vfs=vfs->pNext;
    }
  return result;

 error:
  Py_XDECREF(str);
  Py_XDECREF(result);
  return NULL;
}

static PyObject *
getapswexceptionfor(APSW_ARGUNUSED PyObject *self, PyObject *pycode)
{
  int code, i;
  PyObject *result=NULL;

  if(!PyIntLong_Check(pycode))
    {
      PyErr_Format(PyExc_TypeError, "Argument should be an integer");
      return NULL;
    }
  code=PyIntLong_AsLong(pycode);
  if(PyErr_Occurred()) return NULL;

  for(i=0;exc_descriptors[i].name;i++)
    if (exc_descriptors[i].code==(code&0xff))
      {
        result=PyObject_CallObject(exc_descriptors[i].cls, NULL);
        if(!result) return result;
        break;
      }
  if(!result)
    {
      PyErr_Format(PyExc_ValueError, "%d is not a known error code", code);
      return result;
    }

  PyObject_SetAttrString(result, "extendedresult", PyInt_FromLong(code));
  PyObject_SetAttrString(result, "result", PyInt_FromLong(code&0xff));
  return result;
}

#if defined(APSW_TESTFIXTURES) && defined(APSW_USE_SQLITE_AMALGAMATION)
/* a routine to reset the random number generator so that we can test xRandomness */
static PyObject *
apsw_test_reset_rng(APSW_ARGUNUSED PyObject *self)
{
  /* See sqlite3PrngResetState in sqlite's random.c */
  GLOBAL(struct sqlite3PrngType, sqlite3Prng).isInit = 0;

  Py_RETURN_NONE;
}
#endif

#ifdef APSW_TESTFIXTURES
/* xGetLastError isn't actually called anywhere by SQLite so add a
   manual way of doing so
   http://www.sqlite.org/cvstrac/tktview?tn=3337 */

static PyObject *
apsw_call_xGetLastError(APSW_ARGUNUSED PyObject *self, PyObject *args)
{
  char *vfsname;
  int bufsize;
  PyObject *resultbuffer=NULL;
  sqlite3_vfs *vfs;
  int res=-1;

  if(!PyArg_ParseTuple(args, "esi", STRENCODING, &vfsname, &bufsize))
    return NULL;

  vfs=sqlite3_vfs_find(vfsname);
  if(!vfs) goto finally;

  resultbuffer=PyBytes_FromStringAndSize(NULL, bufsize);
  if(!resultbuffer) goto finally;

  memset(PyBytes_AS_STRING(resultbuffer), 0, PyBytes_GET_SIZE(resultbuffer));

  res=vfs->xGetLastError(vfs, bufsize, PyBytes_AS_STRING(resultbuffer));

 finally:
  if(vfsname)
    PyMem_Free(vfsname);

  return resultbuffer?Py_BuildValue("Ni", resultbuffer, res):NULL;
}

static PyObject *
apsw_fini(APSW_ARGUNUSED PyObject *self)
{
  APSWBuffer_fini();

  Py_RETURN_NONE;
}
#endif


static PyMethodDef module_methods[] = {
  {"sqlitelibversion", (PyCFunction)getsqliteversion, METH_NOARGS,
   "Return the version of the SQLite library"},
  {"apswversion", (PyCFunction)getapswversion, METH_NOARGS,
   "Return the version of the APSW wrapper"},
  {"vfsnames", (PyCFunction)vfsnames, METH_NOARGS,
   "Returns list of vfs names"},
  {"enablesharedcache", (PyCFunction)enablesharedcache, METH_VARARGS,
   "Sets shared cache semantics for this thread"},
  {"initialize", (PyCFunction)initialize, METH_NOARGS,
   "Initialize SQLite library"},
  {"shutdown", (PyCFunction)sqliteshutdown, METH_NOARGS,
   "Shutdown SQLite library"},
#ifdef EXPERIMENTAL
  {"config", (PyCFunction)config, METH_VARARGS,
   "Calls sqlite3_config"},
#endif
  {"memoryused", (PyCFunction)memoryused, METH_NOARGS,
   "Current SQLite memory in use"},
  {"memoryhighwater", (PyCFunction)memoryhighwater, METH_VARARGS,
   "Most amount of memory used"},
  {"status", (PyCFunction)status, METH_VARARGS,
   "Gets various SQLite counters"},
  {"softheaplimit", (PyCFunction)softheaplimit, METH_VARARGS,
   "Sets soft limit on SQLite memory usage"},
  {"releasememory", (PyCFunction)releasememory, METH_VARARGS,
   "Attempts to free specified amount of memory"},
  {"randomness", (PyCFunction)randomness, METH_VARARGS,
   "Obtains random bytes"},
  {"exceptionfor", (PyCFunction)getapswexceptionfor, METH_O,
   "Returns exception instance corresponding to supplied sqlite error code"},
#if defined(APSW_TESTFIXTURES) && defined(APSW_USE_SQLITE_AMALGAMATION)
  {"test_reset_rng", (PyCFunction)apsw_test_reset_rng, METH_NOARGS,
   "Resets random number generator so we can test vfs xRandomness"},
#endif
#ifdef APSW_TESTFIXTURES
  {"test_call_xGetLastError", (PyCFunction)apsw_call_xGetLastError, METH_VARARGS,
   "Calls xGetLastError routine"},
  {"_fini", (PyCFunction)apsw_fini, METH_NOARGS,
   "Frees all caches and recycle lists"},
#endif
  {0, 0, 0, 0}  /* Sentinel */
};



#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef apswmoduledef={
  PyModuleDef_HEAD_INIT,
  "apsw", 
  NULL,
  -1,
  module_methods,
  0,
  0,
  0,
  0,
};
#endif


PyMODINIT_FUNC
#if PY_MAJOR_VERSION < 3
initapsw(void) 
#else
PyInit_apsw(void)
#endif
{
    PyObject *m=NULL;
    PyObject *thedict=NULL;
    const char *mapping_name=NULL;
    PyObject *hooks;
    unsigned int i;

    assert(sizeof(int)==4);             /* we expect 32 bit ints */
    assert(sizeof(long long)==8);             /* we expect 64 bit long long */

    /* Check SQLite was compiled with thread safety */
    if(!sqlite3_threadsafe())
      {
        PyErr_Format(PyExc_EnvironmentError, "SQLite was compiled without thread safety and cannot be used.");
        goto fail;
      }

    if (PyType_Ready(&ConnectionType) < 0
        || PyType_Ready(&APSWCursorType) < 0
        || PyType_Ready(&ZeroBlobBindType) <0
        || PyType_Ready(&APSWBlobType) <0
        || PyType_Ready(&APSWVFSType) <0
        || PyType_Ready(&APSWVFSFileType) <0
        || PyType_Ready(&APSWStatementType) <0
        || PyType_Ready(&APSWBufferType) <0
        )
      goto fail;

    /* ensure threads are available */
    PyEval_InitThreads();

#if PY_MAJOR_VERSION < 3
    m = apswmodule = Py_InitModule3("apsw", module_methods,
                       "Another Python SQLite Wrapper.");
#else
    m = apswmodule = PyModule_Create(&apswmoduledef);
#endif

    if (m == NULL)  goto fail;

    if(init_exceptions(m)) goto fail;

    Py_INCREF(&ConnectionType);
    PyModule_AddObject(m, "Connection", (PyObject *)&ConnectionType);
    
    /* we don't add cursor to the module since users shouldn't be able to instantiate them directly */
    
    Py_INCREF(&ZeroBlobBindType);
    PyModule_AddObject(m, "zeroblob", (PyObject *)&ZeroBlobBindType);

    Py_INCREF(&APSWVFSType);
    PyModule_AddObject(m, "VFS", (PyObject*)&APSWVFSType);
    Py_INCREF(&APSWVFSFileType);
    PyModule_AddObject(m, "VFSFile", (PyObject*)&APSWVFSFileType);
    
    hooks=PyList_New(0);
    if(!hooks) goto fail;
    PyModule_AddObject(m, "connection_hooks", hooks);

    /* Version number */
    PyModule_AddIntConstant(m, "SQLITE_VERSION_NUMBER", SQLITE_VERSION_NUMBER);
    

    /* add in some constants and also put them in a corresponding mapping dictionary */

    /* sentinel should be a number that doesn't exist */
#define SENTINEL -786343
#define DICT(n) {n, SENTINEL}
#define END {NULL, 0}
#define ADDINT(n) {#n, n}

    struct { const char *name; int value; } integers[]={
      DICT("mapping_authorizer_return"),
      ADDINT(SQLITE_DENY),
      ADDINT(SQLITE_IGNORE),
      ADDINT(SQLITE_OK),
      END,
      
      DICT("mapping_authorizer_function"),
      ADDINT(SQLITE_CREATE_INDEX),
      ADDINT(SQLITE_CREATE_TABLE),
      ADDINT(SQLITE_CREATE_TEMP_INDEX),
      ADDINT(SQLITE_CREATE_TEMP_TABLE),
      ADDINT(SQLITE_CREATE_TEMP_TRIGGER),
      ADDINT(SQLITE_CREATE_TEMP_VIEW),
      ADDINT(SQLITE_CREATE_TRIGGER),
      ADDINT(SQLITE_CREATE_VIEW),
      ADDINT(SQLITE_DELETE),
      ADDINT(SQLITE_DROP_INDEX),
      ADDINT(SQLITE_DROP_TABLE),
      ADDINT(SQLITE_DROP_TEMP_INDEX),
      ADDINT(SQLITE_DROP_TEMP_TABLE),
      ADDINT(SQLITE_DROP_TEMP_TRIGGER),
      ADDINT(SQLITE_DROP_TEMP_VIEW),
      ADDINT(SQLITE_DROP_TRIGGER),
      ADDINT(SQLITE_DROP_VIEW),
      ADDINT(SQLITE_INSERT),
      ADDINT(SQLITE_PRAGMA),
      ADDINT(SQLITE_READ),
      ADDINT(SQLITE_SELECT),
      ADDINT(SQLITE_TRANSACTION),
      ADDINT(SQLITE_UPDATE),
      ADDINT(SQLITE_ATTACH),
      ADDINT(SQLITE_DETACH),
      ADDINT(SQLITE_ALTER_TABLE),
      ADDINT(SQLITE_REINDEX),
      ADDINT(SQLITE_COPY),
      ADDINT(SQLITE_ANALYZE),
      ADDINT(SQLITE_CREATE_VTABLE),
      ADDINT(SQLITE_DROP_VTABLE),
      ADDINT(SQLITE_FUNCTION),
      END,

      /* vtable best index constraints */
#if defined(SQLITE_INDEX_CONSTRAINT_EQ) && defined(SQLITE_INDEX_CONSTRAINT_MATCH)
      DICT("mapping_bestindex_constraints"),
      ADDINT(SQLITE_INDEX_CONSTRAINT_EQ),
      ADDINT(SQLITE_INDEX_CONSTRAINT_GT),
      ADDINT(SQLITE_INDEX_CONSTRAINT_LE),
      ADDINT(SQLITE_INDEX_CONSTRAINT_LT),
      ADDINT(SQLITE_INDEX_CONSTRAINT_GE),
      ADDINT(SQLITE_INDEX_CONSTRAINT_MATCH),
      END,
#endif /* constraints */

    /* extendended result codes */
      DICT("mapping_extended_result_codes"),
      ADDINT(SQLITE_IOERR_READ),
      ADDINT(SQLITE_IOERR_SHORT_READ),
      ADDINT(SQLITE_IOERR_WRITE),
      ADDINT(SQLITE_IOERR_FSYNC),
      ADDINT(SQLITE_IOERR_DIR_FSYNC),
      ADDINT(SQLITE_IOERR_TRUNCATE),
      ADDINT(SQLITE_IOERR_FSTAT),
      ADDINT(SQLITE_IOERR_UNLOCK),
      ADDINT(SQLITE_IOERR_RDLOCK),
      ADDINT(SQLITE_IOERR_DELETE),
      ADDINT(SQLITE_IOERR_BLOCKED),
      ADDINT(SQLITE_IOERR_NOMEM),
      ADDINT(SQLITE_IOERR_ACCESS),
      ADDINT(SQLITE_IOERR_CHECKRESERVEDLOCK),
      ADDINT(SQLITE_IOERR_LOCK),
      END,

    /* error codes */
      DICT("mapping_result_codes"),
      ADDINT(SQLITE_OK),
      ADDINT(SQLITE_ERROR),
      ADDINT(SQLITE_INTERNAL),
      ADDINT(SQLITE_PERM),
      ADDINT(SQLITE_ABORT),
      ADDINT(SQLITE_BUSY),
      ADDINT(SQLITE_LOCKED),
      ADDINT(SQLITE_NOMEM),
      ADDINT(SQLITE_READONLY),
      ADDINT(SQLITE_INTERRUPT),
      ADDINT(SQLITE_IOERR),
      ADDINT(SQLITE_CORRUPT),
      ADDINT(SQLITE_FULL),
      ADDINT(SQLITE_CANTOPEN),
      ADDINT(SQLITE_PROTOCOL),
      ADDINT(SQLITE_EMPTY),
      ADDINT(SQLITE_SCHEMA),
      ADDINT(SQLITE_CONSTRAINT),
      ADDINT(SQLITE_MISMATCH),
      ADDINT(SQLITE_MISUSE),
      ADDINT(SQLITE_NOLFS),
      ADDINT(SQLITE_AUTH),
      ADDINT(SQLITE_FORMAT),
      ADDINT(SQLITE_RANGE),
      ADDINT(SQLITE_NOTADB),
      END,

      /* open flags */
      DICT("mapping_open_flags"),
      ADDINT(SQLITE_OPEN_READONLY),
      ADDINT(SQLITE_OPEN_READWRITE),
      ADDINT(SQLITE_OPEN_CREATE),
      ADDINT(SQLITE_OPEN_DELETEONCLOSE),
      ADDINT(SQLITE_OPEN_EXCLUSIVE),
      ADDINT(SQLITE_OPEN_MAIN_DB),
      ADDINT(SQLITE_OPEN_TEMP_DB),
      ADDINT(SQLITE_OPEN_TRANSIENT_DB),
      ADDINT(SQLITE_OPEN_MAIN_JOURNAL),
      ADDINT(SQLITE_OPEN_TEMP_JOURNAL),
      ADDINT(SQLITE_OPEN_SUBJOURNAL),
      ADDINT(SQLITE_OPEN_MASTER_JOURNAL),
      ADDINT(SQLITE_OPEN_NOMUTEX),
      ADDINT(SQLITE_OPEN_FULLMUTEX),
      END,

      /* limits */
      DICT("mapping_limits"),
      ADDINT(SQLITE_LIMIT_LENGTH),
      ADDINT(SQLITE_LIMIT_SQL_LENGTH),
      ADDINT(SQLITE_LIMIT_COLUMN),
      ADDINT(SQLITE_LIMIT_EXPR_DEPTH),
      ADDINT(SQLITE_LIMIT_COMPOUND_SELECT),
      ADDINT(SQLITE_LIMIT_VDBE_OP),
      ADDINT(SQLITE_LIMIT_FUNCTION_ARG),
      ADDINT(SQLITE_LIMIT_ATTACHED),
      ADDINT(SQLITE_LIMIT_LIKE_PATTERN_LENGTH),
      ADDINT(SQLITE_LIMIT_VARIABLE_NUMBER),
      /* We don't include the MAX limits - see http://code.google.com/p/apsw/issues/detail?id=17 */
      END,

      DICT("mapping_config"),
      ADDINT(SQLITE_CONFIG_SINGLETHREAD),
      ADDINT(SQLITE_CONFIG_MULTITHREAD),
      ADDINT(SQLITE_CONFIG_SERIALIZED),
      ADDINT(SQLITE_CONFIG_MALLOC),
      ADDINT(SQLITE_CONFIG_GETMALLOC),
      ADDINT(SQLITE_CONFIG_SCRATCH),
      ADDINT(SQLITE_CONFIG_PAGECACHE),
      ADDINT(SQLITE_CONFIG_HEAP),
      ADDINT(SQLITE_CONFIG_MEMSTATUS),
      ADDINT(SQLITE_CONFIG_MUTEX),
      ADDINT(SQLITE_CONFIG_GETMUTEX),
      ADDINT(SQLITE_CONFIG_CHUNKALLOC),
      ADDINT(SQLITE_CONFIG_LOOKASIDE),
      END,

      DICT("mapping_db_config"),
      ADDINT(SQLITE_DBCONFIG_LOOKASIDE),
      END,

      DICT("mapping_status"),
      ADDINT(SQLITE_STATUS_MEMORY_USED),
      ADDINT(SQLITE_STATUS_PAGECACHE_USED),
      ADDINT(SQLITE_STATUS_PAGECACHE_OVERFLOW),
      ADDINT(SQLITE_STATUS_SCRATCH_USED),
      ADDINT(SQLITE_STATUS_SCRATCH_OVERFLOW),
      ADDINT(SQLITE_STATUS_MALLOC_SIZE),
      ADDINT(SQLITE_STATUS_PARSER_STACK),
      ADDINT(SQLITE_STATUS_PAGECACHE_SIZE),
      ADDINT(SQLITE_STATUS_SCRATCH_SIZE),
      END,

      DICT("mapping_db_status"),
      ADDINT(SQLITE_DBSTATUS_LOOKASIDE_USED),
      END,

      DICT("mapping_locking_level"),
      ADDINT(SQLITE_LOCK_NONE),
      ADDINT(SQLITE_LOCK_SHARED),
      ADDINT(SQLITE_LOCK_RESERVED),
      ADDINT(SQLITE_LOCK_PENDING),
      ADDINT(SQLITE_LOCK_EXCLUSIVE),
      END,

      DICT("mapping_access"),
      ADDINT(SQLITE_ACCESS_EXISTS),
      ADDINT(SQLITE_ACCESS_READWRITE),
      ADDINT(SQLITE_ACCESS_READ),
      END,

      DICT("mapping_device_characteristics"),
      ADDINT(SQLITE_IOCAP_ATOMIC),
      ADDINT(SQLITE_IOCAP_ATOMIC512),
      ADDINT(SQLITE_IOCAP_ATOMIC1K),
      ADDINT(SQLITE_IOCAP_ATOMIC2K),
      ADDINT(SQLITE_IOCAP_ATOMIC4K),
      ADDINT(SQLITE_IOCAP_ATOMIC8K),
      ADDINT(SQLITE_IOCAP_ATOMIC16K),
      ADDINT(SQLITE_IOCAP_ATOMIC32K),
      ADDINT(SQLITE_IOCAP_ATOMIC64K),
      ADDINT(SQLITE_IOCAP_SAFE_APPEND),
      ADDINT(SQLITE_IOCAP_SEQUENTIAL),
      END,

      DICT("mapping_sync"),
      ADDINT(SQLITE_SYNC_NORMAL),
      ADDINT(SQLITE_SYNC_FULL),
      ADDINT(SQLITE_SYNC_DATAONLY),
      END};
 
 
 for(i=0;i<sizeof(integers)/sizeof(integers[0]); i++)
   {
     const char *name=integers[i].name;
     int value=integers[i].value;
     PyObject *pyname;
     PyObject *pyvalue;

     /* should be at dict */
     if(!thedict)
       {
         assert(value==SENTINEL);
         assert(mapping_name==NULL);
         mapping_name=name;
         thedict=PyDict_New();
         continue;
       }
     /* at END? */
     if(!name)
       {
         assert(thedict);
         PyModule_AddObject(m, mapping_name, thedict);
         thedict=NULL;
         mapping_name=NULL;
         continue;
       }
     /* regular ADDINT */
     PyModule_AddIntConstant(m, name, value);
     pyname=MAKESTR(name);
     pyvalue=PyInt_FromLong(value);
     if(!pyname || !pyvalue) goto fail;
     PyDict_SetItem(thedict, pyname, pyvalue);
     PyDict_SetItem(thedict, pyvalue, pyname);
     Py_DECREF(pyname);
     Py_DECREF(pyvalue);
   }
 /* should have ended with END so thedict should be NULL */
 assert(thedict==NULL);

 if(!PyErr_Occurred())
      {
        return
#if PY_MAJOR_VERSION >= 3
          m
#endif
          ;
      }

 fail:
    Py_XDECREF(m);
    return 
#if PY_MAJOR_VERSION >= 3
          NULL
#endif
          ;
}

#ifdef APSW_TESTFIXTURES
static int
APSW_Should_Fault(const char *name)
{
  PyGILState_STATE gilstate;
  PyObject *faultdict=NULL, *truthval=NULL, *value=NULL;
  int res=0;

  gilstate=PyGILState_Ensure();

  if(!PyObject_HasAttrString(apswmodule, "faultdict"))
    PyObject_SetAttrString(apswmodule, "faultdict", PyDict_New());

  value=MAKESTR(name);
  
  faultdict=PyObject_GetAttrString(apswmodule, "faultdict");
  
  truthval=PyDict_GetItem(faultdict, value);
  if(!truthval)
    goto finally;

  /* set false if present - one shot firing */
  PyDict_SetItem(faultdict, value, Py_False);
  res=PyObject_IsTrue(truthval);

 finally:
  Py_XDECREF(value);
  Py_XDECREF(faultdict);

  PyGILState_Release(gilstate);
  return res;
}
#endif