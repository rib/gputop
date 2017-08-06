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

#include <node.h>
#include <node_object_wrap.h>
#include <v8.h>
#include <map>

#include "gputop-client-c-bindings.h"
#include "gputop-client-c-runtime.h"

#define ARRAY_LENGTH(A) (sizeof(A)/sizeof(A[0]))

using namespace v8;


static std::map<std::string, struct gputop_metric_set *> _guid_to_metric_set_map;

static void
gputop_console(int level, const char *message)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> gputop = Local<Object>::New(isolate, gputop_cc_singleton);
    Local<Object> console = gputop->Get(String::NewFromUtf8(isolate, "console"))->ToObject();
    Local<Function> log = Local<Function>::Cast(console->Get(String::NewFromUtf8(isolate, "log")));

    Local<Value> argv[] = {
        String::NewFromUtf8(isolate, message),
        Number::New(isolate, level)
    };
    log->Call(gputop, ARRAY_LENGTH(argv), argv);
}

void
_gputop_cr_console_log(const char *message)
{
    gputop_console(0, message);
}

void
_gputop_cr_console_warn(const char *message)
{
    gputop_console(1, message);
}

void
_gputop_cr_console_error(const char *message)
{
    gputop_console(2, message);
}

void
_gputop_cr_console_assert(bool condition, const char *message)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Context> ctx = isolate->GetCurrentContext();
    Local<Object> ctx_scope = ctx->Global();

    Local<Object> console = ctx_scope->Get(String::NewFromUtf8(isolate, "console"))->ToObject();
    Local<Function> fn = Local<Function>::Cast(console->Get(String::NewFromUtf8(isolate, "assert")));

    Local<Value> argv[] = {
        Boolean::New(isolate, condition),
        String::NewFromUtf8(isolate, message)
    };
    fn->Call(ctx_scope, ARRAY_LENGTH(argv), argv);
}

void
gputop_cr_index_metric_set(const char *guid, struct gputop_metric_set *metric_set)
{
    _guid_to_metric_set_map[guid] = metric_set;
}

struct gputop_metric_set *
gputop_cr_lookup_metric_set(const char *guid)
{
    return _guid_to_metric_set_map[guid];
}

bool
_gputop_cr_accumulator_start_update(struct gputop_cc_stream *stream,
                                    struct gputop_cc_oa_accumulator *accumulator,
                                    uint32_t events,
                                    double start_timestamp, double end_timestamp)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> gputop = Local<Object>::New(isolate, gputop_cc_singleton);
    Local<Function> fn = Local<Function>::Cast(gputop->Get(String::NewFromUtf8(isolate, "accumulator_start_update")));

    JSPriv *stream_js_priv = static_cast<JSPriv *>(stream->js_priv);
    Local<Object> stream_js = Local<Object>::New(isolate, stream_js_priv->js_obj);
    JSPriv *accumulator_js_priv = static_cast<JSPriv *>(accumulator->js_priv);
    Local<Object> accumulator_js = Local<Object>::New(isolate, accumulator_js_priv->js_obj);

    Local<Value> argv[] = { stream_js,
                            accumulator_js,
                            Number::New(isolate, events),
                            Number::New(isolate, start_timestamp),
                            Number::New(isolate, end_timestamp) };
    Local<Value> ret = fn->Call(gputop, ARRAY_LENGTH(argv), argv);

    return ret->BooleanValue();
}


void
_gputop_cr_accumulator_append_count(int counter,
                                    double max, double value)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> gputop = Local<Object>::New(isolate, gputop_cc_singleton);
    Local<Function> fn = Local<Function>::Cast(gputop->Get(String::NewFromUtf8(isolate, "accumulator_append_count")));

    Local<Value> argv[] = { Number::New(isolate, counter),
                            Number::New(isolate, max),
                            Number::New(isolate, value) };
    fn->Call(gputop, ARRAY_LENGTH(argv), argv);
}

void
_gputop_cr_accumulator_end_update(void)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> gputop = Local<Object>::New(isolate, gputop_cc_singleton);
    Local<Function> fn = Local<Function>::Cast(gputop->Get(String::NewFromUtf8(isolate, "accumulator_end_update")));

    Local<Value> argv[] = { };
    fn->Call(gputop, ARRAY_LENGTH(argv), argv);
}

void
_gputop_cr_send_idle_flag(int idle_flag)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Object> gputop = Local<Object>::New(isolate, gputop_cc_singleton);
    Local<Function> fn = Local<Function>::Cast(gputop->Get(String::NewFromUtf8(isolate, "send_idle_flag")));

    Local<Value> argv[] = {Number::New(isolate, idle_flag)};
    fn->Call(gputop, ARRAY_LENGTH(argv), argv);
}
