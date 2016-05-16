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

if (typeof module !== 'undefined' && module.exports) {
    var http = require('http');
    var WebSocket = require('ws');
    var ProtoBuf = require("protobufjs");
    var fs = require('fs');
    var jsdom = require('jsdom');
    var $ = require('jquery')(jsdom.jsdom().defaultView);

    var webc = require("./gputop-web.js");
    webc.gputop_singleton = undefined;

    var install_prefix = require.resolve("./gputop-web.js");
    var path = require('path');
    install_prefix = path.resolve(install_prefix, '..');

    console.log("install prefix = " + install_prefix);

    is_nodejs = true;
} else {
    var webc = undefined;

    ProtoBuf = dcodeIO.ProtoBuf;
    var $ = window.jQuery;
}

function get_hostname() {
    if (is_nodejs)
        return 'localhost:7890' /* TODO: make this configurable somehow */
    else
        return $('#target_address').val() + ':' + $('#target_port').val();
}

function get_file(filename, load_callback, error_callback) {
    if (is_nodejs) {
        fs.readFile(path.join(install_prefix, filename), 'utf8', (err, data) => {
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

function gputop_is_demo() {

    if (!is_nodejs) {
        var demo = getUrlParameter('demo');
        if (demo == "true" || demo == "1"
                || window.location.hostname == "gputop.github.io"
                || window.location.hostname == "www.gputop.com"
                //      || window.location.hostname == "localhost"
           ) {
            return true;
        }
    }

    return false;
}


function Counter () {
    // Real index number counting with not available ones
    this.idx_ = 0;

    // Index to query inside the C code.
    // -1 Means it is not available or supported
    this.emc_idx_ = -1;
    this.symbol_name = '';
    this.supported_ = false;
    this.xml_ = "<xml/>";

    this.latest_value =  0;
    this.latest_max =  0;
    this.latest_duration = 0; /* how long were raw counters aggregated before
                               * calculating latest_value. (so the value can
                               * be scaled into a per-second value) */

    /* Not all counters have a constant or equation for the maximum
     * and so we simply derive a maximum based on the largest value
     * we've seen */
    this.inferred_max = 0;

    this.samples_ = 0; // Number of samples processed
    this.updates = [];
    this.graph_data = [];
    this.graph_options = []; /* each counter has its own graph options so that
                              * we can adjust the Y axis for each of them */
    this.units = '';
    this.graph_markings = [];
    this.record_data = false;
    this.eq_xml = ""; // mathml equation
    this.max_eq_xml = ""; // mathml max equation
    this.duration_dependent = true;
    this.test_mode = false;
    this.units_scale = 1; // default value
}

Counter.prototype.append_counter_data = function (start_timestamp, end_timestamp,
                                                  d_value, max, reason) {
    var duration = end_timestamp - start_timestamp;
    d_value *= this.units_scale;
    max *= this.units_scale;
    if (this.duration_dependent && (duration != 0)) {
        var per_sec_scale = 1000000000 / duration;
        d_value *= per_sec_scale;
        max *= per_sec_scale;
    }
    if (this.record_data) {
        this.updates.push([start_timestamp, end_timestamp, d_value, max, reason]);
        if (this.updates.length > 2000) {
            console.warn("Discarding old counter update (> 2000 updates old)");
            this.updates.shift();
        }
    }
    this.samples_ ++;

    if (this.latest_value != d_value ||
        this.latest_max != max)
    {
        this.latest_value = d_value;
        this.latest_max = max;
        this.latest_duration = duration;

        if (d_value > this.inferred_max)
            this.inferred_max = d_value;
        if (max > this.inferred_max)
            this.inferred_max = max;
    }
}

//------------------------------ METRIC --------------------------------------
function Metric () {
    // Id for the interface to know on click
    this.name_ = "not loaded";
    this.chipset_ = "not loaded";

    this.guid_ = "undefined";
    this.xml_ = "<xml/>";
    this.supported_ = false;
    this.emc_counters_ = []; // Array containing only available counters
    this.counters_ = [];     // Array containing all counters
    this.counters_map_ = {}; // Map of counters by with symbol_name
    this.metric_set_ = 0;

    this.server_handle = 0;
    this.webc_stream_ptr_ = 0;

    this.per_ctx_mode_ = false;

    // Real counter number including not available ones
    this.n_total_counters_ = 0;

    // Aggregation period
    this.period_ns_ = 1000000000;

    // OA HW periodic timer exponent
    this.exponent = 14;
    this.history = []; // buffer used when query is paused.
    this.history_index = 0;
    this.history_size = 0;
}

Metric.prototype.is_per_ctx_mode = function() {
    return this.per_ctx_mode_;
}

Metric.prototype.find_counter_by_name = function(symbol_name) {
    return this.counters_map_[symbol_name];
}

Metric.prototype.add_new_counter = function(guid, symbol_name, counter) {
    counter.idx_ = this.n_total_counters_++;
    counter.symbol_name = symbol_name;
    counter.graph_options = {
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

    var sp = webc.Runtime.stackSave();

    var counter_idx = webc._gputop_webc_get_counter_id(String_pointerify_on_stack(guid),
                                                       String_pointerify_on_stack(symbol_name));

    webc.Runtime.stackRestore(sp);

    counter.emc_idx_ = counter_idx;
    if (counter_idx != -1) {
        counter.supported_ = true;
        console.log('Counter ' + counter_idx + " " + symbol_name);
        this.emc_counters_[counter_idx] = counter;
    } else {
        console.log('Counter not available ' + symbol_name);
    }

    this.counters_map_[symbol_name] = counter;
    this.counters_[counter.idx_] = counter;
}

//----------------------------- PID --------------------------------------
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

//------------------------------ GPUTOP --------------------------------------
function Gputop () {

    this.metrics_ = [];
    this.map_metrics_ = {}; // Map of metrics by GUID

    this.is_connected_ = false;
    // Gputop generic configuration
    this.config_ = {
        architecture: 'ukn'
    }

    this.get_arch_pretty_name = function() {
        switch (this.config_.architecture) {
            case 'hsw': return "Haswell";
            case 'skl': return "Skylake";
            case 'bdw': return "Broadwell";
            case 'chv': return "Cherryview";
        }
        return this.config_.architecture;
    }

    // Initialize protobuffers
    this.builder_ = undefined;

    /* When we send a request to open a stream of metrics we send
     * the server a handle that will be attached to subsequent data
     * for the stream. We use these handles to lookup the metric
     * set that the data corresponds to.
     */
    this.next_server_handle = 1;
    this.server_handle_to_metric_map = {};

    /* When we open a stream of metrics we also call into the
     * Emscripten compiled webc code to allocate a corresponding
     * struct gputop_webc_stream. This map lets us look up a
     * Metric object given a sputop_webc_stream pointer.
     */
    this.webc_stream_ptr_to_metric_map = {};
    this.active_oa_metric_ = undefined;

    // Current metric on display
    this.metric_visible_ = undefined;

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
}

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

Gputop.prototype.parse_counter_xml = function(guid, metric, xml_elem) {
    try {
        var $cnt = $(xml_elem);
        var symbol_name = $cnt.attr("symbol_name");
        var units = $cnt.attr("units");

        var counter = new Counter();
        counter.eq_xml = ($cnt.find("mathml_EQ"));
        counter.max_eq_xml = ($cnt.find("mathml_MAX_EQ"));
        if (counter.max_eq_xml.length == 0)
            counter.max_eq_xml = undefined;
        counter.xml_ = $cnt;

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
        metric.add_new_counter(guid, symbol_name, counter);
    } catch (e) {
        this.syslog("Failed to parse counter: " + e);
    }
}

Gputop.prototype.get_metric_by_id = function(idx){
    return this.metrics_[idx];
}

Gputop.prototype.get_counter_by_absolute_id = function(metric_set, counter_idx){
    //console.log(" Counter from metric [" + this.metrics_[metric_set].name_ + "]");
    var counter = this.metrics_[metric_set].counters_[counter_idx];
    return counter;
}

Gputop.prototype.get_map_metric = function(guid){
    var metric;
    if (guid in this.map_metrics_) {
        metric = this.map_metrics_[guid];
    } else {
        metric = new Metric();
        metric.guid_ = guid;
        this.map_metrics_[guid] = metric;
    }
    return metric;
}

Gputop.prototype.parse_metrics_set_xml = function (xml_elem) {
    try {
        var guid = $(xml_elem).attr("guid");
        var metric = this.get_map_metric(guid);
        metric.xml_ = $(xml_elem);
        metric.name_ = $(xml_elem).attr("name");
        metric.chipset_ = $(xml_elem).attr("chipset");

        this.weblog(guid + '\n Found metric ' + metric.name_);

        // We populate our array with metrics in the same order as the XML
        // The metric will already be defined when the features query finishes
        metric.metric_set_ = Object.keys(this.metrics_).length;
        this.metrics_[metric.metric_set_] = metric;

        $(xml_elem).find("counter").each((i, elem) => {
            this.parse_counter_xml(guid, metric, elem);
        });
    } catch (e) {
        this.syslog("Failed to parse metrics set: " + e);
    }
}

Gputop.prototype.stream_update_counter = function (counterId,
                                                   stream_ptr,
                                                   start_timestamp,
                                                   end_timestamp,
                                                   max,
                                                   d_value,
                                                   reason) {
    var metric = this.webc_stream_ptr_to_metric_map[stream_ptr];
    if (metric == undefined) {
        if (counterId == 0)
            this.show_alert("No query active for data from "+ stream_ptr +" ","alert-danger");
        return;
    }

    var counter = metric.emc_counters_[counterId];
    if (counter == null) {
        this.show_alert("Counter missing in set "+ metric.name_ +" ","alert-danger");
        return;
    }

    counter.append_counter_data(start_timestamp, end_timestamp,
                                d_value, max, reason);

    this.queue_redraw();
}

Gputop.prototype.parse_xml_metrics = function(xml) {
    this.metrics_xml_ = xml;

    $(xml).find("set").each((i, elem) => {
        this.parse_metrics_set_xml(elem);
    });
    if (gputop_is_demo()) {
        $('#gputop-metrics-panel').load("ajax/metrics.html");
    }
}

Gputop.prototype.set_demo_architecture = function(architecture) {
    if (this.active_oa_metric_) {
        this.close_oa_metric_set(this.active_oa_metric_,
            function() {
                this.show_alert(" Success closing query", "alert-info");
            });
    }

    this.dispose();
    this.set_architecture(architecture);
    this.request_features();
}

Gputop.prototype.set_architecture = function(architecture) {
    this.config_.architecture = architecture;
}

Gputop.prototype.update_period = function(guid, period_ns) {
    var metric = this.map_metrics_[guid];
    metric.period_ns_ = period_ns;
    webc._gputop_webc_update_stream_period(metric.webc_stream_ptr_, period_ns);
}

Gputop.prototype.open_oa_metric_set = function(config, callback) {

    function _real_open_oa_metric_set(config, callback) {
        var metric = this.get_map_metric(config.guid);
        var oa_exponent = metric.exponent;
        var per_ctx_mode = metric.per_ctx_mode_;

        if ('oa_exponent' in config)
            oa_exponent = config.oa_exponent;
        if ('per_ctx_mode' in config)
            per_ctx_mode = config.per_ctx_mode;

        function _finalize_open() {
            this.syslog("Opened OA metric set " + metric.name_);

            metric.exponent = oa_exponent;
            metric.per_ctx_mode_ = per_ctx_mode;

            var sp = webc.Runtime.stackSave();

            metric.webc_stream_ptr_ =
                webc._gputop_webc_stream_new(String_pointerify_on_stack(config.guid),
                                             per_ctx_mode,
                                             metric.period_ns_);

            webc.Runtime.stackRestore(sp);

            this.webc_stream_ptr_to_metric_map[metric.webc_stream_ptr_] = metric;

            if (callback != undefined)
                callback(metric);
        }

        this.active_oa_metric_ = metric;

        // if (open.per_ctx_mode)
        //     this.show_alert("Opening metric set " + metric.name_ + " in per context mode", "alert-info");
        // else
        //     this.show_alert("Opening metric set " + metric.name_, "alert-info");


        if ('paused_state' in config) {
            _finalize_open.call(this);
        } else {
            var oa_query = new this.builder_.OAQueryInfo();

            oa_query.guid = config.guid;
            oa_query.period_exponent = oa_exponent;

            var open = new this.builder_.OpenQuery();

            metric.server_handle = this.next_server_handle++;

            open.id = metric.server_handle;
            open.oa_query = oa_query;
            open.overwrite = false;   /* don't overwrite old samples */
            open.live_updates = true; /* send live updates */
            open.per_ctx_mode = per_ctx_mode;

            this.server_handle_to_metric_map[open.id] = metric;

            var self = this;
            this.rpc_request('open_query', open, _finalize_open.bind(this));

            metric.history = [];
            metric.history_size = 0;
        }

        this.queue_redraw();
    }

    var metric = this.get_map_metric(config.guid);
    if (metric == undefined) {
        console.error('Error: failed to lookup OA metric set with guid = "' + config.guid + '"');
        return;
    }

    if (metric.supported_ == false) {
        this.show_alert(config.guid + " " + metric.name_ + " not supported on this kernel", "alert-danger");
        return;
    }

    if (metric.closing_) {
        //this.show_alert("Ignoring attempt to open OA metrics while waiting for close ACK", "alert-danger");
        return;
    }

    if (this.active_oa_metric_ != undefined) {
        this.close_oa_metric_set(this.active_oa_metric_, () => {
            _real_open_oa_metric_set.call(this, config, callback);
        });
    } else
        _real_open_oa_metric_set.call(this, config, callback);
}

Gputop.prototype.close_oa_metric_set = function(metric, callback) {
    if (metric.closing_ == true ) {
        this.syslog("Pile Up: ignoring repeated request to close oa metric set (already waiting for close ACK)");
        return;
    }

    function _finish_close() {
        webc._gputop_webc_stream_destroy(metric.webc_stream_ptr_);
        delete this.webc_stream_ptr_to_metric_map[metric.webc_stream_ptr_];
        delete this.server_handle_to_metric_map[metric.server_handle];

        metric.webc_stream_ptr_ = 0;
        metric.server_handle = 0;

        metric.closing_ = false;

        if (callback != undefined)
            callback();
    }

    //this.show_alert("Closing query " + metric.name_, "alert-info");
    metric.closing_ = true;
    this.active_oa_metric_ = undefined;

    if (global_paused_query) {
        _finish_close.call(this);
    } else {
        this.rpc_request('close_query', metric.server_handle, (msg) => {
            _finish_close.call(this);
        });
    }
}

Gputop.prototype.close_active_metric_set = function(callback) {
    if (this.active_oa_metric_ == undefined) {
        this.show_alert("No Active Metric Set", "alert-info");
        return;
    }

    this.close_oa_metric_set(this.active_oa_metric_, callback);
}


function String_pointerify_on_stack(js_string) {
    return webc.allocate(webc.intArrayFromString(js_string), 'i8', webc.ALLOC_STACK);
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

    if (gputop_is_demo()) {
        if (closure != undefined)
            window.setTimeout(closure);
        return;
    }

    var msg = new this.builder_.Request();

    msg.uuid = this.generate_uuid();

    msg.set(method, value);

    msg.encode();
    this.socket_.send(msg.toArrayBuffer());

    this.syslog("RPC: " + msg.req + " request: ID = " + msg.uuid);

    if (closure != undefined) {
        this.rpc_closures_[msg.uuid] = closure;

        console.assert(Object.keys(this.rpc_closures_).length < 1000,
                       "Leaking RPC closures");
    }
}

Gputop.prototype.request_features = function() {
    if (!gputop_is_demo()) {
        if (this.socket_.readyState == is_nodejs ? 1 : WebSocket.OPEN) {
            this.rpc_request('get_features', true);
        } else {
            this.syslog("Not connected");
        }
    } else {
        var demo_devinfo = new this.builder_.DevInfo();

        demo_devinfo.set('timestamp_frequency', 12500000);

        var n_eus = 0;
        var threads_per_eu = 7;

        switch (this.config_.architecture) {
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
        case 'ukn':
        case 'skl':
            demo_devinfo.set('devid', 0x1926);
            demo_devinfo.set('gen', 9);
            demo_devinfo.set('n_eu_slices', 3);
            demo_devinfo.set('n_eu_sub_slices', 3);
            n_eus = 72;
            demo_devinfo.set('slice_mask', 0x7);
            demo_devinfo.set('subslice_mask', 0x1ff);
            demo_devinfo.set('timestamp_frequency', 12000000);
            break;
        }

        demo_devinfo.set('n_eus', n_eus);
        demo_devinfo.set('eu_threads_count', n_eus * threads_per_eu);
        demo_devinfo.set('gt_min_freq', 500);
        demo_devinfo.set('gt_max_freq', 1100);

        var demo_features = new this.builder_.Features();

        demo_features.set('devinfo', demo_devinfo);
        demo_features.set('has_gl_performance_query', false);
        demo_features.set('has_i915_oa', true);
        demo_features.set('n_cpus', 4);
        demo_features.set('cpu_model', 'Intel(R) Core(TM) i7-4500U CPU @ 1.80GHz');
        demo_features.set('kernel_release', '4.5.0-rc4');
        demo_features.set('fake_mode', false);
        demo_features.set('supported_oa_query_guids', []);

        this.process_features(demo_features);
    }
}

Gputop.prototype.process_features = function(features){
    var di = features.devinfo;

    this.devinfo = di;

    this.set_architecture(di.devname);

    /* We convert the 64 bits protobuffer entry into 32 bits
     * to make it easier to call the emscripten native API.
     * DevInfo values should not overflow the native type,
     * but stay in 64b internally to help native processing in C.
     *
     * XXX: it would be good if there were a more maintainable
     * way of forwarding this info, since it's currently too
     * easy to forget to update this to forward new devinfo
     * state
     */
    webc._gputop_webc_update_features(di.devid,
                                      di.gen,
                                      di.timestamp_frequency.toInt(),
                                      di.n_eus.toInt(),
                                      di.n_eu_slices.toInt(),
                                      di.n_eu_sub_slices.toInt(),
                                      di.eu_threads_count.toInt(),
                                      di.subslice_mask.toInt(),
                                      di.slice_mask.toInt(),
                                      di.gt_min_freq.toInt(),
                                      di.gt_max_freq.toInt());

    this.xml_file_name_ = this.config_.architecture + ".xml";
    console.log(this.config_.architecture);

    get_file(this.xml_file_name_, (xml) => {
        this.parse_xml_metrics(xml);

        if (gputop_is_demo())
            this.metrics_.forEach(function (metric) { metric.supported_ = true; });
        else {
            this.metrics_.forEach(function (metric) { metric.supported_ = false; });

            if (features.supported_oa_query_guids.length == 0) {
                this.show_alert("No OA metrics are supported on this Kernel " +
                                features.get_kernel_release(), "alert-danger");
            } else {
                features.supported_oa_query_guids.forEach((guid, i, a) => {
                    var metric = this.get_map_metric(guid);
                    metric.supported_ = true;
                    this.syslog(guid);
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

                    webc = Module;

                    /* Tell gputop-web-lib.js about this object so
                     * that the webc code can call methods on it...
                     */
                    webc.gputop_singleton = this;

                    this.native_js_loaded_ = true;
                    console.log("GPUTop Emscripten code loaded\n");
                    callback();
                },
                function () {
                    console.log( "Failed loading emscripten" );
                });
    } else {
        /* In the case of node.js we use require('./gputop-web.js') to
         * load the Emscripten code so this is mostly a NOP...
         */
        this.native_js_loaded_ = true;

        /* Tell gputop-web-lib.js about this object so that the webc
         * code can call methods on it...
         */
        webc.gputop_singleton = this;
        callback();
    }
}

Gputop.prototype.dispose = function() {
    this.is_connected_ = false;

    this.metrics_.forEach(function (metric) {
        if (!metric.closing_ && metric.webc_stream_ptr_)
            _gputop_webc_stream_destroy(metric.webc_stream_ptr_);
    });

    this.metrics_ = [];
    this.map_metrics_ = {}; // Map of metrics by GUID

    this.webc_stream_ptr_to_metric_map = {};
    this.server_handle_to_metric_map = {};
    this.active_oa_metric_ = undefined;
}

function gputop_socket_on_close() {
    this.dispose();

    this.syslog("Disconnected");
    this.show_alert("Failed connecting to GPUTOP <p\>Retry in 5 seconds","alert-warning");
    // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
    setTimeout(this.connect.bind(this), 5000);

    this.is_connected_ = false;
}

Gputop.prototype.replay_buffer = function() {
    var metric = this.active_oa_metric_;

    this.clear_graphs();

    for (var i = 0; i < metric.history.length; i++) {
        var data = metric.history[i];

        var sp = webc.Runtime.stackSave();

        var stack_data = webc.allocate(data, 'i8', webc.ALLOC_STACK);

        webc._gputop_webc_handle_i915_perf_message(metric.webc_stream_ptr_,
                                                   stack_data,
                                                   data.length);
        webc.Runtime.stackRestore(sp);
    }
}

function gputop_socket_on_message(evt) {
    var dv = new DataView(evt.data, 0);
    var data = new Uint8Array(evt.data, 8);
    var msg_type = dv.getUint8(0);

    data.length
    switch(msg_type) {
    case 1: /* WS_MESSAGE_PERF */
        var id = dv.getUint16(4, true /* little endian */);
        webc._gputop_webc_handle_perf_message(id, data);
        break;
    case 2: /* WS_MESSAGE_PROTOBUF */
        var msg = this.builder_.Message.decode(data);
        if (msg.features != undefined) {
            this.syslog("Features: "+msg.features.get_cpu_model());
            this.process_features(msg.features);
        }
        if (msg.error != undefined) {
            this.show_alert(msg.error,"alert-danger");
            this.syslog(msg.reply_uuid + " recv: Error " + msg.error);
            this.log(4, msg.error);
        }
        if (msg.log != undefined) {
            var entries = msg.log.entries;
            entries.forEach((entry) => {
                this.log(entry.log_level, entry.log_message);
            });
        }
        if (msg.process_info != undefined) {
            var pid = msg.process_info.pid;
            var process = this.get_process_by_pid(pid);

            process.update(msg.process_info);
            this.syslog(msg.reply_uuid + " recv: Console process info "+pid);
        }

        if (msg.reply_uuid in this.rpc_closures_) {
            var closure = this.rpc_closures_[msg.reply_uuid];
            closure(msg);
            delete this.rpc_closures_[msg.reply_uuid];
        }

        break;
    case 3: /* WS_MESSAGE_I915_PERF */
        var server_handle = dv.getUint16(4, true /* little endian */);

        if (server_handle in this.server_handle_to_metric_map) {
            this.queue_redraw();

            var sp = webc.Runtime.stackSave();

            var stack_data = webc.allocate(data, 'i8', webc.ALLOC_STACK);

            var metric = this.server_handle_to_metric_map[server_handle];

            // save messages in a buffer to replay when query is paused
            metric.history.push(data);
            metric.history_size += data.length;
            if (metric.history_size > 1048576) // 1 MB of data
                metric.history.shift();

            webc._gputop_webc_handle_i915_perf_message(metric.webc_stream_ptr_,
                                                       stack_data,
                                                       data.length);

            webc.Runtime.stackRestore(sp);
        } else {
            console.log("Ignoring i915 perf data for unknown Metric object")
        }
        break;
    }
}

Gputop.prototype.get_process_info = function(pid, callback) {
    this.rpc_request('get_process_info', pid, callback);
}

Gputop.prototype.connect_web_socket = function(websocket_url, onopen) {
    var socket = new WebSocket(websocket_url, "binary");
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
        this.syslog("Connected");
        this.show_alert("Succesfully connected to GPUTOP", "alert-success");
        this.flush_test_log();
        onopen();
    }
    socket.onclose = gputop_socket_on_close.bind(this);
    socket.onmessage = gputop_socket_on_message.bind(this);

    return socket;
}

Gputop.prototype.load_gputop_proto = function(onload) {
    get_file('gputop.proto', (proto) => {
        var proto_builder = ProtoBuf.newBuilder();

        ProtoBuf.protoFromString(proto, proto_builder, "gputop.proto");

        this.builder_ = proto_builder.build("gputop");

        onload();
    },
    function (error) { console.log(error); });
}

Gputop.prototype.connect = function(callback) {
    this.dispose();

    this.load_emscripten(() => {
        if (!gputop_is_demo()) {
                this.load_gputop_proto(() => {
                    var websocket_url = 'ws://' + get_hostname() + '/gputop/';
                    this.syslog('Connecting to port ' + websocket_url);
                    this.socket_ = this.connect_web_socket(websocket_url, () => {
                        this.is_connected_ = true;
                        this.request_features();
                        if (callback !== undefined)
                            callback();
                    });
                });
        } else {
            this.is_connected_ = true;
            if (callback !== undefined)
                callback();
        }
    });
}

if (is_nodejs) {
    /* For use as a node.js module... */
    exports.Gputop = Gputop;
}
