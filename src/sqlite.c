#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"

#include "helpers/helpers.h"


#include <sqlite3.h>


/* global objects */

static PyObject *Error = NULL;


/* -------------------------------------------------------------------------- */

#define debug(fmt, ...) printf("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)


#define __sqlite_wrap__(t, fn, ...) (t)fn(__VA_ARGS__)

#define __sqlite_gil_wrap__(t, fn, ...) \
    ( \
        { \
            PyThreadState *_save_ = PyEval_SaveThread(); \
            t _result_ = __sqlite_wrap__(t, fn, __VA_ARGS__); \
            PyEval_RestoreThread(_save_); \
            (_result_); \
        } \
    )


#define __sqlite_db_errcode__(...) \
    __sqlite_wrap__(int, sqlite3_errcode, __VA_ARGS__)
#define __sqlite_db_extderr__(...) \
    __sqlite_wrap__(int, sqlite3_extended_errcode, __VA_ARGS__)
#define __sqlite_db_errmsg__(...) \
    __sqlite_wrap__(const char *, sqlite3_errmsg, __VA_ARGS__)

#define __sqlite_db_open__(...) \
    __sqlite_gil_wrap__(int, sqlite3_open_v2, __VA_ARGS__)
#define __sqlite_db_close__(...) \
    __sqlite_gil_wrap__(int, sqlite3_close_v2, __VA_ARGS__)
#define __sqlite_db_readonly__(...) \
    __sqlite_wrap__(long, sqlite3_db_readonly, __VA_ARGS__)


#define __sqlite_stmt_prepare__(...) \
    __sqlite_gil_wrap__(int, sqlite3_prepare_v2, __VA_ARGS__)
#define __sqlite_stmt_step__(...) \
    __sqlite_gil_wrap__(int, sqlite3_step, __VA_ARGS__)
#define __sqlite_stmt_finalize__(...) \
    __sqlite_gil_wrap__(int, sqlite3_finalize, __VA_ARGS__)


#define __sqlite_column_long__(...) \
    __sqlite_wrap__(long long, sqlite3_column_int64, __VA_ARGS__)
#define __sqlite_column_float__(...) \
    __sqlite_wrap__(double, sqlite3_column_double, __VA_ARGS__)
#define __sqlite_column_unicode__(...) \
    __sqlite_wrap__(const char *, sqlite3_column_text, __VA_ARGS__)
#define __sqlite_column_bytes__(...) \
    __sqlite_wrap__(const char *, sqlite3_column_blob, __VA_ARGS__)
#define __sqlite_column_size__(...) \
    __sqlite_wrap__(Py_ssize_t, sqlite3_column_bytes, __VA_ARGS__)

#define __sqlite_column_name__(...) \
    __sqlite_wrap__(const char *, sqlite3_column_name, __VA_ARGS__)
#define __sqlite_column_type__(...) \
    __sqlite_wrap__(int, sqlite3_column_type, __VA_ARGS__)
#define __sqlite_column_count__(...) \
    __sqlite_wrap__(int, sqlite3_column_count, __VA_ARGS__)


#define __sqlite_bind_none__(...) \
    __sqlite_wrap__(int, sqlite3_bind_null, __VA_ARGS__)
#define __sqlite_bind_bool__(...) \
    __sqlite_wrap__(int, sqlite3_bind_int, __VA_ARGS__)
#define __sqlite_bind_long__(...) \
    __sqlite_wrap__(int, sqlite3_bind_int64, __VA_ARGS__)
#define __sqlite_bind_float__(...) \
    __sqlite_wrap__(int, sqlite3_bind_double, __VA_ARGS__)
#define __sqlite_bind_unicode__(...) \
    __sqlite_wrap__(int, sqlite3_bind_text, __VA_ARGS__)
#define __sqlite_bind_bytes__(...) \
    __sqlite_wrap__(int, sqlite3_bind_blob, __VA_ARGS__)

#define __sqlite_bind_count__(...) \
    __sqlite_wrap__(int, sqlite3_bind_parameter_count, __VA_ARGS__)

#define __sqlite_bind_true__(...) __sqlite_bind_bool__(__VA_ARGS__, 1)
#define __sqlite_bind_false__(...) __sqlite_bind_bool__(__VA_ARGS__, 0)


#define __params_size__ PySequence_Fast_GET_SIZE
#define __params_item__ PySequence_Fast_GET_ITEM


/* --------------------------------------------------------------------------
   Row
   -------------------------------------------------------------------------- */

/* RowType_Type.tp_dealloc */
static void
RowType_tp_dealloc(PyTypeObject *type)
{

    printf("RowType_tp_dealloc\n");

    PyMemberDef *member = type->tp_members;

    if (member != NULL) {
        for (; member->name; member++) {
            printf("\tmember->name: %s\n", member->name);
            free((void *)member->name);
            member->name = NULL;
        }
    }
    PyType_Type.tp_dealloc((PyObject *)type);
}


static PyTypeObject RowType_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mood.sqlite.RowType",
    .tp_dealloc = (destructor)RowType_tp_dealloc,
};


static inline char *
__strdup__(const char *s)
{
    return (s) ? strdup(s) : NULL;
}

static PyTypeObject *
__new_rowtype__(sqlite3_stmt *stmt, int len)
{
    static const char *_name_ = "mood.sqlite.Row";
    PyStructSequence_Field *_fields_ = NULL;
    PyStructSequence_Desc *_desc_ = NULL;
    PyTypeObject *_type_ = NULL;
    int i;

    _fields_ = PyMem_Calloc((len + 1), sizeof(PyStructSequence_Field));
    if (!_fields_) {
        goto fail;
    }
    for (i = 0; i < len; ++i) {
        if (!(_fields_[i].name = __strdup__(__sqlite_column_name__(stmt, i)))) {
            goto fail;
        }
    }

    _desc_ = PyMem_Calloc(1, sizeof(PyStructSequence_Desc));
    if (!_desc_) {
        goto fail;
    }
    _desc_->name = _name_;
    _desc_->fields = _fields_;
    _desc_->n_in_sequence = len;

    if ((_type_ = PyStructSequence_NewType(_desc_))) {
        Py_SET_TYPE(_type_, &RowType_Type);
        PyMem_Free(_desc_);
        PyMem_Free(_fields_);
        return _type_;
    }

fail:
    if (_desc_) {
        PyMem_Free(_desc_);
        _desc_ = NULL;
    }
    if (_fields_) {
        for (i = 0; i < len; ++i) {
            if(_fields_[i].name) {
                free((void *)_fields_[i].name);
                _fields_[i].name = NULL;
            }
        }
        PyMem_Free(_fields_);
        _fields_ = NULL;
    }
    if (!PyErr_Occurred()) {
        PyErr_NoMemory();
    }
    return NULL;
}


/* --------------------------------------------------------------------------
   Database
   -------------------------------------------------------------------------- */

/* Database */
typedef struct {
    PyObject_HEAD
    PyObject *filename;
    sqlite3 *db;
} Database;


static int
__db_err_occurred__(Database *self)
{
    switch(__sqlite_db_errcode__(self->db)) {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:
            return 0;
        default:
            return 1;
    }
}


static void
__db_set_err__(Database *self)
{
    PyObject *_filename_ = NULL;

    if (self->filename) {
        if ((_filename_ = _PyUnicode_DecodeFSDefault(self->filename))) {
            PyErr_Format(
                Error,
                "[%i] %s: %R",
                __sqlite_db_extderr__(self->db),
                __sqlite_db_errmsg__(self->db),
                _filename_
            );
            Py_DECREF(_filename_);
        }
    }
    else {
        PyErr_Format(
            Error,
            "[%i] %s",
            __sqlite_db_extderr__(self->db),
            __sqlite_db_errmsg__(self->db)
        );
    }
}


static void
_PyErr_SetFromDb(Database *self)
{
    PyObject *exc_type, *exc_value, *exc_traceback;
    PyObject *err = PyErr_Occurred();

    if (err) {
        PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
    }
    __db_set_err__(self);
    if (err) {
        _PyErr_ChainExceptions(exc_type, exc_value, exc_traceback);
    }
}


/* -------------------------------------------------------------------------- */

static Database *
__db_alloc__(PyTypeObject *type)
{
    Database *self = NULL;

    if ((self = PyObject_GC_NEW(Database, type))) {
        self->filename = NULL;
        self->db = NULL;
    }
    return self;
}


static int
__db_open__(Database *self, int flags)
{
    int rc = SQLITE_OK;

    rc = __sqlite_db_open__(
        PyBytes_AS_STRING(self->filename), &self->db, flags, NULL
    );
    if (rc) {
        _PyErr_SetFromDb(self);
        if (self->db) {
            if (__sqlite_db_close__(self->db)) {
                _PyErr_SetFromDb(self);
            }
            self->db = NULL;
        }
    }
    return (rc != SQLITE_OK) ? -1 : 0;
}


static int
__db_close__(Database *self)
{

    printf("closing db...\n");

    int rc = SQLITE_OK;

    if (self->db) {
        if ((rc = __sqlite_db_close__(self->db))) {
            _PyErr_SetFromDb(self);
        }
        self->db = NULL;
    }

    printf("\t...done\n");

    return (rc != SQLITE_OK) ? -1 : 0;
}


/* Database_Type ------------------------------------------------------------ */

/* Database_Type.tp_finalize */
static void
Database_tp_finalize(Database *self)
{
    PyObject *exc_type, *exc_value, *exc_traceback;

    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
    if (__db_close__(self)) {
        PyErr_WriteUnraisable((PyObject *)self);
    }
    PyErr_Restore(exc_type, exc_value, exc_traceback);
}


/* Database_Type.tp_traverse */
static int
Database_tp_traverse(Database *self, visitproc visit, void *arg)
{
    Py_VISIT(self->filename);
    return 0;
}


/* Database_Type.tp_clear */
static int
Database_tp_clear(Database *self)
{
    Py_CLEAR(self->filename);
    return 0;
}


/* Database_Type.tp_dealloc */
static void
Database_tp_dealloc(Database *self)
{
    if (PyObject_CallFinalizerFromDealloc((PyObject *)self)) {
        return;
    }
    PyObject_GC_UnTrack(self);
    Database_tp_clear(self);
    PyObject_GC_Del(self);
}


/* Database_Type.tp_repr */
static PyObject *
Database_tp_repr(Database *self)
{
    PyObject *_filename_ = NULL, *result = NULL;

    if ((_filename_ = _PyUnicode_DecodeFSDefault(self->filename))) {
        result = PyUnicode_FromFormat(
            "<%s(%R)>", Py_TYPE(self)->tp_name, _filename_
        );
        Py_DECREF(_filename_);
    }
    return result;
}


/* Database_Type.tp_new */
static PyObject *
Database_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    int flags = SQLITE_OPEN_READONLY;
    Database *self = NULL;

    if ((self = __db_alloc__(type))) {
        PyObject_GC_Track(self);
        if (
            !PyArg_ParseTuple(
                args,
                "O&|i:__new__",
                PyUnicode_FSConverter,
                &self->filename,
                &flags
            ) ||
            __db_open__(self, flags | SQLITE_OPEN_URI | SQLITE_OPEN_EXRESCODE)
        ) {
            Py_CLEAR(self);
        }
    }
    return (PyObject *)self;
}


/* Database_Type.tp_init */
static int
Database_tp_init(Database *self, PyObject *args, PyObject *kwargs)
{
    return 0;
}


/* methods/attributes ------------------------------------------------------- */

static PyObject *
__column_value__(Database *self, sqlite3_stmt *stmt, int i)
{
    PyObject *value = NULL;

    switch (__sqlite_column_type__(stmt, i)) {
        case SQLITE_INTEGER:
            {
                long long _value_ = __sqlite_column_long__(stmt, i);
                if (!__db_err_occurred__(self)) {
                    value = PyLong_FromLongLong(_value_);
                }
            }
            break;
        case SQLITE_FLOAT:
            {
                double _value_ = __sqlite_column_float__(stmt, i);
                if (!__db_err_occurred__(self)) {
                    value = PyFloat_FromDouble(_value_);
                }
            }
            break;
        case SQLITE_TEXT:
            {
                const char *_value_ = __sqlite_column_unicode__(stmt, i);
                if (_value_) {
                    value = PyUnicode_FromStringAndSize(
                        _value_, __sqlite_column_size__(stmt, i)
                    );
                }
            }
            break;
        case SQLITE_BLOB:
            {
                const char *_value_ = __sqlite_column_bytes__(stmt, i);
                if (_value_) {
                    value = PyBytes_FromStringAndSize(
                        _value_, __sqlite_column_size__(stmt, i)
                    );
                }
            }
            break;
        case SQLITE_NULL:
            value = Py_NewRef(Py_None);
            break;
        default:
            PyErr_SetString(PyExc_TypeError, "unknown sqlite3 datatype");
            break;
    }
    if ((value == NULL) && !PyErr_Occurred()) {
        _PyErr_SetFromDb(self);
    }
    return value;
}

static int
__stmt_row__(
    Database *self,
    sqlite3_stmt *stmt,
    int len,
    PyObject *rows,
    PyTypeObject **rowtype
)
{
    PyObject *row = NULL, *value = NULL;
    PyTypeObject *_type_ = *rowtype;
    int i, rc = -1;

    if (!_type_) {
        _type_ = __new_rowtype__(stmt, len);
        if (!_type_) {
            goto exit;
        }
        else {
            *rowtype = _type_;
        }
    }
    if ((row = PyStructSequence_New(_type_))) {
        for (i = 0; i < len; ++i) {
            if (!(value = __column_value__(self, stmt, i))) {
                goto fail;
            }
            PyStructSequence_SET_ITEM(row, i, value); // steals ref to value
        }
        rc = PyList_Append(rows, row);
fail:
        Py_CLEAR(row);
    }
exit:
    return rc;
}

static int
__stmt_bind_value__(
    Database *self, sqlite3_stmt *stmt, int index, PyObject *value
)
{
    /*printf("__stmt_bind_value__(index=%d, value=", index);
    PyObject_Print(value, stdout, 1);
    printf(")\n");*/

    int rc = -1;

    if (value == Py_None) {
        rc = __sqlite_bind_none__(stmt, index);
    }
    else if (value == Py_True) {
        rc = __sqlite_bind_true__(stmt, index);
    }
    else if (value == Py_False) {
        rc = __sqlite_bind_false__(stmt, index);
    }
    else if (PyLong_CheckExact(value)) {
        long long _value_ = PyLong_AsLongLong(value);
        if ((_value_ != -1) || !PyErr_Occurred()) {
            rc = __sqlite_bind_long__(stmt, index, _value_);
        }
    }
    else if (PyFloat_CheckExact(value)) {
        rc = __sqlite_bind_float__(stmt, index, PyFloat_AS_DOUBLE(value));
    }
    else if (PyUnicode_CheckExact(value)) {
        Py_ssize_t _size_;
        const char *_value_ = PyUnicode_AsUTF8AndSize(value, &_size_);
        if (_value_) {
            rc = __sqlite_bind_unicode__(
                stmt, index, _value_, _size_, SQLITE_STATIC
            );
        }
    }
    else if (PyBytes_CheckExact(value)) {
        rc = __sqlite_bind_bytes__(
            stmt,
            index,
            PyBytes_AS_STRING(value),
            PyBytes_GET_SIZE(value),
            SQLITE_STATIC
        );
    }
    else {
        PyErr_Format(
            PyExc_TypeError,
            "unsupported python type: '%.200s'",
            Py_TYPE(value)->tp_name
        );
    }
    if ((rc != SQLITE_OK) && !PyErr_Occurred()) {
        _PyErr_SetFromDb(self);
    }
    return rc;
}

static int
__stmt_bind_params__(
    Database *self, sqlite3_stmt *stmt, int count, PyObject *params
)
{
    /*printf("__stmt_bind_params__(count=%d, params=", count);
    PyObject_Print(params, stdout, 1);
    printf(")\n");*/

    Py_ssize_t size = __params_size__(params);
    int i;

    for (i = 0; ((i < size) && (i < count)); ++i) {
        if (__stmt_bind_value__(self, stmt, i + 1, __params_item__(params, i))) {
            return -1;
        }
    }
    return 0;
}

static int
__db_execute__(
    Database *self,
    const char *sql,
    const char **tail,
    PyObject *params,
    PyObject **result
)
{
    sqlite3_stmt *stmt = NULL;
    PyObject *rows = NULL;
    PyTypeObject *rowtype = NULL;
    int count = 0, len = 0;

    if (__sqlite_stmt_prepare__(self->db, sql, strlen(sql) + 1, &stmt, tail)) {
        _PyErr_SetFromDb(self);
        return -1;
    }
    if (stmt) {
        if (
            !params ||
            !(count = __sqlite_bind_count__(stmt)) ||
            !__stmt_bind_params__(self, stmt, count, params)
        ) {
            if ((rows = PyList_New(0))) {
                len = __sqlite_column_count__(stmt);
                while (__sqlite_stmt_step__(stmt) == SQLITE_ROW) {
                    if (__stmt_row__(self, stmt, len, rows, &rowtype)) {
                        break;
                    }
                }
                if (!__db_err_occurred__(self) && !PyErr_Occurred()) {
                    *result = Py_NewRef(PyList_GET_SIZE(rows) ? rows : Py_None);
                }
                Py_CLEAR(rowtype);
                Py_CLEAR(rows);
            }
        }
        if (__sqlite_stmt_finalize__(stmt)) {
            _PyErr_SetFromDb(self);
        }
        if (PyErr_Occurred()) {
            return -1;
        }
    }
    return 0;
}


/* Database.execute() */
static int
__params_check__(PyObject *params)
{
    if (PyList_CheckExact(params) || PyTuple_CheckExact(params)) {
        return 1;
    }
    PyErr_Format(
        PyExc_TypeError,
        "parameters must be a list or tuple, not %.200s",
        Py_TYPE(params)->tp_name
    );
    return 0;
}

static int
__params_converter__(PyObject *arg, void *addr)
{
    int res = 0;

    if ((res = __params_check__(arg))) {
        *(PyObject **)addr = arg;
    }
    return res;
}

static PyObject *
Database_execute(Database *self, PyObject *args)
{
    PyObject *result = NULL, *params = NULL, *_params_ = NULL;
    const char *sql = NULL;
    Py_ssize_t size = 0, i = 0;

    if (
        !PyArg_ParseTuple(
            args, "s|O&:execute", &sql, __params_converter__, &params
        )
    ) {
        return NULL;
    }
    size = (params) ? __params_size__(params) : 0;
    do {
        Py_CLEAR(result);
        if (
            (
                (_params_ = (size) ? __params_item__(params, i) : NULL) &&
                !__params_check__(_params_)
            ) ||
            __db_execute__(self, sql, NULL, _params_, &result)
        ) {
            return NULL;
        }
        ++i;
    } while (i < size);
    return (result) ? result : Py_NewRef(Py_None);
}


/* Database.executescript() */
static PyObject *
Database_executescript(Database *self, PyObject *args)
{
    PyObject *results = NULL, *result = NULL;
    const char *sql = NULL, *tail = NULL;

    if (
        !PyArg_ParseTuple(args, "s:execute", &sql) ||
        !(results = PyList_New(0))
    ) {
        return NULL;
    }
    while (sql[0]) {
        if (
            __db_execute__(self, sql, &tail, NULL, &result) ||
            (result && PyList_Append(results, result))
        ) {
            Py_CLEAR(result);
            Py_CLEAR(results);
            break;
        }
        Py_CLEAR(result);
        sql = tail;
    }
    return results;
}


/* Database_Type.tp_methods */
static PyMethodDef Database_tp_methods[] = {
    {"execute", (PyCFunction)Database_execute, METH_VARARGS, NULL},
    {"executescript", (PyCFunction)Database_executescript, METH_VARARGS, NULL},
    {NULL}  /* Sentinel */
};


/* Database.readonly */
static PyObject *
Database_readonly_getter(Database *self, void *closure)
{
    return PyBool_FromLong(__sqlite_db_readonly__(self->db, NULL));
}


/* Database_Type.tp_getsets */
static PyGetSetDef Database_tp_getsets[] = {
    {
        "readonly", (getter)Database_readonly_getter,
        _Py_READONLY_ATTRIBUTE, NULL, NULL
    },
    {NULL}  /* Sentinel */
};


/* Database_Type ------------------------------------------------------------ */

static PyTypeObject Database_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mood.sqlite.Database",
    .tp_basicsize = sizeof(Database),
    .tp_dealloc = (destructor)Database_tp_dealloc,
    .tp_repr = (reprfunc)Database_tp_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_FINALIZE,
    .tp_doc = "Database()",
    .tp_traverse = (traverseproc)Database_tp_traverse,
    .tp_clear = (inquiry)Database_tp_clear,
    .tp_methods = Database_tp_methods,
    .tp_getset = Database_tp_getsets,
    .tp_init = (initproc)Database_tp_init,
    .tp_new = Database_tp_new,
    .tp_finalize = (destructor)Database_tp_finalize,
};


/* --------------------------------------------------------------------------
    module
   -------------------------------------------------------------------------- */

/* sqlite.test() */
static PyObject *
sqlite_test(PyObject *module)
{
    /*static const char *_name_ = "Row";
    PyStructSequence_Field *_fields_ = NULL;
    PyStructSequence_Desc *_desc_ = NULL;
    PyTypeObject *_type_ = NULL;

    _fields_ = PyMem_Calloc(1, sizeof(PyStructSequence_Field));
    if (!_fields_) {
        return PyErr_NoMemory();
    }

    _desc_ = PyMem_Calloc(1, sizeof(PyStructSequence_Desc));
    if (!_desc_) {
        PyMem_Free(_fields_);
        return PyErr_NoMemory();
    }
    _desc_->name = _name_;
    _desc_->fields = _fields_;
    _desc_->n_in_sequence = 0;

    _type_ = PyStructSequence_NewType(_desc_);

    PyMem_Free(_fields_);
    PyMem_Free(_desc_);

    return (PyObject *)_type_;*/

    Py_RETURN_NONE;
}


/* sqlite_def.m_methods */
static PyMethodDef sqlite_m_methods[] = {
    {"test", (PyCFunction)sqlite_test, METH_NOARGS, NULL},
    {NULL}
};


/* sqlite_def.m_slots.Py_mod_exec */
static int
sqlite_m_slots_exec(PyObject *module)
{
    RowType_Type.tp_base = &PyType_Type;
    if (
        PyType_Ready(&RowType_Type) ||
        PyModule_AddStringConstant(module, "__version__", PKG_VERSION) ||
        _PyModule_AddNewException(
            module, "Error", "mood.sqlite", NULL, NULL, &Error
        ) ||

        // Database
        _PyModule_AddType(module, "Database", &Database_Type)||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_READONLY) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_READWRITE) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_CREATE) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_MEMORY) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_NOMUTEX) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_FULLMUTEX) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_SHAREDCACHE) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_PRIVATECACHE) ||
        _PyModule_AddIntMacro(module, SQLITE_OPEN_NOFOLLOW)
    ) {
        return -1;
    }
    return 0;
}


/* sqlite_def.m_slots */
static struct PyModuleDef_Slot sqlite_m_slots[] = {
    {Py_mod_exec, sqlite_m_slots_exec},
    {0, NULL}
};


/* sqlite_def.m_traverse */
static int
sqlite_m_traverse(PyObject *module, visitproc visit, void *arg)
{
    Py_VISIT(Error);
    return 0;
}


/* sqlite_def.m_clear */
static int
sqlite_m_clear(PyObject *module)
{
    Py_CLEAR(Error);
    return 0;
}


/* sqlite_def.m_free */
static void
sqlite_m_free(PyObject *module)
{
    sqlite_m_clear(module);
}


/* sqlite_def */
static PyModuleDef sqlite_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "sqlite",
    .m_doc = "mood sqlite module",
    .m_size = 0,
    .m_methods = sqlite_m_methods,
    .m_slots = sqlite_m_slots,
    .m_traverse = (traverseproc)sqlite_m_traverse,
    .m_clear = (inquiry)sqlite_m_clear,
    .m_free = (freefunc)sqlite_m_free,
};


/* module initialization */
PyMODINIT_FUNC
PyInit_sqlite(void)
{
    return PyModuleDef_Init(&sqlite_def);
}
