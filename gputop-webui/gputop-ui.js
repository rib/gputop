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

function CounterUI (metricSetParent) {
    Counter.call(this, metricSetParent);

    this.graph_options = {
        grid: {
            borderWidth: 1,
            minBorderMargin: 20,
            labelMargin: 10,
            backgroundColor: {
                colors: ["#fff", "#e4f4f4"]
            },
            margin: {
                top: 8,
                bottom: 20,
                left: 20
            },
        },
        xaxis: {
            show: false,
            panRange: null
        },
        yaxis: {
            min: 0,
            max: 110,
            panRange: false
        },
        legend: {
            show: true
        },
        pan: {
            interactive: true
        }
    };// options


    this.row_div_ = undefined;
    this.row_marker_div_ = undefined;
    this.bar_div_ = undefined;
    this.txt_value_div_ = undefined;

    /* flot graph data */
    this.graph_data = [];
    this.graph_markings = [];
    this.flot_container = undefined;

    /* plotly.js graph data */
    this.y_data = [];

    this.zero = true; /* reset once we see any value > 0 for the counter
                         so inactive counters can be hidden by default */
}

CounterUI.prototype = Object.create(Counter.prototype);

function MetricSetUI (gputopParent) {
    Metric.call(this, gputopParent);

    this.filtered_counters = [];

    this.start_timestamp = 0;
    this.start_gpu_timestamp = 0;
    this.end_gpu_timestamp = 0;
}

MetricSetUI.prototype = Object.create(Metric.prototype);

/*
MetricSetUI.prototype.add_counter = function (counter) {
    Metric.prototype.add_counter.call(this, counter);

    $('#counter_list').append('<div>' + counter.name + '</div>');
}
*/

// remove all the data from all opened graphs
MetricSetUI.prototype.clear_metric_data = function() {

    Metric.prototype.clear_metric_data.call(this);

    this.start_timestamp = 0;

    for (var i = 0; i < this.cc_counters.length; i++) {
        var counter = this.cc_counters[i];
        counter.graph_data = [];
    }
}

var getUrlParameter = function(sParam) {
    var sPageURL = decodeURIComponent(window.location.search.substring(1)),
        sURLVariables = sPageURL.split('&'),
        sParameterName,
        i;

    for (i = 0; i < sURLVariables.length; i++) {
        sParameterName = sURLVariables[i].split('=');

        if (sParameterName[0] === sParam) {
            return sParameterName[1] === undefined ? true : sParameterName[1];
        }
    }
};


function GputopUI () {
    Gputop.call(this);
    this.MetricConstructor = MetricSetUI;
    this.CounterConstructor = CounterUI;

    /* NB: Although we initialize in demo mode in these conditions, it's still
     * possible to enter/leave demo mode interactively by specifying and
     * address:port for a target to connect to. (which is why we don't
     * do these checks in the .is_demo() method.
     */

    var demo = false;

    if (window.location.hostname == "gputop.github.io" ||
        window.location.hostname == "www.gputop.com")
        demo = true;

    var target = getUrlParameter('target');
    var port = getUrlParameter('port');

    if (target !== undefined)
        demo = false;

    var demo_param = getUrlParameter('demo');
    if (demo_param !== undefined && (demo_param === "true" ||
                                     demo_param === "1" ||
                                     demo_param === true))
        demo = true;

    if (demo === true) {
        $('#target_address').attr('value', "demo");
        this.demo_mode = true;
    } else {
        if (target === undefined)
            target = window.location.hostname;
        $('#target_address').attr('value', target);
        if (port !== undefined)
            $('#target_port').attr('value', port);
        this.demo_mode = false;
    }

    this.demo_ui_loaded = false;

    var debug_param = getUrlParameter('debug');
    if (debug_param !== undefined && (debug_param === "true" ||
                                     debug_param === "1" ||
                                     debug_param === true))
        this.debug_mode = true;
    else
        this.debug_mode = false;

    this.paused = false;
    this.current_metric_set = undefined;

    this.selected_counter = undefined;

    this.graph_array = [];
    this.zoom = 10; //seconds

    this.series = [{
        lines: {
            fill: true
        },
        color: "#6666ff"
    }];

    this.redraw_queued_ = false;
    this.filter_queued_ = false;

    this.cpu_stats_stream = undefined;

    this.cpu_stats_last = undefined;
    this.cpu_stats_start = 0;
    this.cpu_stats_start_js = 0;
    this.cpu_stats_last_graph_update_time = 0;
    this.cpu_stats = undefined;
    this.cpu_stats_div = undefined;

    this.gpu_metrics_graph_div = undefined;
    this.oa_counters_to_graph = [];

    this.oa_timestamps = [];
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

GputopUI.prototype.select_counter= function(counter) {
    var metric = counter.metric;

    if (gputop.selected_counter !== undefined) {
        gputop.selected_counter.row_div_.removeClass("counter-row-selected");
        gputop.selected_counter.row_marker_div_.css("visibility", "hidden");
    }

    gputop.selected_counter = counter;

    counter.row_div_.addClass("counter-row-selected");
    counter.row_marker_div_.css("visibility", "visible");

    var desc_template = `
        <h2>${counter.name}</h2>
        <h3>Summary:</h3>
        ${counter.description}
    `;
    $('#counter-description').empty();
    $('#counter-description').append(desc_template);
    // First queue the mathjax processing, then queue the .show html,
    // so that there will not be artifacts when changing equation
    $('#equation').hide();
    $('#max_equation').hide();

    $('#equation').html("<math display='block'><mstyle mathsize='1.3em' mathcolor='#337ab7' mathvariant='italic'>" +
            (counter.eq_xml).prop('innerHTML') + "</mstyle></math>");
    MathJax.Hub.Queue(["Typeset", MathJax.Hub, "equation"]);
    MathJax.Hub.Queue(function () { $('#equation').show(); });

    if (counter.max_eq_xml) {
        $('#max_equation').html("<math display='block'><mstyle mathsize='1.0em' mathcolor='#337ab7' mathvariant='italic'>" +
                (counter.max_eq_xml).prop('innerHTML') + "</mstyle></math>");
        MathJax.Hub.Queue(["Typeset", MathJax.Hub, "max_equation"]);
        MathJax.Hub.Queue(function () { $('#max_equation').show(); });
    }
}

GputopUI.prototype.filter_counters = function() {
    if (this.current_metric_set === metric)
        return;

    var metric = this.current_metric_set;

    var flags = ["Overview", "System", "Frame", "Batch", "Draw", "Indicate",
                 "Tier1", "Tier2", "Tier3", "Tier4"];
    var selected_flags = [];
    var visible_counters = [];

    flags.forEach((e, i) => {
        if ($('#' + e.toLowerCase() + '-check').prop('checked')) {
            selected_flags.push(e);
        }
    });

    var results = metric.filter_counters({
        flags: selected_flags,
        debug: this.debug_mode,
        active: $('#active-check').prop('checked')
    });

    if (results.matched.length > 0) {
        results.matched.forEach((counter) => {
            counter.row_div_.show();
        });

        this.select_counter(results.matched[0]);
    }

    results.others.forEach((counter) => {
        counter.row_div_.hide();
    });
}

GputopUI.prototype.select_metric_set = function(metric) {
    if (this.current_metric_set === metric)
        return;

    $("#metrics-dropdown-anchor").html('<h3>' + metric.name + '<span class="caret"></span></h3>');

    $('#counter_list').empty();
    for (var i = 0; i < metric.cc_counters.length; i++) {
        var counter = metric.cc_counters[i];
        var counter_row_id = "row_" + metric.underscore_name + '_' + counter.underscore_name;
        var select_marker_id = counter_row_id + "_marker";
        var bar_id = counter_row_id + "_bar";
        var txt_value_id = counter_row_id + "_value";
        var stats_toggle_id = counter_row_id + "_toggle";

        var row_template = `
<div class="metric_row" id="${counter_row_id}" data-toggle="tooltip" title="${counter.description}">
    <div class="counter-select-mark" id="${select_marker_id}" style="visibility: hidden;">❭</div>
    <div class="counter_name">${counter.name}</div>
    <div class="progress">
        <div id="${bar_id}" class="progress-bar progress-bar bar_adjust" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100" data-toggle="tooltip" data-placement="left"></div>
    </div>
    <input checked id="${stats_toggle_id}" class="counter-stats-button" data-toggle="tooltip" title="Add counter to trace view" data-on="<i class='glyphicon glyphicon-stats'/><i class='glyphicon glyphicon-arrow-up'/>" data-off="<i class='glyphicon glyphicon-pause'></i>" type="checkbox">
    <div class="progress_text" id="${txt_value_id}"></div>
</div>`;

        $('#counter_list').append(row_template);

        counter.row_div_ = $('#' + counter_row_id);
        counter.row_marker_div_ = $('#' + select_marker_id);
        counter.bar_div_ = $('#' + bar_id);
        counter.txt_value_div_ = $('#' + txt_value_id);
        counter.row_div_.data("counter", counter);
        counter.stats_toggle_div_ = $('#' +  stats_toggle_id);
        counter.stats_toggle_div_.data("counter", counter);
    }

    $('.counter-stats-button').bootstrapToggle();
    $('.counter-stats-button').css({
        "height": "100%",
    });
    $('.counter-stats-button').change((ev) => {
        var counter = $(ev.target).data("counter");
        counter.record_data = !$(ev.target).prop('checked');
        this.gpu_metrics_graph_div = undefined;
    });

    $('.metric_row').click((ev) => {
        var counter = $(ev.delegateTarget).data("counter");
        this.select_counter(counter);
    });
    $('.equation').click((ev) => {
        var counter = $(ev.target).data("counter");
        this.select_counter(counter);
    });

    this.filter_counters();

    /* XXX: hacky placeholder for now, but the demo mode should eventually
     * work in terms of a fake connection receiving dummy data...
     */
    if (this.demo_mode && this.gpu_metrics_graph_div === undefined) {
        this.gpu_metrics_graph_div = document.getElementById('gpu-metrics-graph');

        var layout = {
            margin: { b: 20, t: 20 },
            xaxis: { range: [1000000000, 2000000000], autorange: false, rangemode: 'normal', ticks: '', showticklabels: false },
            yaxis: { range: [0, 100], showgrid: false, title: "%" },
            showLegend: true
        };

        var config = {
            displayModeBar: false
        };

        var traces = [];

        var counter = metric.cc_counters[0];

        traces[0] = {
            name: counter.name,
            type: "scatter",
            mode: "lines",
            hoverinfo: "none",
            x: [1990000000, 2000000000],
            y: [0, 0],
        };

        Plotly.newPlot(this.gpu_metrics_graph_div,
                       traces,
                       layout,
                       config);
    }

    var config = {
        oa_exponent: this.update_metric_aggregation_period_for_zoom(metric),
        paused: this.paused,
    };

    if (this.current_metric_set !== undefined &&
        this.current_metric_set.open_config !== undefined)
    {
        var prev_metric = this.current_metric_set;

        this.current_metric_set = metric;

        prev_metric.close((ev) => {
            metric.open(config);
        });
    } else {
        this.current_metric_set = metric;

        metric.open(config);
    }
}

/* Also returns the maximum OA exponent suitable for viewing at the given
 * zoom level */
GputopUI.prototype.update_metric_aggregation_period_for_zoom = function (metric) {
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

    /* XXX maybe this function should refuse to reduce the aggregation
     * period lower than is valid with the current HW exponent */
    metric.set_aggregation_period(ns_per_pixel);

    return this.calculate_max_exponent_for_period(ns_per_pixel);
}

GputopUI.prototype.set_zoom = function(zoom) {
    this.zoom = zoom;

    var metric = this.current_metric_set;
    if (metric === undefined)
        return;

    // Note: we have to update the aggregation period even if we are in a
    // paused state where we are replying previously captured metric data.
    var exponent = this.update_metric_aggregation_period_for_zoom(metric);

    if (metric.open_config !== undefined) {
        if (exponent != metric.open_config.oa_exponent && !metric.open_config.paused) {

            var config = metric.open_config;
            metric.close((ev) => {
                config.oa_exponent = exponent;
                metric.open(config);
            });
        }

        if (metric.open_config.paused)
            metric.replay_buffer();

        this.queue_redraw();
    }
}

GputopUI.prototype.set_paused = function(paused) {
    if (this.paused === paused)
        return;

    var metric = this.current_metric_set;
    if (metric === undefined)
        return;

    this.gpu_metrics_graph_div = undefined;

    metric.set_paused(paused);

    this.paused = paused;
}

GputopUI.prototype.update_oa_graphs_paused = function (timestamp) {
    var metric = this.current_metric_set;
    if (metric === undefined)
        return;

    var n_counters = metric.cc_counters.length;
    if (n_counters === 0)
        return;

    var first_counter = undefined;
    for (var i = 0; i < metric.cc_counters.length; i++) {
        first_counter = metric.cc_counters[i];
        if (first_counter.record_data === true)
            break;
    }
    if (!first_counter)
        return;

    var n_updates = first_counter.updates.length;

    if (n_updates === 0)
        return;

    metric.start_gpu_timestamp = first_counter.updates[0][0];
    metric.end_gpu_timestamp = first_counter.updates[n_updates - 1][1];

    var time_range = this.zoom * 1000000000;
    var margin = time_range * 0.05;

    var x_max = metric.end_gpu_timestamp;
    var x_min = x_max - time_range;

    for (var i = 0; i < n_counters; i++) {
        var counter = metric.cc_counters[i];
        if (!counter.record_data)
            continue;

        var container = counter.flot_container;
        if (!container)
            continue;

        for (var j = 0; j < n_updates; j++) {
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
        counter.graph_options.xaxis.panRange = [null, null];

        var default_markings = create_default_markings(counter.graph_options.xaxis);
        counter.graph_options.grid.markings = default_markings.concat(counter.graph_markings);

        this.series[0].data = counter.graph_data;
        $.plot(container, this.series, counter.graph_options);

        counter.updates = [];
    }
}

GputopUI.prototype.update_oa_graphs = function(timestamp) {
    this.timestamp = timestamp;

    var metric = this.current_metric_set;
    if (metric === undefined)
        return;

    var n_counters = metric.cc_counters.length;
    if (n_counters === 0)
        return;

    var first_counter = undefined;
    for (var i = 0; i < metric.cc_counters.length; i++) {
        first_counter = metric.cc_counters[i];
        if (first_counter.record_data === true)
            break;
    }
    if (!first_counter)
        return;

    var n_updates = first_counter.updates.length;

    if (n_updates === 0)
        return;

    var latest_gpu_timestamp = first_counter.updates[n_updates - 1][1];

    if (metric.start_timestamp === 0) {
        metric.start_timestamp = timestamp;
        metric.start_gpu_timestamp = latest_gpu_timestamp;
    }
    var elapsed = (timestamp - metric.start_timestamp) * 1000000;

    var time_range = this.zoom * 1000000000;
    var margin = time_range * 0.05;

    var x_max = metric.start_gpu_timestamp + elapsed;
    var x_min = x_max - time_range;

    // data is saved in the graph for an interval of 20 seconds regardless
    // of the zoom value
    var max_graph_data = x_max - 20000000000;

    for (var i = 0; i < n_counters; i++) {
        var counter = metric.cc_counters[i];
        if (!counter.record_data)
            continue;

        var container = counter.flot_container;
        if (!container)
            continue;

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

        for (var j = 0; j < n_updates; j++) {
            var start = counter.updates[j][0];
            var end = counter.updates[j][1];
            var val = counter.updates[j][2];
            var max = counter.updates[j][3];
            var mid = start + (end - start) / 2;

            counter.graph_data.push([mid, val]);
            counter.graph_options.yaxis.max = 1.10 * counter.inferred_max; // add another 10% to the Y axis
        }

        counter.graph_options.xaxis.min = x_min + margin;
        counter.graph_options.xaxis.max = x_max - margin;
        counter.graph_options.xaxis.label = this.zoom + ' seconds';
        counter.graph_options.xaxis.panRange = false;

        var default_markings = create_default_markings(counter.graph_options.xaxis);
        counter.graph_options.grid.markings = default_markings.concat(counter.graph_markings);

        this.series[0].data = counter.graph_data;
        $.plot(container, this.series, counter.graph_options);

        counter.updates = [];
    }

    /* XXX: we currently have to come up with new synchronization points
     * periodically otherwise the clocks drift apart too much...
     */
    if (elapsed > 5000000000) {
        metric.start_timestamp = timestamp;
        metric.start_gpu_timestamp = latest_gpu_timestamp;
    }
}

GputopUI.prototype.update_gpu_metrics_graph = function (timestamp) {
    this.timestamp = timestamp;

    var metric = this.current_metric_set;
    if (metric === undefined)
        return;

    var n_counters = metric.cc_counters.length;
    if (n_counters === 0)
        return;

    var first_counter = undefined;
    for (var i = 0; i < metric.cc_counters.length; i++) {
        first_counter = metric.cc_counters[i];
        if (first_counter.record_data === true)
            break;
    }
    if (!first_counter)
        return;

    var n_updates = first_counter.updates.length;

    if (n_updates === 0)
        return;

    if (this.paused) {
        metric.start_gpu_timestamp = first_counter.updates[0][0];
        metric.end_gpu_timestamp = first_counter.updates[n_updates - 1][1];

        var time_range = this.zoom * 1000000000;
        var margin = time_range * 0.05;

        var x_max = metric.end_gpu_timestamp;
        var x_min = x_max - time_range;
    } else {
        var latest_gpu_timestamp = first_counter.updates[n_updates - 1][1];

        if (metric.start_timestamp === 0) {
            metric.start_timestamp = timestamp;
            metric.start_gpu_timestamp = latest_gpu_timestamp;
        }
        var elapsed = (timestamp - metric.start_timestamp) * 1000000;

        var time_range = this.zoom * 1000000000;
        var margin = time_range * 0.05;

        var x_max = metric.start_gpu_timestamp + elapsed;
        var x_min = x_max - time_range;
    }

    var counters = [];
    for (var i = 0; i < n_counters; i++) {
        var counter = metric.cc_counters[i];
        if (counter.record_data)
            counters.push(counter);
    }
    n_counters = counters.length;

    if (this.gpu_metrics_graph_div === undefined) {
        this.gpu_metrics_graph_div = document.getElementById('gpu-metrics-graph');

        var layout = {
            margin: { b: 20, t: 20 },
            //title: "GPU Metrics",
            xaxis: { range: [x_min + margin, x_max - margin], autorange: false, rangemode: 'normal', ticks: '', showticklabels: false },
            yaxis: { range: [0, 100], showgrid: false, title: "%" },
            showLegend: true
        };

        var config = {
            displayModeBar: false
        };

        if (this.paused) {
            layout.showticklabels = true;
            config.scrollZoom = true;
        }

        var traces = [];

        for (var i = 0; i < n_counters; i++) {
            var counter = counters[i];

            traces[i] = {
                name: counter.name,
                type: "scatter",
                mode: "lines",
                hoverinfo: "none",
                x: [],
                y: [],
            };
        }

        Plotly.newPlot(this.gpu_metrics_graph_div,
                       traces,
                       layout,
                       config);
    }

    var new_layout = {
        'xaxis.range': [x_min + margin, x_max - margin],
    };
    Plotly.relayout(this.gpu_metrics_graph_div, new_layout);


    var timestamps = this.oa_timestamps;
    for (var clip = 0, dlen = timestamps.length; clip < dlen && timestamps[clip] < x_min; clip++) {}
    if (clip > 0)
        timestamps = timestamps.slice(clip);

    for (var i = 0; i < n_updates; i++) {
        var start = first_counter.updates[i][0];
        var end = first_counter.updates[i][1];
        var mid = start + (end - start) / 2;

        timestamps.push(mid);
    }

    var y_updates = [];
    var x_updates = [];

    /* Some counters may not have any y_data yet if they've only just been
     * added and this is the length that counter.y_data needs to be
     * initialized too before appending updates so that we end up with
     * same length as timestamps[]
     */
    var clip_len = timestamps.length - n_updates;

    for (var i = 0; i < n_counters; i++) {
        var counter = counters[i];

        /* make sure each series of counter data has the same length
         * as timestamps[] */
        var y_data = counter.y_data;
        if (y_data === undefined)
            y_data = [];
        if (y_data.length < clip_len) {
            for (var j = y_data.length - 1; j < clip_len; j++) {
                y_data[j] =  0;
            }
        } else if (clip > 0)
            y_data = y_data.slice(clip);

        for (var j = 0; j < n_updates; j++) {
            var val = counter.updates[j][2];

            y_data.push(val);
        }

        x_updates[i] = timestamps;
        y_updates[i] = y_data;

        counter.updates = [];
        counter.y_data = y_data;
    }

    var update = { x: x_updates, y: y_updates };
    Plotly.restyle(this.gpu_metrics_graph_div, update);

    this.oa_timestamps = timestamps;

    /* XXX: we currently have to come up with new synchronization points
     * periodically otherwise the clocks drift apart too much...
     */
    if (this.paused === false && elapsed > 5000000000) {
        metric.start_timestamp = timestamp;
        metric.start_gpu_timestamp = latest_gpu_timestamp;
    }
}

GputopUI.prototype.update_counter = function(counter) {
    var bar_value = counter.latest_value;
    var text_value = counter.latest_value;
    var max = counter.inferred_max;
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

    if (counter.row_div_ === undefined)
        return;

    if (counter.zero && counter.latest_value !== 0) {
        counter.zero = false;
        this.queue_filter_counters();
    }

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

    if (max != 0) {
        counter.bar_div_.css("width", 100 * bar_value / max + "%");
        counter.txt_value_div_.text(text_value.toFixed(dp) + units_suffix);
    } else {
        counter.bar_div_.css("width", "0%");
        counter.txt_value_div_.text(text_value.toFixed(dp) + units_suffix);
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
                hoverinfo: "none",
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
    var metric = this.current_metric_set;
    if (metric === undefined)
        return;


    if (this.filter_queued_) {
        this.filter_counters();
        this.filter_queued_ = false;
    }

    if (metric.stream !== undefined) {
        this.update_gpu_metrics_graph(timestamp);

        /*
        if (metric.open_config.paused)
            this.update_oa_graphs_paused(timestamp);
        else
            this.update_oa_graphs(timestamp);
            */
    }

    this.update_cpu_stats(timestamp);

    for (var i = 0, l = metric.cc_counters.length; i < l; i++) {
        var counter = metric.cc_counters[i];
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

GputopUI.prototype.queue_filter_counters = function() {
    this.queue_redraw();
    this.filter_queued_ = true;
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

    $("#overview-notices").empty();
    features.notices.forEach((notice, i) => {
        $("#overview-notices").append(`<div class="alert alert-info"><strong>Info!</strong> ${notice}</div>`);
    });

    $("#gputop-gpu").html( this.get_arch_pretty_name() );

    $("#gputop-cpu").html( features.get_cpu_model() );
    $("#gputop-kernel-build").html( features.get_kernel_build() );
    $("#gputop-kernel-release").html( features.get_kernel_release() );
    $("#gputop-n-cpus").html( features.get_n_cpus() );

    $("#gputop-n-eus").html( features.devinfo.get_n_eus().toInt() );
    $("#gputop-n-eu-slices").html( features.devinfo.get_n_eu_slices().toInt()  );
    $("#gputop-n-eu-sub-slices").html( features.devinfo.get_n_eu_sub_slices().toInt()  );
    $("#gputop-n-eu-threads-count").html( features.devinfo.get_eu_threads_count().toInt()  );
    $("#gputop-minimum-frequency").html( features.devinfo.get_gt_min_freq().toInt() + "Hz"  );
    $("#gputop-maximum-frequency").html( features.devinfo.get_gt_max_freq().toInt() + "Hz"  );
    $("#gputop-timestamp-frequency").html( features.devinfo.get_timestamp_frequency().toInt() + "Hz"  );

    if (features.get_fake_mode())
        $("#metrics-tab-anchor").html("Metrics (Fake Mode) ");

    this.load_metrics_panel(() => {

        $('#graph-range-entry').on('change', () => {
            var val = Number($('#graph-range-entry').prop('value'));
            if (val === NaN)
                val = 10;
            if (val > 20)
                val = 20;
            else if (val < 0.1)
                val = 0.1;
            $('#graph-range-entry').prop('value', val);
            this.set_zoom(val);
        });
        $('#zoom-in').on('click', () => {
            $('#graph-range-entry').prop('value', Number($('#graph-range-entry').prop('value')) * 0.8 );
            this.set_zoom(Number($('#graph-range-entry').prop('value')));
        });
        $('#zoom-out').on('click', () => {
            $('#graph-range-entry').prop('value', Number($('#graph-range-entry').prop('value')) * 1.25 );
            this.set_zoom(Number($('#graph-range-entry').prop('value')));
        });

        this.set_zoom(Number($('#graph-range-entry').prop('value')));

        $('#pause-button').click(() => {
            var icon = $("#pause-button-icon");
            if(icon.hasClass('glyphicon-pause')) {
                icon.removeClass('glyphicon-pause');
                icon.addClass('glyphicon-play');

                gputop.set_paused(true);
            } else {
                icon.removeClass('glyphicon-play');
                icon.addClass('glyphicon-pause');

                gputop.set_paused(false);
            }
        });

        $("#metrics-menu-list").empty();

        for (var i = 0; i < this.metrics_.length; i++) {
            (function(metric){
                $("#metrics-menu-list").append('<li><a id="' + metric.guid_ + '" href="#">' + metric.name + '</a></li>');
                $("#" + metric.guid_).click(() => {
                    //$('#metrics-menu').addClass("active");
                    $('#metrics-tab-anchor').tab('show');
                    this.select_metric_set(metric);
                });
            }).call(this, this.metrics_[i]);
        }
        this.select_metric_set(this.metrics_[0]);

        if (this.current_tab !== undefined &&
            this.current_tab.id === "overview-tab-anchor")
                this.open_cpu_stats_stream();
    });
}

GputopUI.prototype.user_msg = function(message, level){
    if (level === undefined)
        level = this.LOG;

    // types of bootstrap alerts: alert-success alert-info alert-warning alert-danger
    switch (level) {
    case this.LOG:
        var alerttype = "alert-success";
        var dismiss_time = 2500;
        break;
    case this.WARN:
        var alerttype = "alert-warning";
        var dismiss_time = 3500;
        break;
    case this.ERROR:
        var alerttype = "alert-danger";
        var dismiss_time = 5000;
        break;
    default:
        console.error("User message given with unknown level " + level + ": " + message);
        return;
    }

    $('#alert_placeholder').append('<div id="alertdiv" class="alert ' +
        alerttype + '"><a class="close" data-dismiss="alert">×</a><span>'+message+'</span></div>')
        setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
            $("#alertdiv").remove();
        }, dismiss_time);
}

GputopUI.prototype.application_log = function(log_level, log_message){
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

Gputop.prototype.log = function(message, level)
{
    if (level === undefined)
        level = this.LOG;

    switch (level) {
        case this.LOG:
            console.log(message);
            break;
        case this.WARN:
            console.warn(message);
            break;
        case this.ERROR:
            console.error(message);
            break;
        default:
            console.error("Unknown log level " + level + ": " + message);
    }
}

GputopUI.prototype.init_interface = function(callback) {

    $('.sidebar [data-toggle="tab"]').on('hide.bs.tab', (e) => {
        if (this.current_tab === undefined)
            return;

        var name = this.current_tab.id.slice(0, -11);
        $('#' + name + '-sidebar').hide();

        switch (name) {
        case "overview":
            this.close_cpu_stats();
            break;
        //case "metrics":
        //    break;
        }
        this.current_tab = undefined;
    });

    $('.sidebar [data-toggle="tab"]').on('show.bs.tab', (e) => {
        this.current_tab = e.target;

        var name = this.current_tab.id.slice(0, -11);
        $('#tab-sidebars').children().each(function () {
            $(this).hide();
        });
        $('#' + name + '-sidebar').show();

        switch (name) {
        case "overview":

            if (this.features === undefined)
                return;
            if (this.features.n_cpus === 0)
                return;

            this.open_cpu_stats_stream();
            break;
        //case "metrics":
        //    break;
        }
    });


    if (this.demo_mode) {

        if (!this.demo_ui_loaded) {
            this.demo_ui_loaded = true;

            $('#welcome-tab-content').load("ajax/welcome.html");

            $('#gputop-entries').prepend('<li class="dropdown" id="metric-menu"></li>');
            $('#metric-menu').append('<a href="#" class="dropdown-toggle" type="button" id="dropdownMenu1" data-toggle="dropdown" aria-haspopup="true" aria-haspopup="true" aria-expanded="true">Set Demo Platform</a>');
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
        $('#welcome-tab-anchor').tab('show');
        this.current_tab = $('#welcome-tab-anchor')[0];
    } else {
        $("#welcome").hide();
        $("#build-instructions").hide();
        $("#wiki").hide();
        $('#overview-tab-anchor').tab('show');
        this.current_tab = $('#overview-tab-anchor')[0];
    }

    this.load_overview_panel(callback);

    $(window).resize(() => {
        if (this.gpu_metrics_graph_div !== undefined)
            Plotly.Plots.resize(this.gpu_metrics_graph_div);
    });
}

GputopUI.prototype.load_metrics_panel = function(onload) {
    $('#metrics-tab-content').empty();
    $('#metrics-sidebar').remove();
    $('#metrics-tab-content').load( "ajax/metrics.html", () => {
        console.log('Metrics panel loaded');
        $('#metrics-sidebar').appendTo('#tab-sidebars');

        if (this.current_tab === undefined ||
            this.current_tab.id !== "metrics-tab-anchor")
        {
            $('#metrics-sidebar').hide();
        }

        onload();
    });
}

GputopUI.prototype.load_overview_panel = function(onload) {
    $("#overview-tab-content").empty();
    $("#overview-tab-content").load( "ajax/overview.html", () => {
        // Display tooltips
        $('[data-toggle="tooltip"]').tooltip();

        if (onload !== undefined)
            onload();
    });
}

GputopUI.prototype.open_cpu_stats_stream = function() {
    if (this.cpu_stats_stream !== undefined) {
        this.log("Close open cpu stats stream before re-opening", this.ERROR);
        return;
    }

    this.cpu_stats_stream = this.request_open_cpu_stats({sample_period_ms: 300}, () => {
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

GputopUI.prototype.close_cpu_stats = function(onclose) {
    if (this.cpu_stats_stream === undefined)
        return;

    this.rpc_request('close_query', this.cpu_stats_stream.server_handle, (msg) => {
        this.cpu_stats_stream = undefined;
        if (onclose !== undefined)
            onclose();
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
        this.connect(address,
                     () => { //onopen
                         $( ".gputop-connecting" ).hide();
                         if (callback !== undefined)
                             callback();
                     },
                     () => { //onclose
                         this.user_msg("Disconnected: Retry in 5 seconds", this.WARN);
                         // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
                         setTimeout(this.reconnect.call(this, callback), 5000);
                     },
                     () => { // onerror
                         this.user_msg("Failed to connect to " + address, this.ERROR);
                     }
                    );
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
