#!/usr/bin/env node
'use strict';

/*
 * Copyright (C) 2016 Intel Corporation
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

const Gputop = require('gputop');
const blessed = require('blessed');
const contrib = require('blessed-contrib');
const sparkline = require('sparkline');
const fs = require('fs');
const ArgumentParser = require('argparse').ArgumentParser;

var screen = blessed.screen({
    smartCSR: true,
    debug: true,
    log: "flibble.txt"
});

var sidenav_width = 37;
var linechart_height = 20;

function TermCounter (metricSetParent) {
    Gputop.Counter.call(this, metricSetParent);

    this.graph_timestamps = [];
    this.graph_abs_data = [];
    this.graph_percent_data = [];
    this.update_series = false;
}

TermCounter.prototype = Object.create(Gputop.Counter.prototype);


function TermMetricSet (gputopParent) {
    Gputop.Metric.call(this, gputopParent);

    this.start_timestamp = 0;
    this.start_gpu_timestamp = 0;

    this.counters_scrollable = blessed.box({
        parent: gputopParent.metrics_tab,
        top: linechart_height,
        left: sidenav_width,
        width: '100%-' + sidenav_width,
        height: '100%-' + linechart_height,
        hidden: true,
        scrollable: true,
        scrollbar: true,
        mouse: true,
        keys: true,

        border: {
            type: 'line'
        },
    });

    this.counter_list = this.counters_scrollable;
    /*
    this.counter_list = blessed.box({
        parent: this.counters_scrollable,
        top: 0,
        left: 0,
        width: '100%',
        height: 1000,
        border: {
            type: 'line'
        },
    });
    */

    this.counter_list_offset = 0;
}

TermMetricSet.prototype = Object.create(Gputop.Metric.prototype);

TermMetricSet.prototype.add_counter = function (counter) {
    Gputop.Metric.prototype.add_counter.call(this, counter);

    function randomColor() {
        return [Math.random() * 255,Math.random()*255, Math.random()*255]
    }

    counter.line_series = { 
        style: {
            line: randomColor()
        },
        title: counter.name,
        x: [0, 1, 2, 4, 5, 6],
        y: [0, 1, 2, 4, 5, 6],
    };

    counter.record_data = true;

    counter.filtered_counters = [];

    this.queue_allocate = true;
}

TermMetricSet.prototype.allocate_counter_widgets = function () {

    this.queue_allocate = false;

    this.line_chart = contrib.line({
        style: {
            line: "yellow",
            text: "green",
            baseline: "black",
            //border: {
            //    type: 'line'
            //},
        },
        xLabelPadding: 3,
        xPadding: 5,
        showLegend: true,
        legend: {
            width: 20,
        },
        wholeNumbersOnly: false,
        label: 'Check "[ ] add to graph ⬏ " to see detailed trace here...',
        top: 1,
        left: sidenav_width,
        height: linechart_height,
        width: '100%-' + sidenav_width,
    });
    this.gputop.metrics_tab.append(this.line_chart);

    this.counter_list_offset = 1;

    var label_col_width = 30;

    for (var i = 0; i < this.cc_counters.length; i++) {
        var counter = this.cc_counters[i];

        var lpos = 0;

        var label = blessed.text({
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            width: label_col_width,
            height: 1,
            content: counter.name,
            //hoverText: counter.description,
        });

        lpos += label.width;

        var label = blessed.text({
            screen: screen,
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            width: 3,
            height: 1,
            content: '(ℹ)',
            hoverText: counter.description,
        });
        lpos += 4;

        /*
        counter.graph_check.on('check', () => {
            counter.update_series = true;
        });
        counter.graph_check.on('uncheck', () => {
            counter.update_series = false;
        });
        */

        //this.counter_list_offset++;

        //this.gputop.log("Add counter " + counter.name);

        counter.gauge = blessed.box({
            screen: screen,
            //parent: screen,
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            width: 10,
            height: 1,
            //left: '15%',
            //label: 'Progress',
            //stroke: 'green',
            //fill: 'white',
            style: {
                fg: 'green',
                bg: 'grey',
            }
        });
        lpos += counter.gauge.width;

        var label = blessed.text({
            screen: screen,
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            width: 3,
            height: 1,
            content: '%',
        });
        lpos += 2;

        //this.counter_list.append(counter.gauge);
        //this.gputop.log("Appended gauge");
        //counter.gauge.setPercent(25);

        counter.sparkline = blessed.box({
            screen: screen,
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            width: 50,
            height: 1,
            //label: 'spark',
            tags: true,
            style: {
                fg: 'blue',
                bg: 'grey',
            }
        })
        lpos += counter.sparkline.width + 1;

        counter.graph_check = blessed.checkbox({
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: lpos,
            height: 1,
            width: 20,
            text: "add to graph ⬏",
            mouse: true,
        });
        lpos += counter.graph_check.width;

        this.counter_list_offset++;

        //this.counter_list.append(counter.sparkline);

        //this.counter_list_offset += 5;
        /*
        //this.counter_list.append(counter.gauge);
        //counter.gauge.setPercent(25);
        */
        //this.counter_list_offset += 2;

        counter.baseline = blessed.Line({
            parent: this.counter_list,
            top: this.counter_list_offset,
            left: 0,
            width: '100%-2',
            height: 1,
            orientation: 'horizontal',
            //label: counter.name,
        });

        this.counter_list_offset++;
    }
}

function GputopTerm()
{
    Gputop.Gputop.call(this);
    this.MetricConstructor = TermMetricSet;
    this.CounterConstructor = TermCounter;

    this.zoom = 10; //seconds

    this.redraw_queued_ = false;

    this.previous_zoom = 0;

    this.cpu_stats_stream = undefined;

    this.cpu_stats_last = undefined;
    this.cpu_stats_start = 0;
    this.cpu_stats_start_js = 0;
    this.cpu_stats_last_graph_update_time = 0;
    this.cpu_stats = undefined;
    this.cpu_stats_div = undefined;


    this.current_metric_set = undefined;


    screen.title = "GPUTOP";
    this.screen = screen;

    this.overview_tab = blessed.box({
        parent: screen,
        top: 1,
        left: 0,
        width: '100%',
        height: '100%-1',
        hidden: true,
    });

    /*
    this.gauge = contrib.gauge({
        screen: screen,
        parent: this.overview_tab,
        top: 0,
        left: 0,
        width: 200,
        //left: '15%',
        label: 'Progress',
        stroke: 'green',
        fill: 'white',
    });

    this.overview_tab.append(this.gauge);
    //this.counter_list.append(counter.guage);
    this.gauge.setPercent(25);
    */

    this.metrics_tab = blessed.box({
        parent: screen,
        top: 1,
        left: 0,
        width: '100%',
        height: '100%-1',
        hidden: true,
    });

    this.sidenav = blessed.list({
        parent: this.metrics_tab,
        top: 0,
        left: 0,
        width: sidenav_width,
        height: "100%",
        tags: true,
        keys: true,
        mouse: true,
        border: {
            type: 'line'
        },
        style: {
            fg: 'white',
            bg: 'magenta',
            border: {
                fg: '#f0f0f0'
            },
            hover: {
                bg: 'green'
            }
        }
    });
    this.sidenav.on('select', (item, i) => {
        var guid = this.features.supported_oa_query_guids[i];
        var metric = this.lookup_metric_for_guid(guid);

        if (this.current_metric_set)
            this.current_metric_set.counters_scrollable.hide();

        this.current_metric_set = metric;
        metric.counters_scrollable.show();

        this.update_metric_aggregation_period_for_zoom(metric);
        this.open_oa_metric_set({guid: guid});
    });

    this.filter_box = blessed.box({
        parent: this.sidenav,
        top: '50%',
        left: 0,
        height: '50%',
        width: '100%-2',
        border: {
            type: 'line'
        },
        label: 'Filter options'
    });


    this.log_tab = blessed.box({
        parent: screen,
        top: 1,
        left: 0,
        width: '100%',
        height: '100%-1',
        hidden: true,
    });

    this.log_widget = blessed.log({
        parent: this.log_tab,
        top: 0,
        left: 0,
        width: '100%',
        height: '100%',
        tags: true,
        border: {
            type: 'line'
        },
        scrollable: true,
        allwaysScroll: true,
        scrollbar: {
            ch: ' ',
            inverse: true
        },
        input: true,
        keys: true,
        mouse: true,
        hidden: true,
        style: {
            fg: 'white',
            bg: 'magenta',
            border: {
                fg: '#f0f0f0'
            },
            hover: {
                bg: 'green'
            },
            scrollbar: {
                bg: 'blue'
            },
        }
    });

    this.tabs = [
        {
            name: "Overview",
            top_widget: this.overview_tab,
        },
        {
            name: "OA Metrics",
            top_widget: this.metrics_tab,
            enter: () => {
                this.sidenav.focus();
            },
            leave: () => {
            }
        },
        {
            name: "GL Metrics",
            top_widget: this.metrics_tab,
        },
        {
            name: "GL Tuneables",
            top_widget: this.metrics_tab,
        },
        {
            name: "Log",
            top_widget: this.log_tab,
        },
    ];

    var topnav_tabs = {};
    this.tabs.forEach((e, i, a) => {

        topnav_tabs[e.name] = {
            callback: () => {
                this.log("Switching to tab " + e.name);
                this.current_tab.top_widget.hide();
                if ('leave' in this.current_tab)
                    this.current_tab.leave();
                this.current_tab = e;
                e.top_widget.show();
                if ('enter' in e)
                    e.enter();
                else
                    e.top_widget.focus();
            }
        }
    });

    this.topnav = blessed.listbar({
        parent: screen,
        height: 1,
        autoCommandKeys: true,
        mouse: true,
        keys: true,
    });
    this.topnav.setItems(topnav_tabs);
    this.current_tab = this.tabs[1];
    this.current_tab.enter.call(this);

    this.console = {
        log: (msg) => {
            //screen.debug(msg);
            //this.log_widget.log(msg);
        },
        warn: (msg) => {
            //screen.debug("WARNING: " + msg);
            //this.log_widget.log("WARN: " + msg);
        },
        error: (msg) => {
            //screen.debug("ERROR: " + msg);
            //this.log_widget.log("ERROR: " + msg);
        },
    };
    //screen.append(this.log_widget);
    //this.log_widget.focus();

    this.messagebar = blessed.message({
        parent: screen,
        top: '100%-5',
        left: 'center',
        height: 3,
        width: '80%',
        border: {
            type: 'line'
        },
    });

    this.dialog = blessed.prompt({
        parent: screen,
        top: 'center',
        left: 'center',
        width: '80%',
        height: 'shrink',
        border: {
            type: 'line'
        },
        tags: true,
        keys: true,
        mouse: true,
        inputOnFocus: false,
        //label: 'Prompt',
    });

    this.spinner = blessed.loading({
        parent: screen,
    });


    // If our box is clicked, change the content.
    //box.on('click', function(data) {
    //  box.setContent('{center}Some different {red-fg}content{/red-fg}.{/center}');
    //  screen.render();
    //});

    // If box is focused, handle `enter`/`return` and give us some more content.
    /*
       box.key('enter', function(ch, key) {
       box.setContent('{right}Even different {black-fg}content{/black-fg}.{/right}\n');
       box.setLine(1, 'bar');
       box.insertLine(1, 'foo');
       screen.render();
       });
       */
    // Quit on Escape, q, or Control-C.
    screen.key(['escape', 'q', 'C-c'], function(ch, key) {
        return process.exit(0);
    });

}

GputopTerm.prototype = Object.create(Gputop.Gputop.prototype);

GputopTerm.prototype.user_msg = function(message, level){
    if (level === undefined)
        level = this.LOG;

    // types of bootstrap alerts: alert-success alert-info alert-warning alert-danger
    switch (level) {
    case this.LOG:
        //this.screen.debug("USER: " + message);
        this.messagebar.log(message);
        break;
    case this.WARN:
        //this.screen.debug("USER: WARNING: " + message);
        this.messagebar.log("WARNING: " + message);
        break;
    case this.ERROR:
        //this.screen.debug("USER: ERROR: " + message);
        this.messagebar.log("ERROR: " + message);
        break;
    default:
        this.console.error("User message given with unknown level " + level + ": " + message);
        return;
    }
}


GputopTerm.prototype.update_features = function(features)
{
    var render_basic_metric = undefined;

    this.features = features;

    for (var i = 0; i < features.supported_oa_query_guids.length; i++) {
        var guid = features.supported_oa_query_guids[i];
        var metric = this.lookup_metric_for_guid(guid);

        this.sidenav.add(metric.name);

        if (metric.name.indexOf("Render Basic") > -1)
            render_basic_metric = metric;
    }

    if (render_basic_metric) {
        this.update_metric_aggregation_period_for_zoom(render_basic_metric);
        this.open_oa_metric_set({guid: render_basic_metric.guid_});
    }

    this.screen.render();
}

/* returns true if the exponent changed and therefore the metric
 * stream needs to be re-opened, else false.
 */
GputopTerm.prototype.update_metric_aggregation_period_for_zoom = function (metric) {
    var hack_graph_size_px = 200;

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
    metric.set_aggregation_period(ns_per_pixel);
    var exponent = this.calculate_max_exponent_for_period(ns_per_pixel);

    if (metric.exponent != exponent) {
        metric.exponent = exponent;
        return true;
    } else
        return false;
}

GputopTerm.prototype.set_zoom = function(zoom) {
    this.zoom = zoom;

    var metric = this.active_oa_metric_;
    if (metric !== undefined) {
        // Note: we have to update the aggregation period even if we are in a
        // paused state where we are replying previously captured metric data.
        var exponent_changed = this.update_metric_aggregation_period_for_zoom(metric);

        if (exponent_changed && !global_paused_query)
            this.open_oa_metric_set({guid: metric.guid_});
    }

    this.queue_redraw();
}

GputopTerm.prototype.update_oa_graphs = function(timestamp) {

    var line_chart_series = [];
    var line_chart_max = 120;

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

    var x_min = 0;
    var x_max = 1;

    if (metric.start_timestamp === 0) {
        metric.start_timestamp = timestamp;
        metric.start_gpu_timestamp = first_counter.updates[n_updates - 1][0];
    }

    var elapsed = (timestamp - metric.start_timestamp) * 1000000;

    var time_range = this.zoom * 1000000000;
    var margin = time_range * 0.05;

    x_max = metric.start_gpu_timestamp + elapsed;
    x_min = x_max - time_range;

    // data is saved in the graph for an interval of 20 seconds regardless
    // of the zoom value
    var min_timestamp = x_max - 20000000000;

    for (var i = 0; i < n_counters; i++) {
        var counter = metric.cc_counters[i];

        if (!counter.record_data)
            continue;

        // remove the old samples from the graph data
        for (var j = 0; j < counter.graph_timestamps.length &&
            counter.graph_timestamps[j] < min_timestamp; j++) {}
        if (j > 0) {
            counter.graph_timestamps = counter.graph_timestamps.slice(j);
            counter.graph_abs_data = counter.graph_data.slice(j);
            counter.graph_percent_data = counter.graph_data.slice(j);
        }

        var save_index = -1;
        for (var j = 0; j < n_updates; j++) {
            var start = counter.updates[j][0];
            var end = counter.updates[j][1];
            var val = counter.updates[j][2];
            var max = counter.updates[j][3];
            var mid = start + (end - start) / 2;

            counter.graph_timestamps.push(mid);
            counter.graph_abs_data.push(val);
            counter.graph_percent_data.push(100 * (val / counter.inferred_max));
            save_index = j;
        }

        // resync the timestamps (the javascript timestamp with the counter timestamp)
        if (elapsed > 5000000000 && save_index > -1)
        {
            metric.start_timestamp = timestamp;
            metric.start_gpu_timestamp = counter.updates[save_index][0];
        }

        counter.sparkline.setContent(sparkline(counter.graph_percent_data.slice(-counter.sparkline.width),
                                               { min: 0, max: counter.inferred_max }));

        if (counter.graph_check.checked) {
            counter.line_series.y = counter.graph_percent_data.slice(-counter.sparkline.width);
            counter.line_series.x = counter.graph_timestamps.slice(-counter.sparkline.width);
            line_chart_series.push(counter.line_series);

//            if (counter.inferred_max > line_chart_max)
//                line_chart_max = counter.inferred_max;
        }

        // remove all the samples from the updates array
        counter.updates.splice(0, counter.updates.length);
    }

    if (line_chart_series.length) {
        metric.line_chart.options.maxY = line_chart_max;
        metric.line_chart.setData(line_chart_series);
    }
}

function range_bar(val, range, width)
{
    var bar_len = Math.round(width * 8 * val / range);
    var bars = [
        " ",
        "▏",
        "▎",
        "▍",
        "▌",
        "▋",
        "▊",
        "▉",
        "█"
    ];
    var str="";

    //return bars[8] + bars[8] + bars[3];

    for (var i = 0; i < width; i++) {
        if (bar_len > 8) {
            str += bars[8];
            bar_len -= 8;
        } else {
            str += bars[bar_len];
            bar_len = 0;
        }
    }

    return str;
}

/* TODO: move units utility code into gputop.js to share with webui */
GputopTerm.prototype.update_counter = function(counter) {
    var bar_value = counter.latest_value;
    var text_value = counter.latest_value;
    //var max = counter.latest_max;
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
        //counter.gauge.setPercent(100 * (bar_value / max));
        //counter.gauge.setContent(range_bar(100 * (bar_value / max), max, counter.gauge.width));
        counter.gauge.setContent(range_bar(100 * (bar_value / max), max, 10));
        //counter.gauge.setContent(range_bar(35, 100, 10));
        counter.bar_text = text_value.toFixed(dp) + units_suffix;
    } else {
        //counter.gauge.setPercent(0);
        counter.gauge.setContent("");
        counter.bar_text =text_value.toFixed(dp) + units_suffix;
    }
}

GputopTerm.prototype.update = function(timestamp) {
    var metric = this.active_oa_metric_;
    if (metric == undefined)
        return;

    if (metric.queue_allocate)
        metric.allocate_counter_widgets();

    this.update_oa_graphs(timestamp);

    //this.update_cpu_stats(timestamp);

    for (var i = 0, l = metric.cc_counters.length; i < l; i++) {
        var counter = metric.cc_counters[i];
        this.update_counter(counter);
    }

    /* We want smooth graph panning and bar graph updates while we have
     * an active stream of metrics, so keep queuing redraws... */
    this.queue_redraw();
}

GputopTerm.prototype.queue_redraw = function() {
    if (this.redraw_queued_)
        return;

    setTimeout((timestamp) => {
        this.redraw_queued_ = false;
        this.update(timestamp);
        this.screen.render();
    }, 32);

    this.redraw_queued_ = true;
}


//GputopTerm.prototype.log = function(message, level)
//{
//}

GputopTerm.prototype.notify_metric_updated = function(metric) {
    this.queue_redraw();
}

var parser = new ArgumentParser({
    version: '0.0.1',
    addHelp: true,
    description: "GPU Top CSV Dump Tool"
});

parser.addArgument(
    [ '-a', '--address' ],
    {
        help: 'host:port to connect to (default localhost:7890)',
        defaultValue: 'localhost:7890'
    }
);

var args = parser.parseArgs();

var gputop;

function init() {
    gputop = new GputopTerm();

    gputop.screen.render();

    function _connect(address) {
        if (!address)
            address = "localhost:7890";

        function _prompt() {
            //gputop.dialog.focus();
            gputop.dialog.input("Target Address:", address, (err, addr) => {
                setTimeout(() => {
                    _connect(addr);
                });
            });
        }

        gputop.screen.debug("Connect iteration: " + address);
        gputop.spinner.load("Connecting...");
        gputop.connect(address,
                       () => { //onopen
                           gputop.spinner.stop();
                       },
                       () => { //onclose
                           _prompt();
                       },
                       () => { //onerror
                           gputop.spinner.stop();
                           gputop.user_msg("Failed to connect to " + address);
                       });
    }

    _connect(args.address);
}

init();
