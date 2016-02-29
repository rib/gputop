const fork = require('child_process').fork;
var child;
var js_file = __dirname + "/gputop-nodejs-ui";

if (process.argv.length >= 3) {
    switch(process.argv[2]) {
        case "features":
            console.log("gputop: Launching features tool.\n");
            break;
        default:
            console.error("gputop: Unrecognized tool: " + process.argv[2] +
                      ". Launching features tool.\n");
            break;
    }
}

child = fork(js_file);
