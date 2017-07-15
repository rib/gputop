"use strict";

//# sourceURL=gputop.js
// https://google.github.io/styleguide/javascriptguide.xml

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

var is_nodejs = false;
var using_emscripten = true;

if (typeof module !== 'undefined' && module.exports) {

    var WebSocket = require('ws');
    var ProtoBuf = require("protobufjs");
    var fs = require('fs');
    var jsdom = require('jsdom');
    var $ = require('jquery')(jsdom.jsdom().defaultView);
    var path = require('path');

    var cc = undefined;

    /* For unit testing we support running node.js tools with the Emscripten
     * compiled webc code, to cover more code in common with the web ui...
     */
    if (process.env.GPUTOP_NODE_USE_WEBC !== undefined) {
        using_emscripten = true;
        cc = require("./gputop-web.js");
        cc.gputop_singleton = undefined;
    } else {
        using_emscripten = false;
        cc = require("gputop-client-c");
        cc.gputop_singleton = undefined;

        /* For code compatibility with using the Emscripten compiled bindings... */
        cc.ALLOC_STACK = 0;
        cc.Runtime = { stackSave: function() { return 0; },
                       stackRestore: function(sp) {} };
        cc.allocate = function (data, type, where) { return data; };
        cc.intArrayFromString = function (str) { return str; };

        var client_data_path = require.resolve("gputop-data");
        client_data_path = path.resolve(client_data_path, "..");
    }

    var install_prefix = __dirname;

    is_nodejs = true;
} else {
    var cc = undefined;

    ProtoBuf = dcodeIO.ProtoBuf;
    var $ = window.jQuery;
}

function get_file(filename, load_callback, error_callback) {
    if (is_nodejs) {
        var full_path = path.join(client_data_path, filename);

        fs.readFile(full_path, 'utf8', (err, data) => {
            if (err)
                error_callback(err);
            else
                load_callback(data);
        });
    } else {
        var req = new XMLHttpRequest();
        req.open('GET', filename);
        req.onload = function () { load_callback(req.responseText); };
        req.onerror = error_callback;
        req.send();
    }
}

function sanitize_address(address) {
    var arr = address.split(':');

    if (arr[0].length < 1)
        arr[0] = 'localhost';
    if (arr.length < 2 || arr[1].length < 1)
        arr[1] = '7890';

    return arr[0] + ':' + arr[1];
}

function Counter (metricParent) {

    this.metric = metricParent;

    /* Index into metric.cc_counters_, understood by gputop-web.c code */
    this.cc_counter_id_ = -1;
    this.name = '';
    this.symbol_name = '';
    this.supported_ = false;
    this.xml_ = "<xml/>";

    /* Not all counters have a constant or equation for the maximum
     * and so we simply derive a maximum based on the largest value
     * we've seen */
    this.inferred_max = 0;

    this.units = '';

    /* whether append_counter_data() should really append to counter.updates[] */
    this.record_data = false;

    this.eq_xml = ""; // mathml equation
    this.max_eq_xml = ""; // mathml max equation

    this.duration_dependent = true;
    this.units_scale = 1; // default value
}

function Metric (gputopParent) {
    this.gputop = gputopParent; /* yay for mark-sweep */

    this.name = "not loaded";
    this.symbol_name = "UnInitialized";
    this.chipset_ = "not loaded";

    this.uuid = "undefined";
    this.metric_set_index_ = 0; //index into gputop.metrics[]

    this.xml_ = "<xml/>";
    this.supported_ = false;
    this.counters_ = [];     /* All possible counters associated with this metric
                              * set (not necessarily all supported by the current
                              * system */
    this.cc_counters = []; /* Counters applicable to this system, supported via
                              * gputop-web.c */
    this.counters_map_ = {}; // Map of counters by with symbol_name

    this.open_config = undefined;
    this.server_handle = 0;

    this.oa_accumulators = [];
}

Metric.prototype.find_counter_by_name = function(symbol_name) {
    return this.counters_map_[symbol_name];
}

/* FIXME: some of this should be handled in the Counter constructor */
Metric.prototype.add_counter = function(counter) {
    var symbol_name = counter.symbol_name;

    var sp = cc.Runtime.stackSave();

    var counter_idx = cc._gputop_cc_get_counter_id(String_pointerify_on_stack(this.uuid),
                                                   String_pointerify_on_stack(symbol_name));

    cc.Runtime.stackRestore(sp);

    counter.cc_counter_id_ = counter_idx;
    if (counter_idx != -1) {
        counter.supported_ = true;
        this.gputop.log('  Added available counter ' + counter_idx + ": " + symbol_name);
        this.cc_counters[counter_idx] = counter;
    } else {
        this.gputop.log('  Not adding unavailable counter:' + symbol_name);
    }

    this.counters_map_[symbol_name] = counter;
    this.counters_.push(counter);
}

Metric.prototype.filter_counters = function(options) {
    var flags = options.flags;
    var results = {
        matched: [],
        others: []
    };

    var debug = false;
    if (options.debug !== undefined)
        debug = options.debug;

    var active = true;
    if (options.active !== undefined)
        active = options.active;

    for (var i = 0; i < this.cc_counters.length; i++) {
        var counter = this.cc_counters[i];
        var filter = true;

        for (var j = 0; j < flags.length; j++) {
            if (counter.flags.indexOf(flags[j]) < 0) {
                filter = false;
                break;
            }
        }

        if (debug === false) {
            if (counter.symbol_name === "GpuTime")
                filter = false;
        }

        if (active && counter.zero)
            filter = false;

        if (filter)
            results.matched.push(counter);
        else
            results.others.push(counter);
    }

    return results;
}

function Process_info () {
    this.pid_ = 0;
    this.process_name_ = "empty";
    this.cmd_line_ = "empty";
    this.active_ = false;
    this.process_path_ = "empty";

    // List of ctx ids on this process
    this.context_ids_ = [];

    // Did we ask gputop about this process?
    this.init_ = false;
}

Process_info.prototype.update = function(process) {
    this.pid_ = process.pid;
    this.cmd_line_ = process.cmd_line;
    var res = this.cmd_line_.split(" ", 2);

    this.process_path_ = res[0];

    var path = res[0].split("/");
    this.process_name_ = path[path.length-1];
    this.update_process(this);
}

function Gputop () {

    this.connection = undefined;

    /* To support being able to redirect the output of node.js tools
     * we redirect all console logging to stderr... */
    if (is_nodejs)
        this.console = new console.Console(process.stderr, process.stderr);
    else
        this.console = console;

    this.LOG=0;
    this.WARN=1;
    this.ERROR=2;

    this.metrics_ = [];
    this.map_metrics_ = {}; // Map of metrics by UUID

    this.tracepoints_ = [];

    this.is_connected_ = false;

    this.config_ = {
        architecture: 'ukn'
    }
    this.demo_architecture =  "hsw";

    this.system_properties = {};

    this.builder_ = undefined;
    this.gputop_proto_ = undefined;

    this.socket_ = undefined;

    /* When we send a request to open a stream of metrics we send
     * the server a handle that will be attached to subsequent data
     * for the stream. We use these handles to lookup the metric
     * set that the data corresponds to.
     */
    this.next_server_handle = 1;
    this.server_handle_to_obj = {};

    /* When we open a stream of metrics we also call into the
     * Emscripten compiled cc code to allocate a corresponding
     * struct gputop_cc_stream. This map lets us look up a
     * Metric object given a gputop_cc_stream pointer.
     */
    this.cc_stream_ptr_to_obj_map = {};
    this.cc_oa_accumulator_ptr_to_obj_map = {}

    this.current_update_ = { metric: null };

    // Pending RPC request closures, indexed by request uuid,
    // to be called once we receive a reply.
    this.rpc_closures_ = {};

    // Process list map organized by PID
    this.map_processes_ = [];

    if (is_nodejs) {
        this.test_mode = false;
    } else {
        var test = getUrlParameter('test');
        if (test === "true" || test === "1")
            this.test_mode = true;
        else
            this.test_mode = false;
    }
    this.test_log_messages = [];

    this.test_log("Global Gputop object constructed");


    // Enable tools to subclass metric sets and counters even though
    // gputop.js is responsible for allocating these objects...
    this.MetricConstructor = Metric;
    this.CounterConstructor = Counter;
}

Gputop.prototype.is_demo = function() {
    return false;
}

Gputop.prototype.application_log = function(level, message)
{
    this.console.log("APP LOG: (" + level + ") " + message.trim());
}

Gputop.prototype.log = function(message, level)
{
    if (level === undefined)
        level = this.LOG;

    switch (level) {
    case this.LOG:
        this.console.log(message);
        break;
    case this.WARN:
        this.console.warn("WARN:" + message);
        break;
    case this.ERROR:
        this.console.error("ERROR:" + message);
        break;
    default:
        this.console.error("Unknown log level " + level + ": " + message);
    }
}

Gputop.prototype.user_msg = function(message, level)
{
    this.log(message, level);
}

/* For unit test feedback, sent back to server in test mode */
Gputop.prototype.test_log = function(message) {
    if (this.test_mode) {
        this.test_log_messages.push(message);
        this.flush_test_log();
    }
}

Gputop.prototype.flush_test_log = function() {
    if (this.socket_) {
        for (var i = 0; i < this.test_log_messages.length; i++) {
            this.rpc_request('test_log', this.test_log_messages[i]);
        }
        this.test_log_messages = [];
    }
}

Gputop.prototype.get_process_by_pid = function(pid) {
    var process = this.map_processes_[pid];
    if (process == undefined) {
        process = new Process_info();
        this.map_processes_[pid] = process;
    }
    return process;
}

Gputop.prototype.get_metrics_xml = function() {
    return this.metrics_xml_;
}

Gputop.prototype.parse_counter_xml = function(metric, xml_elem) {
    var $cnt = $(xml_elem);

    var counter = new metric.gputop.CounterConstructor(metric);
    counter.name = $cnt.attr("name");
    counter.symbol_name = $cnt.attr("symbol_name");
    counter.underscore_name = $cnt.attr("underscore_name");
    counter.description = $cnt.attr("description");
    counter.flags = $cnt.attr("mdapi_usage_flags").split(" ");
    counter.eq_xml = ($cnt.find("mathml_EQ"));
    counter.max_eq_xml = ($cnt.find("mathml_MAX_EQ"));
    if (counter.max_eq_xml.length == 0)
        counter.max_eq_xml = undefined;
    counter.xml_ = $cnt;

    var units = $cnt.attr("units");
    if (units === "us") {
        units = "ns";
        counter.units_scale = 1000;
    }
    if (units === "mhz") {
        units = "hz";
        counter.units_scale *= 1000000;
    }
    counter.units = units;

     if (units === 'hz' || units === 'percent')
         counter.duration_dependent = false;

    metric.add_counter.call(metric, counter);
}

Gputop.prototype.get_metric_by_id = function(idx){
    return this.metrics_[idx];
}

Gputop.prototype.lookup_metric_for_uuid = function(uuid){
    var metric;
    if (uuid in this.map_metrics_) {
        metric = this.map_metrics_[uuid];
    } else {
        metric = new this.MetricConstructor(this);
        metric.uuid = uuid;
        this.map_metrics_[uuid] = metric;
    }
    return metric;
}

Gputop.prototype.parse_metrics_set_xml = function (xml_elem) {
    var uuid = $(xml_elem).attr("hw_config_guid");
    var metric = this.lookup_metric_for_uuid(uuid);
    metric.xml_ = $(xml_elem);
    metric.name = $(xml_elem).attr("name");

    this.log('Parsing metric set:' + metric.name);
    this.log("  HW config UUID: " + uuid);

    metric.symbol_name = $(xml_elem).attr("symbol_name");
    metric.underscore_name = $(xml_elem).attr("underscore_name");
    metric.chipset_ = $(xml_elem).attr("chipset");

    // We populate our array with metrics in the same order as the XML
    metric.metric_set_index_ = Object.keys(this.metrics_).length;
    this.metrics_[metric.metric_set_index_] = metric;

    $(xml_elem).find("counter").each((i, elem) => {
        this.parse_counter_xml(metric, elem);
    });
}

Gputop.prototype.clear_accumulated_metrics = function(metric) {
    for (var i = 0; i < metric.oa_accumulators.length; i++) {
        var accumulator = metric.oa_accumulators[i];

        for (var j = 0; j < accumulator.accumulated_counters.length; j++) {
            var accumulated_counter = accumulator.accumulated_counters[j];

            accumulated_counter.updates = [];
        }
    }
}

Gputop.prototype.replay_i915_perf_history = function(metric) {
    this.clear_accumulated_metrics(metric);

    var stream = metric.stream;

    for (var i = 0; i < this.i915_perf_history.length; i++) {
        var data = this.i915_perf_history[i];

        var sp = cc.Runtime.stackSave();

        var stack_data = cc.allocate(data, 'i8', cc.ALLOC_STACK);

        var n_accumulators = metric.oa_accumulators.length;

        /* XXX: unfortunately we can't handle passing an array of accumulator
         * pointers consistently between Node.js and Emscripten bindings.
         * With Emscripten we're passing a packed array of uint32 integers
         * as pointers, while for Node.js we want to pass a standard Javascript
         * array and let the binding implementation map this into an array
         * before calling the real C function.
         */
        if (using_emscripten) {
            /* Note: 2nd type arg ignored when 1st arg is a size/number in bytes*/
            var vec = cc.allocate(4 * n_accumulators, '*', cc.ALLOC_STACK);
            for (var j = 0; j < n_accumulators; j++) {
                var accumulator = metric.oa_accumulators[j];
                cc.setValue(vec + j * 4, accumulator.cc_accumulator_ptr_, '*');
            }

            if (stream.cc_stream_ptr_ === 0) {
                gputop.log("NULL CC Stream while replaying i915 perf message", this.ERROR);
            } else
                cc._gputop_cc_handle_i915_perf_message(stream.cc_stream_ptr_,
                                                       stack_data,
                                                       data.length,
                                                       vec,
                                                       n_accumulators);
        } else {
            var vec = [];
            for (var j = 0; j < n_accumulators; j++) {
                var accumulator = metric.oa_accumulators[j];
                vec.push(accumulator.cc_accumulator_ptr_);
            }

            cc._gputop_cc_handle_i915_perf_message(stream.cc_stream_ptr_,
                                                   stack_data,
                                                   data.length,
                                                   vec,
                                                   n_accumulators);
        }

        cc.Runtime.stackRestore(sp);
    }
}

Gputop.prototype.accumulator_filter_events = function (metric, accumulator, events_mask) {
    if (events_mask & 1) //period elapsed
        return true;
    else
        return false; //currently ignore context switch events
}

Gputop.prototype.accumulator_start_update = function (stream_ptr,
                                                      accumulator_ptr,
                                                      events_mask,
                                                      start_timestamp,
                                                      end_timestamp) {
    var update = this.current_update_;

    console.assert(update.metric === null, "Started stream update before finishing previous update");

    if (!(stream_ptr in this.cc_stream_ptr_to_obj_map)) {
        console.error("Ignoring spurious update for unknown stream");
        update.metric = null;
        return false;
    }

    if (!(accumulator_ptr in this.cc_oa_accumulator_ptr_to_obj_map)) {
        console.error("Ignoring spurious update for unknown OA accumulator");
        update.metric = null;
        return false;
    }

    var metric = this.cc_stream_ptr_to_obj_map[stream_ptr];
    var accumulator = this.cc_oa_accumulator_ptr_to_obj_map[accumulator_ptr];

    if (!this.accumulator_filter_events(metric, accumulator, events_mask)) {
        update.metric = null;
        return false;
    }

    update.metric = metric;
    update.accumulator = accumulator;
    update.start_timestamp = start_timestamp;
    update.end_timestamp = end_timestamp;
    update.events_mask = events_mask;

    return true;
}

Gputop.prototype.accumulator_append_count = function (counter_id,
                                                      max,
                                                      value) {
    var update = this.current_update_;

    var metric = update.metric;
    if (metric === null) {
        /* Will have already logged an error when starting the update */
        return;
    }

    if (counter_id >= metric.cc_counters.length) {
        console.error("Ignoring spurious counter update for out-of-range counter index " + counter_id);
        return;
    }

    var accumulator = update.accumulator;

    var counter = metric.cc_counters[counter_id];

    if (counter_id >= accumulator.accumulated_counters.length) {
        for (var i = accumulator.accumulated_counters.length; i <= counter_id; i++) {
            accumulator.accumulated_counters[i] = {
                counter: metric.cc_counters[i],
                latest_value: 0,
                latest_max: 0,
                updates: [],
            };
        }
    }

    var accumulated_counter = accumulator.accumulated_counters[counter_id];

    var reason = update.reason;

    var start_timestamp = update.start_timestamp;
    var end_timestamp = update.end_timestamp;
    var duration = end_timestamp - start_timestamp;

    value *= counter.units_scale;
    max *= counter.units_scale;

    if (counter.duration_dependent && (duration !== 0)) {
        var per_sec_scale = 1000000000 / duration;
        value *= per_sec_scale;
        max *= per_sec_scale;
    }

    if (counter.record_data) {
        accumulated_counter.updates.push([start_timestamp, end_timestamp, value, max, reason]);
        if (accumulated_counter.updates.length > 2000) {
            console.warn("Discarding old counter update (> 2000 updates old)");
            accumulated_counter.updates.shift();
        }
    }

    if (accumulated_counter.latest_value !== value ||
        accumulated_counter.latest_max !== max)
    {
        accumulated_counter.latest_value = value;
        accumulated_counter.latest_max = max;

        if (value > counter.inferred_max)
            counter.inferred_max = value;
        if (max > counter.inferred_max)
            counter.inferred_max = max;
    }
}

Gputop.prototype.accumulator_end_update = function () {
    var update = this.current_update_;

    var metric = update.metric;
    if (metric === null) {
        /* Will have already logged an error when starting the update */
        return;
    }

    update.metric = null;

    this.notify_accumulator_events(metric,
                                   update.accumulator,
                                   update.events_mask);
}

Gputop.prototype.accumulator_clear = function (accumulator) {
    cc._gputop_cc_oa_accumulator_clear(accumulator.cc_accumulator_ptr_);
}

Gputop.prototype.format_counter_value = function(accumulated_counter, compact) {
    var counter = accumulated_counter.counter;
    var value = accumulated_counter.latest_value;
    var max = counter.inferred_max;
    var units = counter.units;
    var units_suffix = "";
    var dp = 0;
    var kilo = counter.units === "bytes" ? 1024 : 1000;
    var mega = kilo * kilo;
    var giga = mega * kilo;
    if (compact === false) {
        var scale = {"bytes":["B", "KiB", "MiB", "GiB"],
                     "ns":["ns", "μs", "ms", "s"],
                     "hz":["Hz", "KHz", "MHz", "GHz"],
                     "texels":[" texels", "K texels", "M texels", "G texels"],
                     "pixels":[" pixels", "K pixels", "M pixels", "G pixels"],
                     "cycles":[" cycles", "K cycles", "M cycles", "G cycles"],
                     "threads":[" threads", "K threads", "M threads", "G threads"]};
    } else {
        var scale = {"bytes":["", "KiB", "MiB", "GiB"],
                     "ns":["", "μs", "ms", "s"],
                     "hz":["", "KHz", "MHz", "GHz"],
                     "texels":["", "KT", "MT", "GT"],
                     "pixels":["", "KP", "MP", "GP"],
                     "cycles":["", "ᴇ3", "ᴇ6", "ᴇ9"],
                     "threads":["", "ᴇ3", "ᴇ6", "ᴇ9"]};
    }

    if ((units in scale)) {
        dp = 1;
        if (value >= giga) {
            units_suffix = scale[units][3];
            value /= giga;
        } else if (value >= mega) {
            units_suffix = scale[units][2];
            value /= mega;
        } else if (value >= kilo) {
            units_suffix = scale[units][1];
            value /= kilo;
        } else
            units_suffix = scale[units][0];
    } else if (units === 'percent') {
        units_suffix = compact === true ? '' : '%';
        dp = 1;
    }

    if (compact !== true && counter.duration_dependent)
        units_suffix += '/s';

    return value.toFixed(dp) + units_suffix;
}

Gputop.prototype.parse_xml_metrics = function(xml) {
    this.metrics_xml_ = xml;

    $(xml).find("set").each((i, elem) => {
        this.parse_metrics_set_xml(elem);
    });
    if (this.is_demo())
        $('#gputop-metrics-panel').load("ajax/metrics.html");
}

Gputop.prototype.set_demo_architecture = function(architecture) {
    this.dispose();

    this.demo_architecture = architecture;
    this.is_connected_ = true;
    this.request_features();
}

Gputop.prototype.set_architecture = function(architecture) {
    this.config_.architecture = architecture;
}

Metric.prototype.open = function(config,
                                 onopen,
                                 onclose,
                                 onerror) {

    var stream = new Stream(this.gputop.next_server_handle++);

    if (onopen !== undefined)
        stream.on('open', onopen);

    if (onclose !== undefined)
        stream.on('close', onclose);

    if (onerror !== undefined)
        stream.on('error', onerror);

    if (this.supported_ == false) {
        var ev = { type: "error", msg: this.uuid + " " + this.name + " not supported by this kernel" };
        stream.dispatchEvent(ev);
        return null;
    }

    if (this.closing_) {
        var ev = { type: "error", msg: "Can't open metric while also waiting for it to close" };
        stream.dispatchEvent(ev);
        return null;
    }

    if (this.stream != undefined) {
        var ev = { type: "error", msg: "Can't re-open OA metric without explicitly closing first" };
        stream.dispatchEvent(ev);
        return null;
    }

    if (config === undefined)
        config = {};

    if (config.oa_exponent === undefined)
        config.oa_exponent = 14;
    if (config.paused === undefined)
        config.paused = false;

    this.open_config = config;
    this.stream = stream;

    function _finalize_open() {
        var ev = { type: "open" };
        stream.dispatchEvent(ev);
    }

    function _alloc_cc_stream() {
        this.gputop.log("Opened OA metric set " + this.name);

        var sp = cc.Runtime.stackSave();

        stream.cc_stream_ptr_ =
            cc._gputop_cc_oa_stream_new(String_pointerify_on_stack(this.uuid));

        cc.Runtime.stackRestore(sp);

        this.gputop.cc_stream_ptr_to_obj_map[stream.cc_stream_ptr_] = this;
    }

    if (config.paused === true) {
        stream.server_handle = 0;
        _alloc_cc_stream.call(this);
        _finalize_open.call(this);
    } else {
        var oa_stream = new this.gputop.gputop_proto_.OAStreamInfo();

        oa_stream.set('uuid', this.uuid);
        oa_stream.set('period_exponent', config.oa_exponent);
        oa_stream.set('per_ctx_mode', false); /* TODO: add UI + way to select a specific ctx */

        var open = new this.gputop.gputop_proto_.OpenStream();

        open.set('id', stream.server_handle);
        open.set('oa_stream', oa_stream);
        open.set('overwrite', false);   /* don't overwrite old samples */
        open.set('live_updates', true); /* send live updates */

        this.gputop.server_handle_to_obj[open.id] = this;

        _alloc_cc_stream.call(this);

        this.gputop.rpc_request('open_stream', open, _finalize_open.bind(this));

        this.gputop.i915_perf_history = [];
        this.gputop.i915_perf_history_size = 0;
    }

    return stream;
}

/* Note that this can only be called for a Metric with an open stream */
Metric.prototype.create_oa_accumulator = function(config) {

    var stream = this.stream;

    if (stream === undefined || stream.cc_stream_ptr_ === 0) {
        this.gputop.log("Can't create OA accumulator for Metric without open stream", this.gputop.ERROR);
        return;
    }

    if (config.period_ns === undefined)
        config.period_ns = 1000000000;
    if (config.enable_ctx_switch_events === undefined)
        config.enable_ctx_switch_events = false;

    this.gputop.log("Creating accumulator with aggregation period of " + config.period_ns + "ns", this.gputop.LOG);

    var accumulator = {};

    accumulator.accumulated_counters = [];

    var sp = cc.Runtime.stackSave();

    accumulator.cc_accumulator_ptr_ =
        cc._gputop_cc_oa_accumulator_new(stream.cc_stream_ptr_,
                                         config.period_ns,
                                         config.enable_ctx_switch_events);

    cc.Runtime.stackRestore(sp);

    this.gputop.cc_oa_accumulator_ptr_to_obj_map[accumulator.cc_accumulator_ptr_] = accumulator;

    accumulator.id = this.oa_accumulators.length;
    this.oa_accumulators.push(accumulator);

    return accumulator;
}

Metric.prototype.set_oa_accumulator_period = function(accumulator, period_ns) {

    if (accumulator.cc_accumulator_ptr_ === 0) {
        this.gputop.log("NULL CC accumulator", this.gputop.ERROR);
        return;
    }

    this.gputop.log("Setting aggregation period to " + period_ns + "ns", this.gputop.LOG);
    cc._gputop_cc_oa_accumulator_set_period(accumulator.cc_accumulator_ptr_, period_ns);
}

Metric.prototype.destroy_oa_accumulator = function(accumulator) {
    /* Allow for this to be called multiple times. It may be called
     * automatically when the stream (which the accumulator depends on)
     * is closed.
     */
    if (accumulator.cc_accumulator_ptr_ !== 0) {
        cc._gputop_cc_oa_accumulator_destroy(accumulator.cc_accumulator_ptr_);
        delete this.gputop.cc_oa_accumulator_ptr_to_obj_map[accumulator.cc_accumulator_ptr_];
        accumulator.cc_accumulator_ptr_ = 0;
    }

    /* Disassociate from Metric */
    delete this.oa_accumulators[accumulator.id];
    accumulator.id = -1;
}

/* We have to consider that Client C state isn't automatically garbage
 * collected so this should be called explicitly, when the Metric
 * stream is being gracefully closed, or there was a known error with
 * the stream.
 *
 * This will also destroy any associated CC accumulator state that depends
 * on the stream.
 */
Metric.prototype.dispose = function () {

    /* In the case of an OA metrics stream, also clean up corresponding
     * accumulators that depend on the stream...
     */
    for (var i = 0; i < this.oa_accumulators.length; i++) {
        var accumulator = this.oa_accumulators[i];

        /* array may be sparse if individual accumulator was destroyed
         * via metric.destroy_oa_accumulator()
         */
        if (accumulator !== undefined)
            this.destroy_oa_accumulator(accumulator);
    }
    this.oa_accumulators = [];

    if (this.stream) {
        if (this.stream.cc_stream_ptr_ !== 0) {
            cc._gputop_cc_stream_destroy(this.stream.cc_stream_ptr_);
            delete this.gputop.cc_stream_ptr_to_obj_map[this.stream.cc_stream_ptr_];
            this.stream.cc_stream_ptr_ = 0;
        }

        if (this.stream.server_handle !== 0) {
            delete this.gputop.server_handle_to_obj[this.stream.server_handle];
            this.stream.server_handle = 0;
        }
        this.stream = undefined;
    }

    this.open_config = undefined;
}

Metric.prototype.close = function(onclose) {
    if (this.stream === undefined) {
        this.gputop.log("Redundant OA metric close request", this.gputop.ERROR);
        return;
    }

    if (this.closing_ === true ) {
        var ev = { type: "error", msg: "Pile Up: ignoring repeated request to close oa metric set (already waiting for close ACK)" };
        this.stream.dispatchEvent(ev);
        return;
    }

    function _finish_close() {
        var ev = { type: "close" };
        this.stream.dispatchEvent(ev);

        this.dispose();

        this.closing_ = false;

        if (onclose !== undefined)
            onclose();
    }

    this.closing_ = true;

    /* XXX: May have a stream but no server handle while metric is paused */
    if (this.stream.server_handle === 0) {
        _finish_close.call(this);
    } else {
        this.gputop.rpc_request('close_stream', this.stream.server_handle, (msg) => {
            _finish_close.call(this);
        });
    }
}

Gputop.prototype.oa_exponent_to_nsec = function(exponent) {
    return (1 << exponent) * 1000000000 / this.system_properties.timestamp_frequency;
}

/* NB: the OA unit only has a limited exponential scale for what sampling
 * periods it supports, so a requested accumulation period might need to be
 * based on multiple higher-frequency samples.
 *
 * NB: we may impose a maximum limit on the hardware sampling period to ensure
 * we can account for 32bit counter overflow.
 *
 * Searches for an exponent that factors to within @margin nanoseconds of the
 * requested period.
 *
 * Be careful to consider that too strict of a margin along with a short
 * sampling period could result in an excessively high sampling frequency.
 *
 * The margin should be >= 1% of the period since that's the limit of the
 * searching done here.
 */
Gputop.prototype.calculate_sample_state_for_accumulation_period = function(requested_period, max_hw_period, margin) {

    for (var f = 1; f < 101; f++) {
        for (var e = 0; e < 64; e++) {
            var hw_period = this.oa_exponent_to_nsec(e);
            var factored_period = hw_period * f;

            if (hw_period > max_hw_period)
                break;

            if (factored_period < (requested_period + margin) &&
                factored_period > (requested_period - margin))
            {
                /* Once we manage to get within the requested margin the
                 * exponent is fixed but the multiplication factor may
                 * still be refined...
                 */
                var best_factor = f;
                var best_difference = Math.abs(factored_period - requested_period);

                for (f++; f < 101; f++) {
                    var factored_period = hw_period * f;
                    var difference = Math.abs(factored_period - requested_period);

                    if (difference < best_difference) {
                        best_factor = f;
                        best_difference = difference;
                    } else
                        break;
                }

                return { oa_exponent: e, period: hw_period, factor: best_factor };
            }
        }
    }

    for (var j = 0; j < 64; j++) {
    }

    console.assert(0, "Requested margin for finding OA exponent was too low");
    return undefined;
}

var EventTarget = function() {
    this.listeners = {};
};

EventTarget.prototype.listeners = null;
EventTarget.prototype.removeEventListener = function(type, callback) {
    if(!(type in this.listeners)) {
        return;
    }
    var stack = this.listeners[type];
    for(var i = 0, l = stack.length; i < l; i++){
        if(stack[i] === callback){
            stack.splice(i, 1);
            return this.removeEventListener(type, callback);
        }
    }
};

EventTarget.prototype.addEventListener = function(type, callback) {
    if(!(type in this.listeners)) {
        this.listeners[type] = [];
    }
    this.removeEventListener(type, callback);
    this.listeners[type].push(callback);
};

EventTarget.prototype.dispatchEvent = function(event){
    if(!(event.type in this.listeners)) {
        return;
    }
    var stack = this.listeners[event.type];
    event.target = this;
    for(var i = 0, l = stack.length; i < l; i++) {
        stack[i].call(this, event);
    }
};

EventTarget.prototype.on = function(type, callback) {
    this.addEventListener(type, callback);
}

EventTarget.prototype.once = function(type, callback) {
  function _once_wraper() {
    this.removeListener(type, _once_wrapper);
    return callback.apply(this, arguments);
  }
  return this.on(type, _once_wrapper);
};

var Stream = function(server_handle) {
    EventTarget.call(this);

    this.server_handle = server_handle
    this.cc_stream_ptr_ = 0;
}

Stream.prototype = Object.create(EventTarget.prototype);

Gputop.prototype.request_open_cpu_stats = function(config, callback) {
    var stream = new Stream(this.next_server_handle++);

    var cpu_stats = new this.gputop_proto_.CpuStatsInfo();
    if ('sample_period_ms' in config)
        cpu_stats.set('sample_period_ms', config.sample_period_ms);
    else
        cpu_stats.set('sample_period_ms', 10);

    var open = new this.gputop_proto_.OpenStream();
    open.set('id', stream.server_handle);
    open.set('cpu_stats', cpu_stats);
    open.set('overwrite', false);   /* don't overwrite old samples */
    open.set('live_updates', true); /* send live updates */

    if (callback !== undefined) {
        stream.on('open', () => {
            callback.call(this)
        });
    }

    this.rpc_request('open_stream', open, () => {
        this.server_handle_to_obj[open.id] = stream;

        var ev = { type: "open" };
        stream.dispatchEvent(ev);
    });

    return stream;
}

Gputop.prototype.get_tracepoint_info = function(name, callback) {
    function parse_field(str) {
        var field = {};

        var subfields = str.split(';');

        for (var i = 0; i < subfields.length; i++) {
            var subfield = subfields[i].trim();

            if (subfield.match('^field:')) {
                var tokens = subfield.split(':')[1].split(' ');
                field.name = tokens[tokens.length - 1];
                field.type = tokens.slice(0, -1).join(' ');
            } else if (subfield.match('^offset:')) {
                field.offset = Number(subfield.split(':')[1]);
            } else if (subfield.match('^size:')) {
                field.size = Number(subfield.split(':')[1]);
            } else if (subfield.match('^signed:')) {
                field.signed = Boolean(Number(subfield.split(':')[1]));
            }
        }

        return field;
    }

    function parse_tracepoint_format(str) {
        var tracepoint = {};

        var lines = str.split('\n');
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];

            if (line.match('^name:'))
                tracepoint.name = line.slice(6);
            else if (line.match('^ID:'))
                tracepoint.id = Number(line.slice(4));
            else if (line.match('^print fmt:'))
                tracepoint.print_format = line.slice(11);
            else if (line.match('^format:')) {
                tracepoint.common_fields = [];
                tracepoint.event_fields = [];

                for (i++; lines[i].match('^\tfield:'); i++) {
                    line = lines[i];
                    var field = parse_field(line.slice(1));
                    tracepoint.common_fields.push(field);
                }
                if (lines[i + 1].match('\tfield:')) {
                    for (i++; lines[i].match('^\tfield:'); i++) {
                        line = lines[i];
                        var field = parse_field(line.slice(1));
                        tracepoint.event_fields.push(field);
                    }
                }
                break;
            }
        }

        return tracepoint;
    }

    this.rpc_request('get_tracepoint_info', name, (msg) => {
        var tracepoint = {};

        tracepoint.name = name;

        tracepoint.id = msg.tracepoint_info.id;

        var format = msg.tracepoint_info.sample_format;
        console.log("Full format description = " + format);

        var tracepoint = parse_tracepoint_format(format);
        console.log("Structured format = " + JSON.stringify(tracepoint));

        callback(tracepoint);
    });
}

Gputop.prototype.open_tracepoint = function(tracepoint_info, config, onopen, onclose, onerror) {

    var stream = new Stream(this.next_server_handle++);

    stream.info = tracepoint_info;

    if (onopen !== undefined)
        stream.on('open', onopen);

    if (onclose !== undefined)
        stream.on('close', onclose);

    if (onerror !== undefined)
        stream.on('error', onerror);

    if (this.closing_) {
        var ev = { type: "error", msg: "Can't open metric while also waiting for it to close" };
        stream.dispatchEvent(ev);
        return null;
    }

    if (this.stream != undefined) {
        var ev = { type: "error", msg: "Can't re-open OA metric without explicitly closing first" };
        stream.dispatchEvent(ev);
        return null;
    }

    if (config.paused === undefined)
        config.paused = false;
    if (config.pid === undefined)
        config.pid = -1;
    if (config.cpu === undefined) {
        if (config.pid === -1)
            config.cpu = 0;
        else
            config.cpu = -1;
    }

    this.tracepoints_.push(stream);

    function _finalize_open() {
        this.log("Opened tracepoint " + tracepoint_info.name);

        var sp = cc.Runtime.stackSave();

        stream.cc_stream_ptr_ = cc._gputop_cc_tracepoint_stream_new();

        tracepoint_info.common_fields.forEach((field) => {
            var name_c_string = String_pointerify_on_stack(field.name);
            var type_c_string = String_pointerify_on_stack(field.type);

            cc._gputop_cc_tracepoint_add_field(stream.cc_stream_ptr_,
                                               name_c_string,
                                               type_c_string,
                                               field.offset,
                                               field.size,
                                               field.signed);
        });

        tracepoint_info.event_fields.forEach((field) => {
            var name_c_string = String_pointerify_on_stack(field.name);
            var type_c_string = String_pointerify_on_stack(field.type);

            cc._gputop_cc_tracepoint_add_field(stream.cc_stream_ptr_,
                                               name_c_string,
                                               type_c_string,
                                               field.offset,
                                               field.size,
                                               field.signed);
        });

        cc.Runtime.stackRestore(sp);

        this.cc_stream_ptr_to_obj_map[stream.cc_stream_ptr_] = stream;

        if (callback != undefined)
            callback(stream);
    }

    if (config.paused) {
        stream.server_handle = 0;
        _finalize_open.call(this);
    } else {
        var tracepoint = new this.gputop_proto_.TracepointConfig();

        tracepoint.set('pid', config.pid);
        tracepoint.set('cpu', config.cpu);

        tracepoint.set('id', tracepoint_info.id);

        var open = new this.gputop_proto_.OpenStream();
        open.set('id', stream.server_handle);
        open.set('tracepoint', tracepoint);
        open.set('overwrite', false);   /* don't overwrite old samples */
        open.set('live_updates', true); /* send live updates */

        console.log("REQUEST = " + JSON.stringify(open));
        this.rpc_request('open_stream', open, () => {
            this.server_handle_to_obj[open.id] = stream;

            _finalize_open.call(this);

            var ev = { type: "open" };
            stream.dispatchEvent(ev);
        });
    }

    return stream;
}

function String_pointerify_on_stack(js_string) {
    return cc.allocate(cc.intArrayFromString(js_string), 'i8', cc.ALLOC_STACK);
}

Gputop.prototype.generate_uuid = function()
{
    /* Concise uuid generator from:
     * http://stackoverflow.com/questions/105034/create-guid-uuid-in-javascript
     */
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
        var r = Math.random()*16|0, v = c == 'x' ? r : (r&0x3|0x8);
        return v.toString(16);
    });
}

/* TODO: maybe make @value unnecessary for methods that take no data. */
Gputop.prototype.rpc_request = function(method, value, closure) {

    if (this.is_demo()) {
        if (closure != undefined)
            window.setTimeout(closure);
        return;
    }

    var msg = new this.gputop_proto_.Request();

    msg.uuid = this.generate_uuid();

    msg.set(method, value);

    msg.encode();
    this.socket_.send(msg.toArrayBuffer());

    this.log("RPC: " + msg.req + " request: ID = " + msg.uuid);

    if (closure != undefined) {
        this.rpc_closures_[msg.uuid] = closure;

        console.assert(Object.keys(this.rpc_closures_).length < 1000,
                       "Leaking RPC closures");
    }
}

Gputop.prototype.request_features = function() {
    if (!this.is_demo()) {
        if (this.socket_.readyState == is_nodejs ? 1 : WebSocket.OPEN) {
            this.rpc_request('get_features', true);
        } else {
            this.log("Can't request features while not connected", this.ERROR);
        }
    } else {
        var demo_devinfo = new this.gputop_proto_.DevInfo();

        demo_devinfo.set('devname', this.demo_architecture);

        demo_devinfo.set('timestamp_frequency', 12500000);

        var n_eus = 0;
        var threads_per_eu = 7;

        switch (this.demo_architecture) {
        case 'hsw':
            demo_devinfo.set('devid', 0x0422);
            demo_devinfo.set('gen', 7);
            demo_devinfo.set('n_eu_slices', 2);
            demo_devinfo.set('n_eu_sub_slices', 2);
            n_eus = 40;
            demo_devinfo.set('slice_mask', 0x3);
            demo_devinfo.set('subslice_mask', 0xf);
            break;
        case 'bdw':
            demo_devinfo.set('devid', 0x1616);
            demo_devinfo.set('gen', 8);
            demo_devinfo.set('n_eu_slices', 2);
            demo_devinfo.set('n_eu_sub_slices', 3);
            n_eus = 48;
            demo_devinfo.set('slice_mask', 0x3);
            demo_devinfo.set('subslice_mask', 0x3f);
            break;
        case 'chv':
            demo_devinfo.set('devid', 0x22b0);
            demo_devinfo.set('gen', 8);
            demo_devinfo.set('n_eu_slices', 1);
            demo_devinfo.set('n_eu_sub_slices', 2);
            n_eus = 16;
            demo_devinfo.set('slice_mask', 0x1);
            demo_devinfo.set('subslice_mask', 0x3);
            break;
        case 'sklgt2':
        case 'sklgt3':
        case 'sklgt4':
            demo_devinfo.set('devid', 0x1926);
            demo_devinfo.set('gen', 9);
            demo_devinfo.set('n_eu_slices', 3);
            demo_devinfo.set('n_eu_sub_slices', 3);
            n_eus = 72;
            demo_devinfo.set('slice_mask', 0x7);
            demo_devinfo.set('subslice_mask', 0x1ff);
            demo_devinfo.set('timestamp_frequency', 12000000);
            break;
        default:
            console.error("Unknown architecture to demo");
        }

        demo_devinfo.set('n_eus', n_eus);
        demo_devinfo.set('eu_threads_count', n_eus * threads_per_eu);
        demo_devinfo.set('gt_min_freq', 500000000);
        demo_devinfo.set('gt_max_freq', 1100000000);

        var demo_features = new this.gputop_proto_.Features();

        demo_features.set('devinfo', demo_devinfo);
        demo_features.set('has_gl_performance_query', false);
        demo_features.set('has_i915_oa', true);
        demo_features.set('n_cpus', 4);
        demo_features.set('cpu_model', 'Intel(R) Core(TM) i7-4500U CPU @ 1.80GHz');
        demo_features.set('kernel_release', '4.5.0-rc4');
        demo_features.set('fake_mode', false);
        demo_features.set('supported_oa_uuids', []);

        this.process_features(demo_features);
    }
}

Gputop.prototype.process_features = function(features){

    this.features = features;

    this.devinfo = features.devinfo;

    this.log("Features: ");
    this.log("CPU: " + features.get_cpu_model());
    this.log("Architecture: " + features.devinfo.devname);
    this.set_architecture(features.devinfo.devname);

    this.system_properties = {};
    cc._gputop_cc_reset_system_properties();

    var DevInfo = this.builder_.lookup("gputop.DevInfo");
    var fields = DevInfo.getChildren(ProtoBuf.Reflect.Message.Field);
    fields.forEach((field) => {
        var val = 0;
        var name_c_string = String_pointerify_on_stack(field.name);

        switch (field.type.name) {
        case "uint64":
            /* NB uint64 types are handled via long.js and we're being lazy
             * for now and casting to a Number when forwarding to the cc
             * api. Later we could add a set_system_property_u64() api if
             * necessary */
            val = features.devinfo[field.name].toInt();
            cc._gputop_cc_set_system_property(name_c_string, val);
            break;
        case "uint32":
            val = features.devinfo[field.name];
            cc._gputop_cc_set_system_property(name_c_string, val);
            break;
        case "string":
            val = features.devinfo[field.name];
            cc._gputop_cc_set_system_property_string(name_c_string, val);
            break;
        default:
            console.error("Unexpected DevInfo " + field.name + " field type");
            val = features.devinfo[field.name];
            break;
        }

        this.system_properties[field.name] = val;
    });


    cc._gputop_cc_update_system_metrics();

    this.xml_file_name_ = "gputop-" + this.config_.architecture + ".xml";

    get_file(this.xml_file_name_, (xml) => {
        this.parse_xml_metrics(xml);

        if (this.is_demo())
            this.metrics_.forEach(function (metric) { metric.supported_ = true; });
        else {
            this.metrics_.forEach(function (metric) { metric.supported_ = false; });

            if (features.supported_oa_uuids.length == 0) {
                this.user_msg("No OA metrics are supported on this Kernel " +
                              features.get_kernel_release(), this.ERROR);
            } else {
                this.log("Metrics:");
                features.supported_oa_uuids.forEach((uuid, i, a) => {
                    var metric = this.lookup_metric_for_uuid(uuid);
                    metric.supported_ = true;
                    this.log("  " + metric.name + " (uuid = " + uuid + ")");
                });
            }
        }

        this.update_features(features);
    }, function (error) { console.log(error); });
}

Gputop.prototype.load_emscripten = function(callback) {
    if (this.native_js_loaded_) {
        callback();
        return;
    }

    if (!is_nodejs) {
        get_file('gputop-web.js',
                (text) => {
                    var src = text + '\n' + '//# sourceURL=gputop-web.js\n';

                    $('<script type="text/javascript">').text(src).appendTo(document.body);

                    cc = Module;

                    /* Tell gputop client-c about this object so
                     * that the cc code can call methods on it...
                     */
                    cc.gputop_singleton = this;

                    this.native_js_loaded_ = true;
                    this.log("GPUTop Emscripten code loaded\n");
                    callback();
                },
                function () {
                    this.log( "Failed loading emscripten", this.ERROR);
                });
    } else {
        /* In the case of node.js we use require('./gputop-web.js') to
         * load the Emscripten code so this is mostly a NOP...
         */
        this.native_js_loaded_ = true;

        /* Tell gputop client-c about this object so that the cc
         * code can call methods on it...
         */
        cc._gputop_cc_set_singleton(this);
        this.log("Initialized gputop_singleton");
        callback();
    }
}

Gputop.prototype.dispose = function() {
    if (this.socket_ !== undefined)
        this.socket_.close();
    this.socket_ = undefined;

    this.is_connected_ = false;

    this.metrics_.forEach(function (metric) {
        /* NB: The Client C state is not automatically garbage collected so we
         * need to be careful about explicitly freeing it...
         */
        if (!metric.closing_)
            metric.dispose();
    });

    this.metrics_ = [];
    this.map_metrics_ = {}; // Map of metrics by UUID

    this.cc_stream_ptr_to_obj_map = {};
    this.server_handle_to_obj = {};
}

function gputop_socket_on_message(evt) {
    var dv = new DataView(evt.data, 0);
    var data = new Uint8Array(evt.data, 8);
    var msg_type = dv.getUint8(0);

    switch(msg_type) {
    case 1: /* WS_MESSAGE_PERF */
        var server_handle = dv.getUint16(4, true /* little endian */);

        if (server_handle in this.server_handle_to_obj) {
            var sp = cc.Runtime.stackSave();

            var stack_data = cc.allocate(data, 'i8', cc.ALLOC_STACK);

            var stream = this.server_handle_to_obj[server_handle];

            // TODO: save messages in a buffer to replay when stream is paused
            /*
            this.tracepoint_history.push(data);
            this.tracepoint_history_size += data.length;
            if (this.tracepoint_history_size > 1048576) // 1 MB of data
                this.tracepoint_history.shift();
            */

            cc._gputop_cc_handle_tracepoint_message(stream.cc_stream_ptr_,
                                                    stack_data,
                                                    data.length);

            cc.Runtime.stackRestore(sp);
        } else {
            console.log("Ignoring i915 perf data for unknown Metric object")
        }
        break;
    case 2: /* WS_MESSAGE_PROTOBUF */
        var msg = this.gputop_proto_.Message.decode(data);

        switch (msg.cmd) {
        case 'features':
            this.process_features(msg.features);
            break;
        case 'error':
            this.user_msg(msg.error, this.ERROR);
            this.log(msg.reply_uuid + " recv: Error " + msg.error, this.ERROR);
            break;
        case 'log':
            var entries = msg.log.entries;
            entries.forEach((entry) => {
                this.application_log(entry.log_level, entry.log_message);
            });
            break;
        case 'process_info':
            var pid = msg.process_info.pid;
            var process = this.get_process_by_pid(pid);

            process.update(msg.process_info);
            this.log(msg.reply_uuid + " recv: Console process info "+pid);
            break;
        case 'cpu_stats':
            var server_handle = msg.cpu_stats.id;

            if (server_handle in this.server_handle_to_obj) {
                var stream = this.server_handle_to_obj[server_handle];

                var ev = { type: "update", stats: msg.cpu_stats };
                stream.dispatchEvent(ev);
            }
            break;
        }

        if (msg.reply_uuid in this.rpc_closures_) {
            var closure = this.rpc_closures_[msg.reply_uuid];
            closure(msg);
            delete this.rpc_closures_[msg.reply_uuid];
        }

        break;
    case 3: /* WS_MESSAGE_I915_PERF */
        var server_handle = dv.getUint16(4, true /* little endian */);

        if (server_handle in this.server_handle_to_obj) {
            var metric = this.server_handle_to_obj[server_handle];

            // save messages in a buffer to replay when stream is paused
            this.i915_perf_history.push(data);
            this.i915_perf_history_size += data.length;
            if (this.i915_perf_history_size > 1048576) // 1 MB of data
                this.i915_perf_history.shift();

            var sp = cc.Runtime.stackSave();

            var stack_data = cc.allocate(data, 'i8', cc.ALLOC_STACK);

            var n_accumulators = metric.oa_accumulators.length;

            /* XXX: unfortunately we can't handle passing an array of accumulator
             * pointers consistently between Node.js and Emscripten bindings.
             * With Emscripten we're passing a packed array of uint32 integers
             * as pointers, while for Node.js we want to pass a standard Javascript
             * array and let the binding implementation map this into an array
             * before calling the real C function.
             */
            if (using_emscripten) {
                /* Note: 2nd type arg ignored when 1st arg is a size/number in bytes*/
                var vec = cc.allocate(4 * n_accumulators, '*', cc.ALLOC_STACK);
                for (var i = 0; i < n_accumulators; i++) {
                    var accumulator = metric.oa_accumulators[i];
                    cc.setValue(vec + i * 4, accumulator.cc_accumulator_ptr_, '*');
                }

                if (metric.stream.cc_stream_ptr_ === 0) {
                    gputop.log("NULL CC Stream while handling i915 perf message", this.ERROR);
                } else
                    cc._gputop_cc_handle_i915_perf_message(metric.stream.cc_stream_ptr_,
                                                           stack_data,
                                                           data.length,
                                                           vec,
                                                           n_accumulators);
            } else {
                var vec = [];
                for (var i = 0; i < n_accumulators; i++) {
                    var accumulator = metric.oa_accumulators[i];
                    vec.push(accumulator.cc_accumulator_ptr_);
                }

                cc._gputop_cc_handle_i915_perf_message(metric.stream.cc_stream_ptr_,
                                                       stack_data,
                                                       data.length,
                                                       vec,
                                                       n_accumulators);
            }

            cc.Runtime.stackRestore(sp);
        } else {
            console.log("Ignoring i915 perf data for unknown Metric object")
        }
        break;
    }
}

Gputop.prototype.get_process_info = function(pid, callback) {
    this.rpc_request('get_process_info', pid, callback);
}

Gputop.prototype.connect_web_socket = function(websocket_url, onopen, onclose, onerror) {
    try {
        var socket = new WebSocket(websocket_url, "binary");
    } catch (e) {
        gputop.log("new WebSocket error", this.ERROR);
        if (onerror !== undefined)
            onerror();
        return null;
    }
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
        this.user_msg("Connected to GPUTOP");
        this.flush_test_log();
        if (onopen !== undefined)
            onopen();
    }

    socket.onclose = () => {
        if (onclose !== undefined)
            onclose();

        this.dispose();

        this.user_msg("Disconnected");
    }

    if (onerror !== undefined)
        socket.onerror = onerror;

    socket.onmessage = gputop_socket_on_message.bind(this);

    return socket;
}

Gputop.prototype.load_gputop_proto = function(onload) {
    get_file('gputop.proto', (proto) => {
        this.builder_ = ProtoBuf.newBuilder();

        ProtoBuf.protoFromString(proto, this.builder_, "gputop.proto");

        this.gputop_proto_ = this.builder_.build("gputop");

        onload();
    },
    function (error) { console.log(error); });
}

var GputopConnection = function(gputopObj) {
    EventTarget.call(this);

    this.gputop = gputopObj;
}

GputopConnection.prototype = Object.create(EventTarget.prototype);

Gputop.prototype.connect = function(address, onopen, onclose, onerror) {
    this.dispose();

    this.connection = new GputopConnection(this);

    if (onopen !== undefined)
        this.connection.on('open', onopen);

    if (onclose !== undefined)
        this.connection.on('close', onclose);

    if (onerror !== undefined)
        this.connection.on('error', onerror);

    this.load_emscripten(() => {
        this.load_gputop_proto(() => {
            if (!this.is_demo()) {
                var websocket_url = 'ws://' + sanitize_address(address) + '/gputop/';
                this.log('Connecting to ' + websocket_url);
                this.socket_ = this.connect_web_socket(websocket_url, () => { //onopen
                    this.is_connected_ = true;
                    this.request_features();

                    var ev = { type: "open" };
                    this.connection.dispatchEvent(ev);
                },
                () => { //onclose
                    var ev = { type: "close" };
                    this.connection.dispatchEvent(ev);
                },
                () => { //onerror
                    var ev = { type: "error" };
                    this.connection.dispatchEvent(ev);
                });
            } else {
                this.is_connected_ = true;
                this.request_features();

                var ev = { type: "open" };
                this.connection.dispatchEvent(ev);
            }
        });
    });

    return this.connection;
}

if (is_nodejs) {
    /* For use as a node.js module... */
    exports.Gputop = Gputop;
    exports.Metric = Metric;
    exports.Counter = Counter;
}
