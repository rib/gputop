// Set up: npm install
var http = require("http"),
    fs = require("fs"),
    path = require("path"),
    open = require("open");

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
            var filename =  req.url.substring(1).split("?")[0];
            file = filename;
            console.log("Asking "+filename);

            if (/\.css$/.test(file)) {
                type = "text/css";
            }
        }
        if (file) {
            fs.readFile(path.join(__dirname, "remote", file), function(err, data) {
                if (err) {
                    res.writeHead(500, {"Content-Type": type});
                    res.end("Internal Server Error: "+err);
                    console.log("Internal Server Error: "+file);
                } else {
                    res.writeHead(200, {"Content-Type": type});
                    res.write(data);
                    res.end();
                    console.log("Served remote/"+file);
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

