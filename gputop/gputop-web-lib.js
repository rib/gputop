//"use strict";

/*
 * GPU Top
 *
 * Copyright (C) 2015-2016 Intel Corporation
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

var LibraryGpuTopWeb = {
    $GPUTop: {
        _guid_to_metric_set_map: {},
    },

    _gputop_web_console_log: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(message);
        else
            console.log(Pointer_stringify(message));
    },

    _gputop_web_console_warn: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(message, gputop.WARN);
        else
            console.warn(Pointer_stringify(message));
    },

    _gputop_web_console_error: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(message, gputop.ERROR);
        else
            console.error(Pointer_stringify(message));
    },

    _gputop_web_console_assert: function (condition, message) {
        console.assert(condition, Pointer_stringify(message));
    },

    gputop_web_index_metric_set: function (guid, metric_set) {
        GPUTop._guid_to_metric_set_map[Pointer_stringify(guid)] = metric_set;
    },
    gputop_web_lookup_metric_set: function (guid) {
        var key = Pointer_stringify(guid);
        if (key in GPUTop._guid_to_metric_set_map)
            return GPUTop._guid_to_metric_set_map[key];
        else {
            console.error('Failed to find metric_set with guid = ' + key);
            return 0;
        }
    },

    _gputop_stream_start_update: function (stream_ptr, start_timestamp, end_timestamp, reason) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.stream_start_update.call(gputop, stream_ptr, start_timestamp, end_timestamp, reason);
        else
            console.error("Gputop singleton not initialized");
    },
    _gputop_stream_update_counter: function (stream_ptr, counter_idx, max, value) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.stream_update_counter.call(gputop, stream_ptr, counter_idx, max, value);
        else
            console.error("Gputop singleton not initialized");
    },
    _gputop_stream_end_update: function (stream_ptr) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.stream_end_update.call(gputop, stream_ptr);
        else
            console.error("Gputop singleton not initialized");
    },
};

autoAddDeps(LibraryGpuTopWeb, '$GPUTop');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);

//# sourceURL=gputop-web-lib.js
