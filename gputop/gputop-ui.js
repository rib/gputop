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
    Gputop.call(this);

    /* NB: Although we initialize in demo mode in these conditions, it's still
     * possible to enter/leave demo mode interactively by specifying and
     * address:port for a target to connect to. (which is why we don't
     * do these checks in the .is_demo() method.
     */
    var demo = getUrlParameter('demo');
    if (demo == "true" || demo == "1"
        || window.location.hostname == "gputop.github.io"
        || window.location.hostname == "www.gputop.com"
       ) {
        $('#target_address').attr('value', "demo");
        this.demo_mode = true;
    } else {
        $('#target_address').attr('value', window.location.hostname);
        this.demo_mode = false;
    }

    this.overview_loaded = false;
    this.demo_ui_loaded = false;

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

    this.redraw_queued_ = false;

    this.previous_zoom = 0;

    this.cpu_stats_stream = undefined;

    this.cpu_stats_last = undefined;
    this.cpu_stats_start = 0;
    this.cpu_stats_start_js = 0;
    this.cpu_stats_last_graph_update_time = 0;
    this.cpu_stats = undefined;
    this.cpu_stats_div = undefined;
}

GputopUI.prototype = Object.create(Gputop.prototype);

GputopUI.prototype.is_demo = function() {
    return this.demo_mode;
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
function max_exponent_below(nsec, timestamp_frequency) {
    for (var i = 0; i < 64; i++) {
        var period = (1<<i) * 1000000000 / timestamp_frequency;

        if (period > nsec)
            return Math.max(0, i - 1);
    }

    return i;
}


/* returns true if the exponent changed and therefore the metric
 * stream needs to be re-opened, else false.
 */
GputopUI.prototype.update_metric_period_exponent_for_zoom = function (metric) {
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

    var ns_per_pixel = this.zoom * 1000000000 / hack_graph_size_px;

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
    this.update_period(global_guid, ns_per_pixel);
    var exponent = max_exponent_below(ns_per_pixel,
                                      this.devinfo.get_timestamp_frequency());

    if (metric.exponent != exponent) {
        metric.exponent = exponent;
        return true;
    } else
        return false;
}


GputopUI.prototype.set_zoom = function(zoom) {
    this.zoom = zoom;

    var metric = this.lookup_metric_for_guid(global_guid);

    // First update metric period exponent, then check for paused query.
    // The other way around would have not checked whether or not to update the
    // exponent if the query was paused.
    if (this.update_metric_period_exponent_for_zoom(metric) && !global_paused_query)
        this.open_oa_metric_set({guid:global_guid});

    this.queue_redraw();
}

// remove all the data from all opened graphs
GputopUI.prototype.clear_graphs = function() {
    var metric = this.lookup_metric_for_guid(global_guid);

    // reset the accumulator clock and continuation report
    webc._gputop_webc_reset_accumulator(metric.webc_stream_ptr_);

    // go through the list of opened graphs
    for (var i = 0; i < this.graph_array.length; ++i) {
        var container = "#" + this.graph_array[i];
        var counter = $(container).data("counter");
        counter.graph_data = [];
        counter.updates = [];
        this.start_timestamp = 0;
    }
}

GputopUI.prototype.update_graphs_paused = function (timestamp) {
    var metric = this.lookup_metric_for_guid(global_guid);

    for (var i = 0; i < this.graph_array.length; ++i) {
        var container = "#" + this.graph_array[i];
        var counter = $(container).data("counter");

        var length = counter.updates.length;
        var x_min = 0;
        var x_max = 1;

        if (length > 0) {
                this.start_gpu_timestamp = counter.updates[0][0]; // end_timestamp
                this.end_gpu_timestamp = counter.updates[length - 1][0]; // end_timestamp
        }
        if (length > 0 || this.previous_zoom != this.zoom) {
            this.previous_zoom = this.zoom;
            var time_range = this.zoom * 1000000000;
            var margin = time_range * 0.1;

            x_max = this.end_gpu_timestamp;
            x_min = x_max - time_range;
            var max_graph_data = x_max - 20000000000;

            for (var j = 0; j < length; j++) {
                var start = counter.updates[j][0];
                var end = counter.updates[j][1];
                var val = counter.updates[j][2];
                var max = counter.updates[j][3];
                var mid = start + (end - start) / 2;

                counter.graph_data.push([mid, val]);
                counter.graph_options.yaxis.max = 1.10 * counter.inferred_max; // add another 10% to the Y axis
            }

            // adjust the min and max (start and end of the graph)
            counter.graph_options.xaxis.min = x_min + margin;
            counter.graph_options.xaxis.max = x_max;
            counter.graph_options.xaxis.label = this.zoom + ' seconds';
            counter.graph_options.xaxis.panRange = [max_graph_data, x_max];

            var default_markings = create_default_markings(counter.graph_options.xaxis);
            counter.graph_options.grid.markings = default_markings.concat(counter.graph_markings);

            this.series[0].data = counter.graph_data;
            $.plot(container, this.series, counter.graph_options);

            // remove all the samples from the updates array
            counter.updates.splice(0, counter.updates.length);
        }
    }
}

GputopUI.prototype.update_oa_graphs = function(timestamp) {
    this.timestamp = timestamp;
    if (global_paused_query) {
        this.update_graphs_paused(timestamp);
        return;
    }

    var metric = this.lookup_metric_for_guid(global_guid);

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
        var margin = time_range * 0.05;

        x_max = this.start_gpu_timestamp + elapsed;
        x_min = x_max - time_range;

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

GputopUI.prototype.update_cpu_stats = function (timestamp) {

    if (this.cpu_stats === undefined || this.cpu_stats_start === 0)
        return;

    if (this.cpu_stats_div === undefined) {
        this.cpu_stats_div = document.getElementById('cpu-stats');

        this.cpu_stats_start_js = timestamp;

        var layout = {
            title: "CPU Overview",
            xaxis: { range: [this.cpu_stats_start, this.cpu_stats_start + 5000000000], autorange: false, rangemode: 'normal', ticks: '', showticklabels: false },
            yaxis: { range: [0, 100], showgrid: false, title: "Busy %" },
            showLegend: true
        };

        var config = {
            displayModeBar: false
        };

        var traces = new Array(this.cpu_stats.length);

        for (var i = 0; i < this.cpu_stats.length; i++) {
            traces[i] = {
                name: "CPU " + i,
                type: "scatter",
                mode: "lines",
                x: [],
                y: [],
            };
        }

        Plotly.newPlot(this.cpu_stats_div,
                       traces,
                       layout,
                       config);
    }

    var progress = (timestamp - this.cpu_stats_start_js) * 1000000;

    var time_range = 10000000000; // 10 seconds

    var x_max = this.cpu_stats_start + progress;
    var x_min = x_max - time_range;

    var margin = time_range * 0.05;
    var new_layout = {
        'xaxis.range': [x_min + margin, x_max - margin],
    };
    Plotly.relayout(this.cpu_stats_div, new_layout);

    /* Updating the graph data is a lot more costly so we do it in batches
     * instead of every frame... */
    var margin_ms = margin / 1000000;
    if ((timestamp - this.cpu_stats_last_graph_update_time) < (margin_ms / 2))
        return;

    var timestamps = this.cpu_stats_div.data[0].x;
    for (var clip = 0, dlen = timestamps.length; clip < dlen && timestamps[clip] < x_min; clip++) {}
    // remove the old samples
    if (clip > 0)
        timestamps = timestamps.slice(clip);

    timestamps = timestamps.concat(this.cpu_stats_timestamp_updates);
    this.cpu_stats_timestamp_updates = [];

    var y_updates = new Array(this.cpu_stats.length);
    var x_updates = new Array(this.cpu_stats.length);

    for (var i = 0; i < this.cpu_stats.length; i++) {

        var y_data = this.cpu_stats_div.data[i].y;

        // remove the old samples
        if (clip > 0)
            y_data = y_data.slice(clip);

        y_data = y_data.concat(this.cpu_stats[i]);
        this.cpu_stats[i] = [];

        y_updates[i] = y_data;
        x_updates[i] = timestamps;
    }
    var update = { x: x_updates, y: y_updates };
    Plotly.restyle(this.cpu_stats_div, update);

    this.cpu_stats_last_graph_update_time = timestamp;
}

GputopUI.prototype.update = function(timestamp) {
    var metric = this.active_oa_metric_;
    if (metric == undefined)
        return;

    this.update_oa_graphs(timestamp);


    this.update_cpu_stats(timestamp);

    for (var i = 0, l = metric.webc_counters.length; i < l; i++) {
        var counter = metric.webc_counters[i];
        this.update_counter(counter);
    }

    /* We want smooth graph panning and bar graph updates while we have
     * an active stream of metrics, so keep queuing redraws... */
    this.queue_redraw();
}

GputopUI.prototype.queue_redraw = function() {
    if (this.redraw_queued_)
        return;

    window.requestAnimationFrame((timestamp) => {
        this.redraw_queued_ = false;
        this.update(timestamp);
    });

    this.redraw_queued_ = true;
}

GputopUI.prototype.notify_metric_updated = function() {
    this.queue_redraw();
}

GputopUI.prototype.metric_not_supported = function(metric) {
    alert(" Metric not supported " + metric.title_)
}

/* XXX: this is essentially the first entry point into GputopUI after
 * connecting to the server, after Gputop has been initialized */
GputopUI.prototype.update_features = function(features) {
    if (features.devinfo.get_devid() == 0 ) {
        this.show_alert(" No device was detected, is it the functionality on kernel ? ","alert-danger");
    }

    $( "#gputop-gpu" ).html( this.get_arch_pretty_name() );

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

    this.load_metrics_panel(() => {
        var metric = this.lookup_metric_for_guid(global_guid);

        this.update_metric_period_exponent_for_zoom(metric);

        this.open_oa_metric_set({guid:global_guid});
    });

    this.cpu_stats_stream = this.open_cpu_stats({sample_period_ms: 300}, () => {
        console.log("CPU Stats stream open");

        this.cpu_stats_stream.on('update', (ev) => {
            var stats = ev.stats;

            if (this.cpu_stats_last == undefined) {
                this.cpu_stats_start = stats.cpus[0].get_timestamp().toNumber();
                this.cpu_stats_timestamp_updates = [];
                this.cpu_stats = new Array(stats.cpus.length);
                for (var i = 0; i < stats.cpus.length; i++) {
                    this.cpu_stats[i] = [];
                }
            } else {
                var last = this.cpu_stats_last;
                var last_time = last.cpus[0].get_timestamp().toNumber();
                var time = stats.cpus[0].get_timestamp().toNumber();
                var mid_time = last_time + ((time - last_time) / 2);

                console.assert(time > last_time, "Time went backwards!");
                /* Expect timestamps to be consistent for all core samples so
                 * just maintain one array of timestamp updates... */
                this.cpu_stats_timestamp_updates.push(mid_time);

                for (var i = 0; i < stats.cpus.length; i++) {
                    var core_last = last.cpus[i];
                    var core = stats.cpus[i];

                    var user = core.user - core_last.user;
                    var nice = core.nice - core_last.nice;
                    var system = core.system - core_last.system;
                    var idle = core.idle - core_last.idle;
                    var iowait = core.iowait - core_last.iowait;
                    var irq = core.irq - core_last.irq;
                    var softirq = core.softirq - core_last.softirq;
                    var steal = core.steal - core_last.steal;
                    var guest = core.guest - core_last.guest;
                    var guest_nice = core.guest_nice - core_last.guest_nice;

                    var total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;

                    var busy_percentage = 100 - ((idle / total) * 100);
                    this.cpu_stats[i].push(busy_percentage);

                    //console.log("CPU" + i + ": ts = " + mid_time + " busy = " + busy_percentage);
                }
            }
            this.cpu_stats_last = stats;
        });
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
    $('#log').append('<font color="' + color + '">' + log_message + '</font></br>');
}

GputopUI.prototype.syslog = function(message){
    console.log(message);
}

GputopUI.prototype.init_interface = function(callback) {

    if (this.demo_mode) {

        if (!this.demo_ui_loaded) {
            this.demo_ui_loaded = true;

            $('#gputop-welcome-panel').load("ajax/welcome.html");

            $('#gputop-entries').prepend('<li class="dropdown" id="metric-menu"></li>');
            $('#metric-menu').append('<a href="#" class="dropdown-toggle" type="button" id="dropdownMenu1" data-toggle="dropdown" aria-haspopup="true" aria-haspopup="true" aria-expanded="true">Inspect metric set</a>');
            $('#metric-menu').append('<ul id="demoMenu" class="dropdown-menu" aria-labelledby="dropdownMenu1"></ul>');

            var arch_list = [
                ["hsw", "Haswell"],
                ["chv", "Cherryview"],
                ["bdw", "Broadwell"],
                ["skl", "Skylake"],
            ];

            for (var i = 0; i < arch_list.length; i++)
                $("#demoMenu").append('<li><a href="#" onclick="gputop.set_demo_architecture(\'' + arch_list[i][0] + '\')">'+ arch_list[i][1] + '</a></li>');
        }

        $("#welcome").show();
        $("#build-instructions").show();
        $("#wiki").show();
        $('#welcome-tab-a').trigger('click');
    } else {
        $("#welcome").hide();
        $("#build-instructions").hide();
        $("#wiki").hide();
        $('#overview-tab-a').trigger('click');
    }


    if (!this.overview_loaded) {
        this.overview_loaded = true;

        $("#gputop-overview-panel" ).load( "ajax/overview.html", () => {
            // Display tooltips
            $('[data-toggle="tooltip"]').tooltip();

            if (callback !== undefined)
                callback();
        });
    } else {
        if (callback !== undefined)
            callback();
    }
}

GputopUI.prototype.load_metrics_panel = function(callback_success) {
    $( '#gputop-metrics-panel' ).load( "ajax/metrics.html", () => {
        console.log('Metrics panel loaded');
        callback_success();
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

GputopUI.prototype.reconnect = function(callback) {
    var address = $('#target_address').val() + ':' + $('#target_port').val();

    var current_demo_mode = this.demo_mode;

    if ($('#target_address').val() === "demo")
        this.demo_mode = true;
    else
        this.demo_mode = false;

    function do_connect() {
        $( ".gputop-connecting" ).show();
        this.connect(address, () => {
            $( ".gputop-connecting" ).hide();
            if (callback !== undefined)
                callback();
        });
    }

    if (this.demo_mode !== current_demo_mode) {
        this.init_interface(() => {
            do_connect.call(this);
        });
    } else
        do_connect.call(this);
}

GputopUI.prototype.dispose = function() {
    this.cpu_stats_stream = undefined;

    Gputop.prototype.dispose.call(this);
}
