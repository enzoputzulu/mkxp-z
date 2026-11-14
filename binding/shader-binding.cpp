/*
 ** shader-binding.cpp
 **
 ** This file is part of mkxp.
 **
 ** mkxp is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** mkxp is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Ruby class Shader: a fragment shader written by the game.
 *
 *   s = Shader.new(glsl_source)            # GLSL ES 1.00 fragment stage
 *   s.set_float("strength", 0.5)           # float, vec2, vec3 or vec4 (1 to 4 values)
 *   s.set_array("lights", 4, [x, y, r, i, ...])  # uniform vec4 lights[N]; comps 1 to 4
 *   s.set_texture("noise", bitmap)         # extra sampler2D, nil removes it
 *   viewport.shader = s                    # or sprite.shader = s, nil removes it
 *
 * Shader::API is the version of this interface. */

#include "binding-types.h"
#include "binding-util.h"
#include "sharedstate.h"
#include "graphics.h"
#include "bitmap.h"
#include "shader.h"
#include "exception.h"

#include <string>
#include <vector>

/* The GL objects of a shader are deleted when Ruby frees it: hold the
 * graphics lock, as the setters do (it also makes the GL context current
 * on the thread that runs the garbage collector). */
static void freeShader(void *inst)
{
    GFX_LOCK;
    delete static_cast<CustomShader *>(inst);
    GFX_UNLOCK;
}

#if RAPI_FULL > 187
DEF_TYPE_CUSTOMNAME_AND_FREE(CustomShader, "Shader", freeShader);
#else
DEF_ALLOCFUNC_CUSTOMFREE(CustomShader, freeShader);
#endif

static const int MAX_ARRAY_FLOATS = 1024;

/* Every argument is checked before a C++ object (std::string,
 * std::vector) is built: rb_raise jumps over C++ destructors, so raising
 * after building one would leak it. */
static void checkString(VALUE v, int pos)
{
    if (!RB_TYPE_P(v, RUBY_T_STRING))
        rb_raise(rb_eTypeError, "Argument %d: Expected string", pos);
}

static void checkFloat(VALUE v, int pos)
{
    if (!RB_FLOAT_TYPE_P(v) && !RB_INTEGER_TYPE_P(v))
        rb_raise(rb_eTypeError, "Argument %d: Expected float", pos);
}

/* Only after checkString / checkFloat: these cannot raise */
static std::string toString(VALUE v)
{
    return std::string(RSTRING_PTR(v), RSTRING_LEN(v));
}

static float toFloat(VALUE v)
{
    return (float) NUM2DBL(v);
}

/* getPrivateData raises on an object without data: read the pointer itself */
static CustomShader *rawData(VALUE self)
{
#if RAPI_FULL > 187
    return static_cast<CustomShader *>(RTYPEDDATA_DATA(self));
#else
    return static_cast<CustomShader *>(DATA_PTR(self));
#endif
}

static CustomShader *shaderData(VALUE self)
{
    CustomShader *s = rawData(self);
    if (!s)
        rb_raise(rb_eRuntimeError, "uninitialized shader");

    return s;
}

/* Calls f() under the graphics lock and turns a C++ Exception into a
 * Ruby one once every C++ object of this frame is gone. */
template<typename F>
static void guarded(F f)
{
    VALUE excClass = Qnil, msg = Qnil;

    GFX_LOCK;
    try {
        f();
    } catch (const Exception &exc) {
        excClass = getRbData()->exc[MKXP];
        msg = rb_str_new_cstr(exc.msg.c_str());   /* msg is padded with NULs */
    }
    GFX_UNLOCK;

    if (!NIL_P(msg))
        rb_exc_raise(rb_exc_new_str(excClass, msg));
}

RB_METHOD(shaderInitialize) {
    if (argc != 1)
        rb_raise(rb_eArgError, "wrong number of arguments (%d for 1)", argc);
    checkString(argv[0], 0);
    if (rawData(self))
        rb_raise(rb_eRuntimeError, "shader already initialized");

    VALUE src = argv[0];
    CustomShader *s = 0;
    guarded([&] { s = new CustomShader(toString(src)); });

    setPrivateData(self, s);
    rb_iv_set(self, "textures", rb_hash_new());

    return self;
}

RB_METHOD(shaderSetFloat) {
    if (argc < 2 || argc > 5)
        rb_raise(rb_eArgError, "set_float(name, value[, value, value, value])");

    CustomShader *s = shaderData(self);
    checkString(argv[0], 0);
    for (int i = 1; i < argc; ++i)
        checkFloat(argv[i], i);

    float values[4];
    for (int i = 1; i < argc; ++i)
        values[i - 1] = toFloat(argv[i]);

    VALUE name = argv[0];
    int count = argc - 1;
    guarded([&] { s->setFloats(toString(name), count, 1, values); });

    return self;
}

RB_METHOD(shaderSetArray) {
    if (argc != 3)
        rb_raise(rb_eArgError, "set_array(name, components, values)");

    CustomShader *s = shaderData(self);
    checkString(argv[0], 0);

    int comps;
    rb_int_arg(argv[1], &comps, 1);
    if (comps < 1 || comps > 4)
        rb_raise(rb_eArgError, "components must be 1 to 4");

    VALUE ary = argv[2];
    if (!RB_TYPE_P(ary, RUBY_T_ARRAY))
        rb_raise(rb_eTypeError, "Argument 2: Expected array");

    long len = RARRAY_LEN(ary);
    if (len == 0 || len % comps != 0 || len > MAX_ARRAY_FLOATS)
        rb_raise(rb_eArgError, "values must hold a multiple of components, at most %d floats", MAX_ARRAY_FLOATS);

    float values[MAX_ARRAY_FLOATS];
    for (long i = 0; i < len; ++i)
    {
        VALUE v = rb_ary_entry(ary, i);
        checkFloat(v, 2);
        values[i] = toFloat(v);
    }

    VALUE name = argv[0];
    guarded([&] { s->setFloats(toString(name), comps, (int) (len / comps), values); });

    return self;
}

RB_METHOD(shaderSetTexture) {
    if (argc != 2)
        rb_raise(rb_eArgError, "set_texture(name, bitmap)");

    CustomShader *s = shaderData(self);
    checkString(argv[0], 0);

    Bitmap *b = 0;
    if (!NIL_P(argv[1]))
    {
        b = getPrivateDataCheck<Bitmap>(argv[1], BitmapType);
        if (!b)
            rb_raise(rb_eArgError, "Argument 1: uninitialized bitmap");
        if (b->isDisposed())
            rb_raise(getRbData()->exc[RGSS], "disposed bitmap");
        if (b->isMega())
            rb_raise(rb_eArgError, "Argument 1: bitmap too large for a texture");
    }

    VALUE name = argv[0];
    guarded([&] { s->setTexture(toString(name), b); });

    /* Keep the bitmap alive as long as the shader uses it */
    VALUE textures = rb_iv_get(self, "textures");
    if (NIL_P(argv[1]))
        rb_hash_delete(textures, argv[0]);
    else
        rb_hash_aset(textures, argv[0], argv[1]);

    return self;
}

void shaderBindingInit() {
    VALUE klass = rb_define_class("Shader", rb_cObject);
#if RAPI_FULL > 187
    rb_define_alloc_func(klass, classAllocate<&CustomShaderType>);
#else
    rb_define_alloc_func(klass, CustomShaderAllocate);
#endif

    rb_define_const(klass, "API", INT2FIX(1));

    _rb_define_method(klass, "initialize", shaderInitialize);
    _rb_define_method(klass, "set_float", shaderSetFloat);
    _rb_define_method(klass, "set_array", shaderSetArray);
    _rb_define_method(klass, "set_texture", shaderSetTexture);
}
