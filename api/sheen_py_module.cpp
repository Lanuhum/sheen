// Sheen нативный Python-модуль (Этап 2).
// Python-обёртка БЕЗ зависимостей от CUDA: использует плоский POD-контракт
// sheen_flat.h (изоморфен types.h) и линкуется с libsheen_c_api.so, где
// живёт CUDA-ядро. Передача сцены — единый срез памяти (buffer protocol),
// ноль разбора, ноль копирования.
//
// Использование:
//   import sheen
//   sheen.init(0, 61)
//   sheen.render_scene(scene_ctypes, width, height, aa, output_path)
//   sheen.cleanup()

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sheen_flat.h"

// ---------------------------------------------------------------------
// C-функции из libsheen_c_api.so. Сцена передаётся как SSheenScene* —
// ABI-совместим с SheenScene (проверено: 208 байт, те же offsets).
// ---------------------------------------------------------------------
extern "C" {
int sheen_init(int device_id, int cuda_arch);
void sheen_cleanup(void);
int sheen_render_scene(const SSheenScene* scene, int width, int height,
                       int aa_samples, const char* output_path);
const char* sheen_get_last_error(void);
}

// ---------------------------------------------------------------------
// def init(device_id: int, cuda_arch: int) -> None
// ---------------------------------------------------------------------
static PyObject* py_sheen_init(PyObject*, PyObject* args) {
    int dev = 0, arch = 61;
    if (!PyArg_ParseTuple(args, "ii", &dev, &arch)) return nullptr;
    int rc = sheen_init(dev, arch);
    if (rc != 0) {
        PyErr_SetString(PyExc_RuntimeError, sheen_get_last_error());
        return nullptr;
    }
    Py_RETURN_NONE;
}

// ---------------------------------------------------------------------
// def cleanup() -> None
// ---------------------------------------------------------------------
static PyObject* py_sheen_cleanup(PyObject*, PyObject*) {
    sheen_cleanup();
    Py_RETURN_NONE;
}

// ---------------------------------------------------------------------
// def render_scene(scene, width, height, aa, output_path) -> None
// scene: ctypes-структура SheenScene (обязана давать непрерывный буфер,
//        размером >= sizeof(SSheenScene)) — из c_api_contract.py.
// ---------------------------------------------------------------------
static PyObject* py_sheen_render_scene(PyObject*, PyObject* args) {
    PyObject* scene_obj = nullptr;
    int width = 0, height = 0, aa = 4;
    const char* out_path = nullptr;
    if (!PyArg_ParseTuple(args, "Oiiis", &scene_obj, &width, &height,
                          &aa, &out_path))
        return nullptr;

    Py_buffer buf;
    if (PyObject_GetBuffer(scene_obj, &buf, PyBUF_SIMPLE) != 0) {
        PyErr_SetString(PyExc_TypeError,
            "scene must be a buffer (ctypes SheenScene structure)");
        return nullptr;
    }
    if (buf.len < (Py_ssize_t)sizeof(SSheenScene)) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_TypeError, "scene buffer too small for SSheenScene");
        return nullptr;
    }

    const SSheenScene* scene = static_cast<const SSheenScene*>(buf.buf);
    int rc = sheen_render_scene(scene, width, height, aa, out_path);
    PyBuffer_Release(&buf);

    if (rc != 0) {
        PyErr_SetString(PyExc_RuntimeError, sheen_get_last_error());
        return nullptr;
    }
    Py_RETURN_NONE;
}

// ---------------------------------------------------------------------
static PyMethodDef SheenMethods[] = {
    {"init", py_sheen_init, METH_VARARGS, "Initialize CUDA device."},
    {"cleanup", py_sheen_cleanup, METH_NOARGS, "Release CUDA resources."},
    {"render_scene", py_sheen_render_scene, METH_VARARGS,
     "Render a SSheenScene (ctypes struct) to PNG."},
    {nullptr, nullptr, 0, nullptr},
};

static struct PyModuleDef sheenmodule = {
    PyModuleDef_HEAD_INIT, "sheen",
    "Sheen renderer native module (CUDA core via libsheen_c_api.so).",
    -1, SheenMethods,
};

PyMODINIT_FUNC PyInit_sheen(void) {
    return PyModule_Create(&sheenmodule);
}