// Copyright (c) 2014 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PYSTON_CAPI_TYPES_H
#define PYSTON_CAPI_TYPES_H

#include "runtime/types.h"

namespace pyston {

typedef PyObject* (*wrapperfunc)(PyObject* self, PyObject* args, void* wrapped);
typedef PyObject* (*wrapperfunc_kwds)(PyObject* self, PyObject* args, void* wrapped, PyObject* kwds);

struct wrapper_def {
    const char* name;
    int offset;
    void* function;      // "generic" handler that gets put in the tp_* slot which proxies to the python version
    wrapperfunc wrapper; // "wrapper" that ends up getting called by the Python-visible WrapperDescr
    int flags;
};

extern BoxedClass* capifunc_cls, *wrapperdescr_cls, *wrapperobject_cls;

class BoxedCApiFunction : public Box {
private:
    int ml_flags;
    Box* passthrough;
    const char* name;
    PyCFunction func;

public:
    BoxedCApiFunction(int ml_flags, Box* passthrough, const char* name, PyCFunction func)
        : Box(capifunc_cls), ml_flags(ml_flags), passthrough(passthrough), name(name), func(func) {}

    static BoxedString* __repr__(BoxedCApiFunction* self) {
        assert(self->cls == capifunc_cls);
        return boxStrConstant(self->name);
    }

    static Box* __call__(BoxedCApiFunction* self, BoxedTuple* varargs, BoxedDict* kwargs) {
        assert(self->cls == capifunc_cls);
        assert(varargs->cls == tuple_cls);
        assert(kwargs->cls == dict_cls);

        threading::GLPromoteRegion _gil_lock;

        Box* rtn;
        if (self->ml_flags == METH_VARARGS) {
            assert(kwargs->d.size() == 0);
            rtn = (Box*)self->func(self->passthrough, varargs);
        } else if (self->ml_flags == (METH_VARARGS | METH_KEYWORDS)) {
            rtn = (Box*)((PyCFunctionWithKeywords)self->func)(self->passthrough, varargs, kwargs);
        } else if (self->ml_flags == METH_NOARGS) {
            assert(kwargs->d.size() == 0);
            assert(varargs->elts.size() == 0);
            rtn = (Box*)self->func(self->passthrough, NULL);
        } else if (self->ml_flags == METH_O) {
            assert(kwargs->d.size() == 0);
            assert(varargs->elts.size() == 1);
            rtn = (Box*)self->func(self->passthrough, varargs->elts[0]);
        } else {
            RELEASE_ASSERT(0, "0x%x", self->ml_flags);
        }
        RELEASE_ASSERT(rtn, "need to throw an exception");
        return rtn;
    }
};

class BoxedWrapperDescriptor : public Box {
public:
    const wrapper_def* wrapper;
    BoxedClass* type;
    BoxedWrapperDescriptor(const wrapper_def* wrapper, BoxedClass* type)
        : Box(wrapperdescr_cls), wrapper(wrapper), type(type) {}

    static Box* __get__(BoxedWrapperDescriptor* self, Box* inst, Box* owner);
};

class BoxedWrapperObject : public Box {
public:
    BoxedWrapperDescriptor* descr;
    Box* obj;

    BoxedWrapperObject(BoxedWrapperDescriptor* descr, Box* obj) : Box(wrapperobject_cls), descr(descr), obj(obj) {}

    static Box* __call__(BoxedWrapperObject* self, Box* args, Box* kwds) {
        assert(self->cls == wrapperobject_cls);
        assert(args->cls == tuple_cls);
        assert(kwds->cls == dict_cls);

        int flags = self->descr->wrapper->flags;
        wrapperfunc wrapper = self->descr->wrapper->wrapper;
        assert(self->descr->wrapper->offset > 0);
        char* ptr = (char*)self->descr->type + self->descr->wrapper->offset;
        void* wrapped = *reinterpret_cast<void**>(ptr);

        if (flags & PyWrapperFlag_KEYWORDS) {
            wrapperfunc_kwds wk = (wrapperfunc_kwds)wrapper;
            return (*wk)(self->obj, args, wrapped, kwds);
        } else {
            return (*wrapper)(self->obj, args, wrapped);
        }
        abort();
    }
};

class BoxedMethodDescriptor : public Box {
public:
    PyMethodDef* method;
    BoxedClass* type;

    BoxedMethodDescriptor(PyMethodDef* method, BoxedClass* type) : Box(method_cls), method(method), type(type) {}

    static Box* __get__(BoxedMethodDescriptor* self, Box* inst, Box* owner) {
        RELEASE_ASSERT(self->cls == method_cls, "");

        if (inst == None)
            return self;
        // CPython apparently returns a "builtin_function_or_method" object
        return boxInstanceMethod(inst, self);
    }

    static Box* __call__(BoxedMethodDescriptor* self, Box* obj, BoxedTuple* varargs, Box** _args);
};

} // namespace pyston

#endif
