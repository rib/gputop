//"use strict";

var LibraryGpuTopWeb = {
    //$GpuTopWeb__deps: ['$Browser'],
    $GpuTopWeb: {

    },

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

    _gputop_web_worker_post: function (message) {
	postMessage(Pointer_stringify(message));
    },
};

autoAddDeps(LibraryGpuTopWeb, '$GpuTopWeb');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);
