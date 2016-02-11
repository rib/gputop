/*
 * GPU Top
 *
 * Copyright (C) 2015 Intel Corporation
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

_gputop_webworker_init();

onmessage = function(e) {
    var req = e.data;
    var sym_name = "_gputop_webworker_on_" + req.method;
    var func = Module[sym_name];

    if (!func) {
        console.warn("Failed to resolve symbol: " + sym_name);
        return;
    }

    var stack = Runtime.stackSave();

    /*
     * UI request messages to the web worker should look like:
     *
     *   { "method": "set_foo", "params": [ "arg", 0.35 ], "uuid": "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" }
     *
     * We lookup a C function based on the method name
     * (in this case "gputop_webworker_on_set_foo")
     *
     * Before calling the function we copy any strings in params[]
     * to the Emscripten stack.
     *
     * The ID may be used to send replies
     *
     * We use UUIDs to easily allow requests that start in different
     * components (e.g. UI vs worker initiated requests)
     */
    var args = [];
    var i = 0;
    /*
    console.log("params:");
    for (var arg of req.params) {
        console.log("  " + arg);
    }
    */

    for (var arg of req.params) {
        if (arg.substring) /* recognise literals or objects */
            args[i++] = allocate(intArrayFromString(arg), 'i8', ALLOC_STACK);
        else if (arg.toFixed)
            args[i++] = arg;
        else if (typeof arg === "boolean")
            args[i++] = arg ? 1 : 0;
        else
            console.warn("JS param couldn't be coerced for calling " + sym_name + ": param = " + arg)
    }

    args[i++] = allocate(intArrayFromString(req.uuid), 'i8', ALLOC_STACK);

    //console.log("n arguments = " + args.length);

    func.apply(this, args);

    Runtime.stackRestore(stack);
}
