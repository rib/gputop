//"use strict";

var LibraryGpuTopWeb = {
    //$GpuTopWeb__deps: ['$Browser'],
    $GpuTopWeb: {
        sockets: {},
        next_socket_id: 1,

        scratch_buf: null,
        scratch_buf_len: 0,
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

    _gputop_web_get_location_host: function () {
        var str = allocate(intArrayFromString(location.host), 'i8', ALLOC_NORMAL);
        return str;
    },

    _gputop_web_worker_post: function (message) {
        postMessage(Pointer_stringify(message));
    },

    _gputop_web_socket_new: function (_url, _protocols) {
        var url = Pointer_stringify(_url);
        var protocols = Pointer_stringify(_protocols).split(" ");
        var id = GpuTopWeb.next_socket_id++;

        var ws = new WebSocket(url, protocols);
        ws.binaryType = 'arraybuffer';

        GpuTopWeb.sockets[id] = ws;

        return id;
    },

    _gputop_web_socket_set_onopen: function (id, onopen, user_data) {
        GpuTopWeb.sockets[id].onopen = function (e) {
            if (onopen) Runtime.dynCall('vii', onopen, [id, user_data]);
        };
    },

    _gputop_web_socket_set_onerror: function (id, onerror, user_data) {
        GpuTopWeb.sockets[id].onerror = function (e) {
            if (onerror) Runtime.dynCall('vii', onerror, [id, user_data]);
        };
    },

    _gputop_web_socket_set_onclose: function (id, onclose, user_data) {
        GpuTopWeb.sockets[id].onclose = function (e) {
            if (onclose) Runtime.dynCall('vii', onclose, [id, user_data]);
        };
    },

    _gputop_web_socket_set_onmessage: function (id, onmessage, user_data) {
        GpuTopWeb.sockets[id].onmessage = function (e) {
            var data = e.data;

            if (data && onmessage) {
                if (!GpuTopWeb.scratch_buf || GpuTopWeb.scratch_buf_len < data.byteLength) {
                    if (GpuTopWeb.scratch_buf)
                        _free(GpuTopWeb.scratch_buf);
                    GpuTopWeb.scratch_buf_len = data.byteLength;
                    GpuTopWeb.scratch_buf = _malloc(data.byteLength);
                }

                if (!(data instanceof Uint8Array))
                    data = new Uint8Array(data);

                HEAPU8.set(data, GpuTopWeb.scratch_buf);

                Runtime.dynCall('viiii', onmessage,
                                [id,
                                GpuTopWeb.scratch_buf,
                                data.byteLength,
                                user_data]);
            }
        };
    },

    _gputop_web_socket_destroy: function (id) {
        GpuTopWeb.sockets[id] = null;
    },

    _gputop_web_socket_post: function (id, data, len) {
        var ws = GpuTopWeb.sockets[id];
        var msg = HEAPU8.subarray(data, data + len);
        ws.send(msg);
    },
};

autoAddDeps(LibraryGpuTopWeb, '$GpuTopWeb');
mergeInto(LibraryManager.library, LibraryGpuTopWeb);
