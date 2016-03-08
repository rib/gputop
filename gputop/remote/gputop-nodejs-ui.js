const web_socket = require('ws');
const protobufjs = require("protobufjs");
const uuid = require('uuid');
const http_request = require('http');
const string_decoder = require('string_decoder').StringDecoder;
const pkill = require('pkill');
const gputop_web_socket = new web_socket('ws://localhost:7890/gputop/');
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

    gputop_web_socket.send(request_buffer.toArrayBuffer());
    console.log('Sent features request to GPUTop server');
}

process.on('SIGINT', gputop_clean_exit); // catch ctrl-c
process.on('SIGTERM', gputop_clean_exit); // catch kill

gputop_web_socket.on('open', function() {
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
});

gputop_web_socket.on('message', function(proto_message) {
    var message = gputop_message.decode(proto_message);
    console.log('Received message from GPUTop server');

    if (message.features !== null)
    {
        gputop_print_features(message.features);
        gputop_clean_exit();
    }
});

function gputop_is_website() {
    return false;
}
