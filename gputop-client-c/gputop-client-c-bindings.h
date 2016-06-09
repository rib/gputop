/*
 * GPU Top
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <node.h>
#include <v8.h>


/* Trivial wrapper object we can use to effectively return a handle to a C
 * pointer to javascript.
 */
class CPtrObj : public node::ObjectWrap {
    public:
        static v8::Persistent<v8::Function> constructor;
        static void Init(v8::Handle<v8::Object> exports);
        void *ptr_;
    private:
        CPtrObj();
        ~CPtrObj();
        static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
};

/* An opaque structure that can attach a persistent reference to a v8::Object
 * to a C structure via a void *js_priv member
 */
struct JSPriv
{
public:
    v8::Persistent<v8::Object> js_obj;
};


extern v8::Persistent<v8::Object> gputop_cc_singleton;
