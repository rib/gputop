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

    _gputop_query_update_counter_ui: function (id, start_timestamp, end_timestamp, delta, max, ui64_value) {
        console.log(" UPDATE COUNTER UI");
    },

    _gputop_query_update_counter_d: function (id, start_timestamp, end_timestamp, delta, max, d_value) {
        console.log(" UPDATE COUNTER D");
    },
      
};

autoAddDeps(LibraryGpuTopWeb, '$GpuTopWeb');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);
