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

    _gputop_cr_console_log: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(Pointer_stringify(message));
        else
            console.log(Pointer_stringify(message));
    },

    _gputop_cr_console_warn: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(Pointer_stringify(message), gputop.WARN);
        else
            console.warn(Pointer_stringify(message));
    },

    _gputop_cr_console_error: function (message) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.log(Pointer_stringify(message), gputop.ERROR);
        else
            console.error(Pointer_stringify(message));
    },

    _gputop_cr_console_assert: function (condition, message) {
        console.assert(condition, Pointer_stringify(message));
    },

    gputop_cr_index_metric_set: function (guid, metric_set) {
        GPUTop._guid_to_metric_set_map[Pointer_stringify(guid)] = metric_set;
    },
    gputop_cr_lookup_metric_set: function (guid) {
        var key = Pointer_stringify(guid);
        if (key in GPUTop._guid_to_metric_set_map)
            return GPUTop._guid_to_metric_set_map[key];
        else {
            console.error('Failed to find metric_set with guid = ' + key);
            return 0;
        }
    },

    _gputop_cr_accumulator_start_update: function (stream_ptr, accumulator_ptr, events_mask, start_timestamp, end_timestamp) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            return gputop.accumulator_start_update.call(gputop, stream_ptr, accumulator_ptr, events_mask, start_timestamp, end_timestamp);
        else {
            console.error("Gputop singleton not initialized");
            return false;
        }
    },
    _gputop_cr_accumulator_append_count: function (counter_idx, max, value) {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.accumulator_append_count.call(gputop, counter_idx, max, value);
        else
            console.error("Gputop singleton not initialized");
    },
    _gputop_cr_accumulator_end_update: function () {
        var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.accumulator_end_update.call(gputop);
        else
            console.error("Gputop singleton not initialized");
    },
    _gputop_cr_send_idle_flag: function(idle_flag) {
    var gputop = Module['gputop_singleton'];
        if (gputop !== undefined)
            gputop.send_idle_flag.call(gputop, idle_flag);
        else
            console.error("Gputop singleton not initialized");
    },
};

autoAddDeps(LibraryGpuTopWeb, '$GPUTop');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);

//# sourceURL=gputop-web-lib.js
