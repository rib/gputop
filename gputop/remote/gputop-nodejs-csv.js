const http = require('http');

function GPUTopNodeJSUI()
{
    this.type = "dummy_ui";
}

GPUTopNodeJSUI.prototype.syslog = function(message)
{
    console.log(message);
}

GPUTopNodeJSUI.prototype.show_alert = function(message, type)
{
    console.log(message);
}

GPUTopNodeJSUI.prototype.weblog = function(message)
{
    console.log(message);
}

GPUTopNodeJSUI.prototype.display_features = function(message)
{
    console.log(message);
}

var gputop_ui = new GPUTopNodeJSUI();

function gputop_ready(gputop)
{
    gputop.connect();
}

http.get("http://localhost:7890/gputop.js", function(response) {
    response.setEncoding('utf8');
    response.on('data', function(data) {
        eval(data);
    });
});
