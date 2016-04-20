"use strict";

//# sourceURL=gputop-ui.js

/*
 * GPU Top
 *
 * Copyright (C) 2015-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

function GputopUI () {
    this.graph_array = [];
    this.zoom = 10; //seconds

    this.series = [{
        lines: {
            fill: true
        },
        color: "#6666ff"
    }];

    this.start_timestamp = 0;
    this.start_gpu_timestamp = 0;

    this.queue_redraw_ = false;
}


function create_default_markings(xaxis) {
    var markings = [];
    for (var x = Math.floor(xaxis.min); x < xaxis.max; x += xaxis.tickSize * 2) {
        markings.push({ xaxis: { from: x, to: x + xaxis.tickSize }, color: "rgba(232, 232, 255, 0.2)" });
    }
    return markings;
}


/* FIXME: this isn't a good place for code relating to fiddly OA exponent details
 */
function max_exponent_below(nsec) {
    for (var i = 0; i < 64; i++) {
        var period = (1<<i) * 1000000000 / gputop.devinfo.get_timestamp_frequency();

        if (period > nsec)
            return Math.max(0, i - 1);
    }

    return i;
}


/* returns true if the exponent changed and therefore the metric
 * stream needs to be re-opened, else false.
 */
function update_metric_period_exponent_for_zoom(metric) {
    var hack_graph_size_px = 1000;

    /* We want to set an aggregation period such that we get ~1 update per
     * x-axis-pixel on the trace graph.
     *
     * We need to ensure the HW sampling period is low enough to expect two HW
     * samples per x-axis-pixel.
     *
     * FIXME: actually determine how wide the graphs are in pixels instead of
     * assuming 1000 pixels.
     */

    var ns_per_pixel = gputop_ui.zoom * 1000000000 / hack_graph_size_px;

    /* XXX: the way we make side-band changes to the metric object while its
     * still open seems fragile.
     *
     * E.g. directly lowering the aggregation period potentially lower than the
     * HW sampling period won't be meaningful.
     *
     * These changes should be done in terms of opening a new stream of metrics
     * which happens asynchronously after the current stream has closed and the
     * new, pending configuration shouldn't have any affect on any currently
     * open stream.
     */
    gputop.update_period(global_guid, ns_per_pixel);
    var exponent = max_exponent_below(ns_per_pixel);

    if (metric.exponent != exponent) {
        metric.exponent = exponent;
        return true;
    } else
        return false;
}


GputopUI.prototype.set_zoom = function(zoom) {
    gputop_ui.zoom = zoom;

    var metric = gputop.get_map_metric(global_guid);

    if (update_metric_period_exponent_for_zoom(metric))
        gputop.open_oa_metric_set({guid:global_guid});

    this.queue_redraw();
}


GputopUI.prototype.update_graphs = function(timestamp) {
    var metric = gputop.get_map_metric(global_guid);

    for (var i = 0; i < this.graph_array.length; ++i) {
        var container = "#" + this.graph_array[i];
        var counter = $(container).data("counter");

        var length = counter.updates.length;
        var x_min = 0;
        var x_max = 1;

        if (!this.start_timestamp && length > 0) {
            this.start_timestamp = timestamp;
            this.start_gpu_timestamp = counter.updates[length - 1][0]; // end_timestamp
        }

        var elapsed = (timestamp - this.start_timestamp) * 1000000; // elapsed time from the very begining

        var time_range = this.zoom * 1000000000;
        var margin = time_range * 0.1;

        x_max = this.start_gpu_timestamp + elapsed;
        x_min = x_max - time_range;

        // make sure the distance between points is proportional to the aggregation period,
        // so that we don't clutter the graph when changing the zoom, which would lead to
        // low performance.
        var i = 1;
        while (i < counter.graph_data.length) {
            if (counter.graph_data[i][0] - counter.graph_data[i-1][0] < metric.period_ns_)
                counter.graph_data.splice(i, 1);
            else
                i++;
        }
        // data is saved in the graph for an interval of 20 seconds regardless
        // of the zoom value
        var max_graph_data = x_max - 20000000000;

        // remove the old samples from the graph data
        for (var j = 0; j < counter.graph_data.length &&
            counter.graph_data[j][0] < max_graph_data; j++) {}
        if (j > 0)
            counter.graph_data = counter.graph_data.slice(j);

        // remove old markings from the graph
        for (var j = 0; j < counter.graph_markings.length &&
             counter.graph_markings[j].xaxis.from < max_graph_data; j++) {}
        if (j > 0)
            counter.graph_markings = counter.graph_markings.slice(j);

        var save_index = -1;
        for (var j = 0; j < length; j++) {
            var start = counter.updates[j][0];
            var end = counter.updates[j][1];
            var val = counter.updates[j][2];
            var max = counter.updates[j][3];
            var mid = start + (end - start) / 2;

            counter.graph_data.push([mid, val]);
            counter.graph_options.yaxis.max = 1.10 * counter.inferred_max; // add another 10% to the Y axis
            save_index = j;
        }

        // resync the timestamps (the javascript timestamp with the counter timestamp)
        if (elapsed > 5000000000 && save_index > -1)
        {
            this.start_timestamp = timestamp;
            this.start_gpu_timestamp = counter.updates[save_index][0];
        }

        // adjust the min and max (start and end of the graph)
        counter.graph_options.xaxis.min = x_min + margin;
        counter.graph_options.xaxis.max = x_max - margin;
        counter.graph_options.xaxis.label = this.zoom + ' seconds';

        var default_markings = create_default_markings(counter.graph_options.xaxis);
        counter.graph_options.grid.markings = default_markings.concat(counter.graph_markings);

        this.series[0].data = counter.graph_data;
        $.plot(container, this.series, counter.graph_options);

        // remove all the samples from the updates array
        counter.updates.splice(0, counter.updates.length);
    }
}


GputopUI.prototype.update_counter = function(counter) {
    var bar_value = counter.latest_value;
    var text_value = counter.latest_value;
    var max = counter.latest_max;
    var units = counter.units;
    var units_suffix = "";
    var dp = 0;
    var kilo = 1000;
    var mega = kilo * 1000;
    var giga = mega * 1000;
    var scale = {"bytes":["B", "KB", "MB", "GB"],
                 "ns":["ns", "μs", "ms", "s"],
                 "hz":["Hz", "KHz", "MHz", "GHz"],
                 "texels":[" texels", " K texels", " M texels", " G texels"],
                 "pixels":[" pixels", " K pixels", " M pixels", " G pixels"],
                 "cycles":[" cycles", " K cycles", " M cycles", " G cycles"],
                 "threads":[" threads", " K threads", " M threads", " G threads"]};



    if ((units in scale)) {
        dp = 2;
        if (text_value >= giga) {
            units_suffix = scale[units][3];
            text_value /= 1000000000;
        } else if (text_value >= mega) {
            units_suffix = scale[units][2];
            text_value /= 1000000;
        } else if (text_value >= kilo) {
            units_suffix = scale[units][1];
            text_value /= 1000;
        } else
            units_suffix = scale[units][0];
    } else if (units === 'percent') {
        units_suffix = '%';
        dp = 2;
    }

    if (counter.duration_dependent)
        units_suffix += '/s';

    if (counter.div_ == undefined)
        counter.div_ = $('#'+counter.div_bar_id_ );

    if (counter.div_txt_ == undefined)
        counter.div_txt_ = $('#'+counter.div_txt_id_ );

    if (max != 0) {
        counter.div_.css("width", 100 * bar_value / max + "%");
        counter.div_txt_.text(text_value.toFixed(dp) + units_suffix);
    } else {
        counter.div_.css("width", "0%");
        counter.div_txt_.text(text_value.toFixed(dp) + units_suffix);
    }
}

GputopUI.prototype.update = function(timestamp) {
    var metric = gputop.active_oa_metric_;
    if (metric == undefined)
        return;

    this.update_graphs(timestamp);

    for (var i = 0, l = metric.emc_counters_.length; i < l; i++) {
        var counter = metric.emc_counters_[i];
        this.update_counter(counter);
    }

    /* We want smooth graph panning and bar graph updates while we have
     * an active stream of metrics, so keep queuing redraws... */
    this.queue_redraw();
}

GputopUI.prototype.queue_redraw = function() {
    if (this.redraw_queued_)
        return;

    window.requestAnimationFrame(function (timestamp) {
        gputop_ui.redraw_queued_ = false;
        gputop_ui.update(timestamp);
    });

    this.redraw_queued_ = true;
}

GputopUI.prototype.metric_not_supported = function(metric) {
    alert(" Metric not supported " + metric.title_)
}

/* XXX: this is essentially the first entry point into GputopUI after
 * connecting to the server, after Gputop has been initialized */
GputopUI.prototype.update_features = function(features) {
    if (features.devinfo.get_devid() == 0 ) {
        gputop_ui.show_alert(" No device was detected, is it the functionality on kernel ? ","alert-danger");
    }

    $( "#gputop-gpu" ).html( gputop.get_arch_pretty_name() );

    $( ".gputop-connecting" ).hide();
    $( "#gputop-cpu" ).html( features.get_cpu_model() );
    $( "#gputop-kernel-build" ).html( features.get_kernel_build() );
    $( "#gputop-kernel-release" ).html( features.get_kernel_release() );
    $( "#gputop-n-cpus" ).html( features.get_n_cpus() );

    $( "#gputop-n-eus" ).html( features.devinfo.get_n_eus().toInt() );
    $( "#gputop-n-eu-slices" ).html( features.devinfo.get_n_eu_slices().toInt()  );
    $( "#gputop-n-eu-sub-slices" ).html( features.devinfo.get_n_eu_sub_slices().toInt()  );
    $( "#gputop-n-eu-threads-count" ).html( features.devinfo.get_eu_threads_count().toInt()  );
    $( "#gputop-timestamp-frequency" ).html( features.devinfo.get_timestamp_frequency().toInt()  );

    if (features.get_fake_mode())
        $( "#metrics-tab-a" ).html("Metrics (Fake Mode) ");

    gputop_ui.load_metrics_panel(function() {
        var metric = gputop.get_map_metric(global_guid);

        update_metric_period_exponent_for_zoom(metric);

        gputop.open_oa_metric_set({guid:global_guid});
    });
}

// types of alerts: alert-success alert-info alert-warning alert-danger
GputopUI.prototype.show_alert = function(message,alerttype){

    var dimiss_time = 5000;
    if (alerttype == "alert-success") dimiss_time = 2500; else
    if (alerttype == "alert-info") dimiss_time = 3500; else
    if (alerttype == "alert-danger") dimiss_time = 30000;

    $('#alert_placeholder').append('<div id="alertdiv" class="alert ' +
        alerttype + '"><a class="close" data-dismiss="alert">×</a><span>'+message+'</span></div>')
        setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
            $("#alertdiv").remove();
        }, dimiss_time);
}

GputopUI.prototype.log = function(log_level, log_message){
    var color = "red";
    switch(log_level) {
        case 0: color = "orange"; break;
        case 1: color = "green"; break;
        case 2: color = "yellow"; break;
        case 3: color = "blue"; break;
        case 4: color = "black"; break;
    }
    $('#editor').append("<font color='"+color+"'>"+log_message+"<br/></font>");
}

GputopUI.prototype.syslog = function(message){
    gputop_ui.syslog_.value += message + "\n";
    console.log(message);
}

GputopUI.prototype.weblog = function(message){
    //gputop_ui.weblog.value += message + "\n";
}

GputopUI.prototype.init_interface = function(){
    $( "#gputop-overview-panel" ).load( "ajax/overview.html", function() {
        console.log('gputop-overview-panel load');
        gputop.connect();
    });
}

GputopUI.prototype.load_metrics_panel = function(callback_success) {
    $( '#pane2' ).load( "ajax/metrics.html", function() {
        console.log('Metrics panel loaded');
        callback_success();
    });
}

var gputop_ui = new GputopUI();

GputopUI.prototype.btn_close_current_query = function() {
    var active_metric = gputop.active_oa_metric_;
    if (active_metric == undefined) {
        gputop_ui.show_alert(" No Active Query","alert-info");
        return;
    }

    gputop.close_oa_metric_set(active_metric, function() {
       gputop_ui.show_alert(" Success closing query","alert-info");
    });
}

GputopUI.prototype.update_process = function(process) {

    var pid = process.pid_;
    var name = process.process_name_;

    var tooltip = '<a id="pid_'+ pid +'" href="#" data-toggle="tooltip" title="' + process.cmd_line_ + '">'+ pid + ' ' +name+ '</a>';

    $("#sidebar_processes_info").append('<li class="col-sm-10 ">' + tooltip + '</li>');
    $('#pid_'+pid).click(function (e) {
        console.log("click "+e.target.id);
    });
}

GputopUI.prototype.btn_get_process_info = function() {
    bootbox.prompt("Process Id?", function(result) {
        if (result === null) {
            gputop_ui.show_alert(" Cancelled","alert-info");
        } else {
            var pid = parseInt(result,10);
            if (!isNaN(pid)) {
                gputop.get_process_info(pid, function(msg) {
                    gputop_ui.show_alert(" Callback "+result,"alert-info");
                });
            } else {
                gputop_ui.show_alert("Input not a valid PID","alert-info");
            }
        }
    });
}

// jquery code
$( document ).ready(function() {
    //log = $( "#log" );
    gputop_ui.syslog_ = document.getElementById("log");

/*
    $( "#gputop-entries" ).append( '<li><a id="close_query" href="#" onClick>Close Query</a></li>' );
    $( '#close_query' ).click( gputop_ui.btn_close_current_query);
*/

    $( '#process-tab-a' ).click(gputop_ui.btn_get_process_info);

    gputop_ui.init_interface();
    $( '#editor' ).wysiwyg();

    // Display tooltips
    $( '[data-toggle="tooltip"]' ).tooltip();
});
