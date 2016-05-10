const Gputop = require('gputop');
const pkill = require('pkill');
const fs = require('fs');

function GPUTopNodeJSUI()
{
    Gputop.Gputop.call(this);

    this.type = "dummy_ui";
    this.supported_oa_query_guids = undefined;
}

GPUTopNodeJSUI.prototype = Object.create(Gputop.Gputop.prototype);

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

    features.supported_oa_query_guids.forEach((guid, i, a) => {
        var metric = this.get_map_metric(guid);
        metric.counters_.forEach(function (i, j, k) {
            metric.counters_[j].record_data = true;
        });
    });

    gputop.open_oa_metric_set({guid: features.supported_oa_query_guids[0]});
    this.supported_oa_query_guids = features.supported_oa_query_guids;
}

GPUTopNodeJSUI.prototype.queue_redraw = function()
{
    if (this.supported_oa_query_guids !== undefined)
        var metric = this.get_map_metric(this.supported_oa_query_guids[0]);
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

GPUTopNodeJSUI.prototype.clean_exit = function () {
    console.log("Writting CSV file...");
    if (this.supported_oa_query_guids !== undefined) {
        var metric = this.get_map_metric(this.supported_oa_query_guids[0]);
        var stream = fs.createWriteStream("my_file.csv");

        stream.once('open', (fd) => {
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

var gputop = new GPUTopNodeJSUI();
gputop.connect();

process.on('SIGINT', gputop.clean_exit.bind(gputop));
process.on('SIGTERM', gputop.clean_exit.bind(gputop));

