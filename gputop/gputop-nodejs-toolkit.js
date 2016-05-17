const fork = require('child_process').fork;
var child;
var js_file = __dirname + "/gputop-nodejs-ui";

if (process.argv.length >= 3) {
    switch(process.argv[2]) {
        case "features":
            console.log("gputop: Launching features tool.\n");
            break;
        case "csv":
            console.log("gputop: Launching csv tool.\n");
            js_file = __dirname + "/gputop-nodejs-csv"
            break;
        default:
            console.error("gputop: Unrecognized tool: " + process.argv[2] +
                      ". Launching features tool.\n");
            break;
    }
}

child = fork(js_file);
