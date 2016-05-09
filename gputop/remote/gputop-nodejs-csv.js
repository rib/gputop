const http = require('http');
const pkill = require('pkill');

var gputop;
var gputop_ui;
var supported_oa_query_guids;

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

GPUTopNodeJSUI.prototype.update_features = function(features)
{
    console.log(features);

    features.supported_oa_query_guids.forEach(function (guid, i, a) {
        var metric = gputop.get_map_metric(guid);
        metric.counters_.forEach(function (i, j, k) {
            metric.counters_[j].record_data = true;
        });
    });

    gputop.open_oa_metric_set({guid: features.supported_oa_query_guids[0]});
    supported_oa_query_guids = features.supported_oa_query_guids;
}

GPUTopNodeJSUI.prototype.queue_redraw = function()
{
    if (supported_oa_query_guids !== undefined)
        var metric = gputop.get_map_metric(supported_oa_query_guids[0]);
}

GPUTopNodeJSUI.prototype.load_metrics_panel = function(open_query)
{
    open_query();
}

GPUTopNodeJSUI.prototype.update_slider_period = function(period)
{
}

GPUTopNodeJSUI.prototype.render_bars = function()
{
}

GPUTopNodeJSUI.prototype.log = function(level, message)
{
    console.log(level);
    console.log(message);
}

function gputop_ready(inst)
{
    gputop = inst;
    gputop_ui = new GPUTopNodeJSUI();
    gputop.connect();
}

function gputop_is_demo () {
    return false;
}

function on_gputop_web_ready(gputop_web) {
    http.get("http://localhost:7890/gputop.js", function(response) {
        var gputopjs = "";
        response.setEncoding('utf8');
        response.on('data', function(data) {
            gputopjs += data;
        });
        response.on('end', function () {
            eval(gputop_web);
            eval(gputopjs);
        });
    });
}

http.get("http://localhost:7890/gputop-web.js", function(response) {
    var gputop_web = "";
    response.setEncoding('utf8');
    response.on('data', function(data) {
        gputop_web += data;
    });
    response.on('end', function () {
        on_gputop_web_ready(gputop_web);
    });
});

function gputop_clean_exit()
{
    if (gputop !== undefined && supported_oa_query_guids !== undefined) {
        var metric = gputop.get_map_metric(supported_oa_query_guids[0]);
        var fs = require('fs');
        var stream = fs.createWriteStream("my_file.csv");
        stream.once('open', function(fd) {
            var csv_file = "Metric,Counter,Start,End,Value,Maximum,Reason\n";

            for (j = 0; j < metric.counters_.length; j++) {
                for (i = 0; i < metric.counters_[j].updates.length; i++) {
                    csv_file += "" + metric.name_ + "," +
                        metric.counters_[j].symbol_name + "," +
                        metric.counters_[j].updates[i][0] + "," +
                        metric.counters_[j].updates[i][1] + "," +
                        metric.counters_[j].updates[i][2] + "," +
                        metric.counters_[j].updates[i][3] + "," +
                        metric.counters_[j].updates[i][4] + "," + "\n";
                }
            }

            stream.write(csv_file);
            stream.end();
            pkill('gputop', function(error, valid_pid) {
                if (error)
                    console.log(error);
                console.log(valid_pid);
            });

            process.exit();
        });
    } else {
        pkill('gputop', function(error, valid_pid) {
            if (error)
                console.log(error);
            console.log(valid_pid);
        });

        process.exit();
    }
}

process.on('SIGINT', gputop_clean_exit);
process.on('SIGTERM', gputop_clean_exit);
