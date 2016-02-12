// Set up: npm install
var http = require("http"),
    fs = require("fs"),
    path = require("path"),
    ws = require("ws"),
    open = require("open"),
    ProtoBuf = require("protobufjs");

// Initialize from .proto file
var builder = ProtoBuf.loadProtoFile(path.join(__dirname, "gputop.proto")),

LogEntry = builder.build("gputop.LogEntry");

// HTTP server
var server = http.createServer(function(req, res) {
        var file = null,
            type = "text/html";
        if (req.url == "/") {
            file = "index.html";
        } else if (/^\/(\w+(?:\.min)?\.(?:js|html|proto))$/.test(req.url)) {
            file = req.url.substring(1);
            console.log("Serving "+req.url.substring(1));
            if (/\.js$/.test(file)) {
                type = "text/javascript";
            }
        } else {
            file = req.url.substring(1);
            //console.log("Asking "+req.url.substring(1));
            if (/\.css$/.test(file)) {
                type = "text/css";
            }
        }
        if (file) {
            fs.readFile(path.join(__dirname, file), function(err, data) {
                if (err) {
                    res.writeHead(500, {"Content-Type": type});
                    res.end("Internal Server Error: "+err);
                    console.log("Internal Server Error: "+file);
                } else {
                    res.writeHead(200, {"Content-Type": type});
                    res.write(data);
                    res.end();
                    console.log("Served "+file);
                }
            });
        } else {
            console.log("Not found "+req.url);
            res.writeHead(404, {"Content-Type": "text/html"});
            res.end("Not Found");
        }
    });
server.listen(8080);
server.on("listening", function() {
    console.log("Server started");
    open("http://localhost:8080/");
});
server.on("error", function(err) {
    console.log("Failed to start server:", err);
    process.exit(1);
});

// WebSocket adapter
var wss = new ws.Server({server: server});
wss.on("connection", function(socket) {
    console.log("New WebSocket connection");
    socket.on("close", function() {
        console.log("WebSocket disconnected");
    });
    socket.on("message", function(data, flags) {
        if (flags.binary) {
            try {
                // Decode the Message

                var msg = LogEntry.decode(data);
                console.log("Received: "+msg.log_message);
                // Transform the text to upper case
                msg.log_message = msg.log_message.toUpperCase();
                // Re-encode it and send it back
                socket.send(msg.toBuffer());
                console.log("Test Sent: "+msg.log_message);

            } catch (err) {
                console.log("Processing failed:", err);
            }
        } else {
            console.log("Not binary data");
        }
    });
});
