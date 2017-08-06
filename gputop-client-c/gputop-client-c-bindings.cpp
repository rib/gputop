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

#include "gputop-client-c.h"
#include "gputop-client-c-bindings.h"

#define ARRAY_LENGTH(A) (sizeof(A)/sizeof(A[0]))


using namespace v8;

Persistent<Object> gputop_cc_singleton;

Persistent<Function> CPtrObj::constructor;

CPtrObj::CPtrObj() {
}

CPtrObj::~CPtrObj() {
}

void CPtrObj::New(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.IsConstructCall()) {
        // Invoked as constructor: `new CPtrObj(...)`
        CPtrObj *obj = new CPtrObj();
        obj->Wrap(args.This());
        args.GetReturnValue().Set(args.This());
    } else {
        // Invoked as plain function `CPtrObj(...)`, turn into construct call.
        const int argc = 1;
        Local<Value> argv[argc] = { args[0] };
        Local<Function> cons = Local<Function>::New(isolate, constructor);
        Local<Context> context = isolate->GetCurrentContext();
        Local<Object> instance =
            cons->NewInstance(context, argc, argv).ToLocalChecked();
        args.GetReturnValue().Set(instance);
    }
}

void CPtrObj::Init(Handle<Object> exports)
{
    Isolate* isolate = Isolate::GetCurrent();

    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "CPtrObj"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    constructor.Reset(isolate, tpl->GetFunction());
    exports->Set(String::NewFromUtf8(isolate, "CPtrObj"), tpl->GetFunction());
}


void
gputop_cc_set_singleton_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    gputop_cc_singleton.Reset(isolate, args[0]->ToObject());
}

void
gputop_cc_get_counter_id_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 2) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    int ret = gputop_cc_get_counter_id(*String::Utf8Value(args[0]), /* guid */
                                       *String::Utf8Value(args[1])); /* counter symbol name */
    args.GetReturnValue().Set(Number::New(isolate, ret));
}

void
gputop_cc_handle_i915_perf_message_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_stream *stream = (struct gputop_cc_stream *)ptr->ptr_;

    unsigned int len = args[2]->NumberValue();
    unsigned int ctx_hw_id = args[5]->NumberValue();
    unsigned int idle_flag = args[6]->NumberValue();

    if (!args[1]->IsArrayBufferView()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Expected 2nd argument to be an ArrayBufferView")));
        return;
    }

    Local<ArrayBufferView> view = Local<ArrayBufferView>::Cast(args[1]);
    size_t offset = view->ByteOffset();
    size_t view_len = view->ByteLength();

    Local<ArrayBuffer> buf = view->Buffer();
    auto data_contents = buf->GetContents();

    if (len > view_len || (offset + len) > data_contents.ByteLength()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Length given would overrun data")));
        return;
    }

    if (!args[3]->IsArray()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Expected array of accumulators in 4th arg")));
        return;
    }

    Local<Array> accumulators_js = Local<Array>::Cast(args[3]);
    int n_accumulators = accumulators_js->Length();

    if (n_accumulators > 256 || n_accumulators != args[4]->NumberValue()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Spurious value for N accumulators")));
        return;
    }

    struct gputop_cc_oa_accumulator *accumulators[n_accumulators];

    for (int i = 0; i < n_accumulators; i++) {
        if (!accumulators_js->Get(0)->IsObject()) {
            isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Non object in accumulator array")));
            return;
        }

        Local<Object> obj = accumulators_js->Get(0)->ToObject();

        CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(obj);
        struct gputop_cc_oa_accumulator *accumulator =
            (struct gputop_cc_oa_accumulator *)ptr->ptr_;

        accumulators[i] = accumulator;
    }

    gputop_cc_handle_i915_perf_message(stream,
                                       static_cast<uint8_t *>(data_contents.Data()) + offset,
                                       len,
                                       accumulators,
                                       n_accumulators,
                                       ctx_hw_id,
                                       idle_flag);
}

void
gputop_cc_reset_system_properties_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    gputop_cc_reset_system_properties();
}

void
gputop_cc_set_system_property_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    gputop_cc_set_system_property(*String::Utf8Value(args[0]),
                                  args[1]->NumberValue());
}

void
gputop_cc_set_system_property_string_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    gputop_cc_set_system_property_string(*String::Utf8Value(args[0]),
                                         *String::Utf8Value(args[1]));
}

void
gputop_cc_update_system_metrics_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    gputop_cc_update_system_metrics();
}

static void __attribute__((unused))
object_print_properties_debug(Local<Object> obj)
{
    Local<Array> props = obj->GetPropertyNames();
    uint32_t len = props->Length();
    for (uint32_t i = 0; i < len; i++) {
        Local<Value> e = props->Get(i)->ToString();
        fprintf(stderr, "prop: %s\n", *String::Utf8Value(e));
    }
}

static Local<Object>
bind_c_struct(Isolate* isolate, void *c_struct, void **js_priv_member)
{
    /* Create a corresponding v8::Object we can return as a handle to the new stream */
    Local<Function> constructor = Local<Function>::New(isolate, CPtrObj::constructor);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> obj = constructor->NewInstance(context).ToLocalChecked();
    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(obj);
    ptr->ptr_ = (void *)c_struct;

    /* And associate a pointer in the other direction from the struct to the
     * JS Object... */
    JSPriv *js_priv = new JSPriv();
    js_priv->js_obj.Reset(isolate, obj);
    *js_priv_member = static_cast<void *>(js_priv);

    return obj;
}

void
gputop_cc_oa_stream_new_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    struct gputop_cc_stream *stream =
        gputop_cc_oa_stream_new(*String::Utf8Value(args[0])); /* guid */

    Local<Object> stream_obj = bind_c_struct(isolate, stream, &stream->js_priv);

    args.GetReturnValue().Set(stream_obj);
}

void
gputop_cc_stream_destroy_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_stream *stream = static_cast<struct gputop_cc_stream *>(ptr->ptr_);
    JSPriv *js_priv = static_cast<JSPriv *>(stream->js_priv);

    delete js_priv;
    stream->js_priv = nullptr;

    gputop_cc_stream_destroy(stream);
}

void
gputop_cc_oa_accumulator_new_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 3) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_stream *stream = static_cast<struct gputop_cc_stream *>(ptr->ptr_);

    struct gputop_cc_oa_accumulator *accumulator =
        gputop_cc_oa_accumulator_new(stream,
                                     args[1]->NumberValue(), // aggregation period
                                     args[2]->BooleanValue()); //enable_ctx_switch_events

    Local<Object> accumulator_obj = bind_c_struct(isolate, accumulator, &accumulator->js_priv);
    args.GetReturnValue().Set(accumulator_obj);
}

void
gputop_cc_oa_accumulator_set_period_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 2) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_oa_accumulator *accumulator = static_cast<struct gputop_cc_oa_accumulator *>(ptr->ptr_);

    unsigned int period = args[1]->NumberValue();

    gputop_cc_oa_accumulator_set_period(accumulator, period);
}

void
gputop_cc_oa_accumulator_clear_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_oa_accumulator *accumulator = static_cast<struct gputop_cc_oa_accumulator *>(ptr->ptr_);

    gputop_cc_oa_accumulator_clear(accumulator);
}

void
gputop_cc_oa_accumulator_destroy_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 1) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_oa_accumulator *accumulator = static_cast<struct gputop_cc_oa_accumulator *>(ptr->ptr_);
    JSPriv *js_priv = static_cast<JSPriv *>(accumulator->js_priv);

    delete js_priv;
    accumulator->js_priv = nullptr;

    gputop_cc_oa_accumulator_destroy(accumulator);
}

void
gputop_cc_tracepoint_stream_new_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    struct gputop_cc_stream *stream =
        gputop_cc_tracepoint_stream_new();

    Local<Object> stream_obj = bind_c_struct(isolate, stream, &stream->js_priv);

    args.GetReturnValue().Set(stream_obj);
}

void
gputop_cc_tracepoint_add_field_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_stream *stream = static_cast<struct gputop_cc_stream *>(ptr->ptr_);

    gputop_cc_tracepoint_add_field(stream,
                                   *String::Utf8Value(args[1]), /* name */
                                   *String::Utf8Value(args[2]), /* type */
                                   args[3]->NumberValue(), /* offset */
                                   args[4]->NumberValue(), /* size */
                                   args[3]->BooleanValue()); /* is_signed */
}

void
gputop_cc_handle_tracepoint_message_binding(const v8::FunctionCallbackInfo<Value>& args)
{
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    if (args.Length() < 3) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Wrong number of arguments")));
        return;
    }

    CPtrObj *ptr = node::ObjectWrap::Unwrap<CPtrObj>(args[0]->ToObject());
    struct gputop_cc_stream *stream = (struct gputop_cc_stream *)ptr->ptr_;

    unsigned int len = args[2]->NumberValue();

    if (!args[1]->IsArrayBufferView()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Expected 2nd argument to be an ArrayBufferView")));
        return;
    }

    Local<ArrayBufferView> view = Local<ArrayBufferView>::Cast(args[1]);
    size_t offset = view->ByteOffset();
    size_t view_len = view->ByteLength();

    Local<ArrayBuffer> buf = view->Buffer();
    auto data_contents = buf->GetContents();

    if (len > view_len || (offset + len) > data_contents.ByteLength()) {
        isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Length given would overrun data")));
        return;
    }

    gputop_cc_handle_tracepoint_message(stream,
                                        static_cast<uint8_t *>(data_contents.Data()) + offset,
                                        len);
}

void
Init(Handle<Object> exports)
{
    Isolate* isolate = Isolate::GetCurrent();

    CPtrObj::Init(exports);

#define EXPORT(X) \
    exports->Set(String::NewFromUtf8(isolate, "_" #X), \
                 FunctionTemplate::New(isolate, X##_binding)->GetFunction())

    EXPORT(gputop_cc_set_singleton);
    EXPORT(gputop_cc_get_counter_id);
    EXPORT(gputop_cc_handle_i915_perf_message);
    EXPORT(gputop_cc_reset_system_properties);
    EXPORT(gputop_cc_set_system_property);
    EXPORT(gputop_cc_set_system_property_string);
    EXPORT(gputop_cc_update_system_metrics);
    EXPORT(gputop_cc_oa_stream_new);
    EXPORT(gputop_cc_stream_destroy);
    EXPORT(gputop_cc_oa_accumulator_new);
    EXPORT(gputop_cc_oa_accumulator_set_period);
    EXPORT(gputop_cc_oa_accumulator_clear);
    EXPORT(gputop_cc_oa_accumulator_destroy);
    EXPORT(gputop_cc_tracepoint_stream_new);
    EXPORT(gputop_cc_tracepoint_add_field);
    EXPORT(gputop_cc_handle_tracepoint_message);

#undef EXPORT
}

NODE_MODULE(gputop_client_c, Init)
