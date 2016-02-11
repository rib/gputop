//"use strict";

var LibraryGpuTopWeb = {
    _gputop_web_console_log: function (message) {
        console.log(Pointer_stringify(message));
    },

    _gputop_web_console_info: function (message) {
        console.info(Pointer_stringify(message));
    },

    _gputop_web_console_warn: function (message) {
        console.warn(Pointer_stringify(message));
    },

    _gputop_web_console_error: function (message) {
        console.error(Pointer_stringify(message));
    },

    _gputop_web_console_assert: function (condition, message) {
        console.assert(condition, Pointer_stringify(message));
    },

    _gputop_web_console_trace: function () {
        console.trace();
    },

    _gputop_query_update_counter: function (counter, id, start_timestamp, end_timestamp, delta, max, d_value) {
        if (gputop != undefined)
            gputop.query_update_counter(counter, id, start_timestamp, end_timestamp, delta, max, d_value);
        else
            console.log(" Missing GPUTOP to process " + counter + " COUNTER ["+start_timestamp+":"+ end_timestamp +"]:"+delta+" = "+ d_value);
    },

};

autoAddDeps(LibraryGpuTopWeb, '$GpuTopWeb');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);
