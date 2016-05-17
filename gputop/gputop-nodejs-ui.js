const WebSocket = require('ws');
const protobufjs = require("protobufjs");
const uuid = require('uuid');
const http_request = require('http');
const string_decoder = require('string_decoder').StringDecoder;
const pkill = require('pkill');
const ws = new WebSocket('ws://localhost:7890/gputop/', 'binary');
var protobuf_builder = null;
var gputop_protobuf = null;
var gputop_request = null;
var gputop_message = null;

function gputop_clean_exit()
{
    pkill('gputop', function(error, valid_pid) {
        if (error)
            console.log(error);
        console.log(valid_pid);
    });

    process.exit();
}

function gputop_print_features(features)
{
    console.log(features);
}

function gputop_send_features_request()
{
    var request = new gputop_request();
    var reqest_buffer;

    request.uuid = uuid.v4();
    request.get_features = true;
    request.req = "get_features";

    request_buffer = request.encode();

    ws.send(request_buffer.toArrayBuffer());
    console.log('Sent features request to GPUTop server');
}

process.on('SIGINT', gputop_clean_exit); // catch ctrl-c
process.on('SIGTERM', gputop_clean_exit); // catch kill

ws.onopen =  function() {
    console.log('Connected to GPUTop server');
    http_request.get("http://localhost:7890/gputop.proto", function(response) {
        response.on('data', function(data) {
            var decoder = new string_decoder('utf8');

            protobuf_builder = protobufjs.newBuilder();
            protobufjs.protoFromString(decoder.write(data), protobuf_builder,
                "gputop.proto");
            gputop_protobuf = protobuf_builder.build("gputop");
            console.log("Loaded proto file");
            gputop_request = gputop_protobuf.Request;
            gputop_message = gputop_protobuf.Message;
            gputop_send_features_request();
        });
    });
};

ws.onmessage = function(evt) {
    //node.js ws api doesn't support .binaryType = "arraybuffer"
    //so manually convert evt.data to an ArrayBuffer...
    evt.data = new Uint8Array(evt.data).buffer;

    dv = new DataView(evt.data, 0);
    data = new Uint8Array(evt.data, 8);

    msg_type = dv.getUint8(0);

    if (msg_type !== 2) // WS_MESSAGE_PROTOBUF
        return;

    var message = gputop_message.decode(data);

    console.log('Received protobuf message from GPUTop server');
    if (message.features !== null)
    {
        gputop_print_features(message.features);
        gputop_clean_exit();
    }
};

/*function gputop_is_demo() {
    return false;
}*/
