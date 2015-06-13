
ws = new WebSocket("ws://" + location.host + "/gputop/", "binary");
ws.binaryType = 'arraybuffer';
ws.onerror = function () {
    console.log("WS Error");
}
ws.onopen = function () {
    console.log("WS Open");
    ws.send("test");
}

var buffer = 0, bufferSize = 0;

ws.onmessage = function (e) {
    var data = e.data;

    console.log("WS On Message");
    if (data) {
	if (!buffer || bufferSize < data.byteLength) {
	    if (buffer) _free(buffer);
	    bufferSize = data.byteLength;
	    buffer = _malloc(data.byteLength);
	}

	if (!(data instanceof Uint8Array))
	    data = new Uint8Array(data);

	HEAPU8.set(data, buffer);

	_gputop_websocket_onmessage(buffer, data.byteLength);
    }
}

onmessage = function(e) {
    postMessage("foo");
}
