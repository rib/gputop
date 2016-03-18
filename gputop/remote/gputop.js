"use strict";

//# sourceURL=Gputop.js
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
    is_nodejs = true;
}

Object.size = function(obj) {
    var size = 0, key;
    for (key in obj) {
        if (obj.hasOwnProperty(key)) size++;
    }
    return size;
};

//------------------------------ Protobuffer Init ----------------------------

var ProtoBuf;
var proto_builder;
var gputop;
var $;

function on_jquery_ready() {
    http_request.get("http://localhost:7890/gputop.proto", function(response) {
        response.setEncoding('utf8');
        response.on('data', function(data) {
            proto_builder = ProtoBuf.newBuilder();
            ProtoBuf.protoFromString(data, proto_builder, "gputop.proto");
            gputop = new Gputop();
            gputop_ready(gputop);
        });
    });
}

if (!is_nodejs) {
    if (typeof dcodeIO === 'undefined' || !dcodeIO.ProtoBuf) {
        throw(new Error("ProtoBuf.js is not present."));
    }
    // Initialize ProtoBuf.js
    ProtoBuf = dcodeIO.ProtoBuf;
    proto_builder = ProtoBuf.loadProtoFile("gputop.proto");
    $ = window.jQuery;
} else {
    var http_request = require('http');
    ProtoBuf = require("protobufjs");

    http_request.get("http://localhost:7890/index.html", function(response) {
        response.setEncoding('utf8');
        response.on('data', function(data) {
            var jsdom = require('jsdom');
            jsdom.env({html: data, scripts:
                ['http://localhost:7890/jquery.min.js'],
                    loaded: function (err, window) {
                        $ = require('jquery')(window);
                        on_jquery_ready();
                    }
            });
        });
    });
}

//----------------------------- COUNTER --------------------------------------
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
    this.latest_duration = 0; /* how long where raw counters aggregated before
                               * calculating latest_value. (so the value can
                               * be scaled into a per-second value) */

    /* Not all counters have a constant or equation for the maximum
     * and so we simply derive a maximum based on the largest value
     * we've seen */
    this.inferred_max = 0;

    this.samples_ = 0; // Number of samples processed
    this.updates = [];
    this.graph_data = [];
    this.units = '';
    this.graph_markings = [];
    this.record_data = false;
    this.mathml_xml = "";

    this.test_mode = false;
}

Counter.prototype.append_counter_data = function (start_timestamp, end_timestamp,
                                                  d_value, max, reason) {
     if (this.record_data && max != 0) {
        this.updates.push([start_timestamp, end_timestamp, d_value, max, reason]);

        if (this.updates.length > 2000) {
            this.updates.shift();
        }
    }
    this.samples_ ++;


    if (this.latest_value != d_value ||
        this.latest_max != max)
    {
        this.latest_value = d_value;
        this.latest_max = max;
        this.latest_duration = end_timestamp - start_timestamp;

        if (d_value > this.inferred_max)
            this.inferred_max = d_value;
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
}

Metric.prototype.is_per_ctx_mode = function() {
    return this.per_ctx_mode_;
}

Metric.prototype.print = function() {
    gputop_ui.weblog(this.guid_);
}

Metric.prototype.find_counter_by_name = function(symbol_name) {
    return this.counters_map_[symbol_name];
}

Metric.prototype.add_new_counter = function(guid, symbol_name, counter) {
    counter.idx_ = this.n_total_counters_++;
    counter.symbol_name = symbol_name;

    var sp = Runtime.stackSave();

    var counter_idx = _gputop_webc_get_counter_id(String_pointerify_on_stack(guid),
                                                  String_pointerify_on_stack(symbol_name));

    Runtime.stackRestore(sp);

    counter.emc_idx_ = counter_idx;
    if (counter_idx != -1) {
        counter.supported_ = true;
        gputop_ui.weblog('Counter ' + counter_idx + " " + symbol_name);
        this.emc_counters_[counter_idx] = counter;
    } else {
        gputop_ui.weblog('Counter not available ' + symbol_name);
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
    gputop_ui.update_process(this);
}

//------------------------------ GPUTOP --------------------------------------
function Gputop () {
    this.metrics_ = [];
    this.map_metrics_ = {}; // Map of metrics by GUID

    this.is_connected_ = false;
    // Gputop generic configuration
    this.config_ = {
        url_path: is_nodejs ? "localhost" : window.location.hostname,
        uri_port: 7890,
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
    this.builder_ = proto_builder.build("gputop");

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

var params = [ ];
Gputop.prototype.read_counter_xml = function() {
    var metric = params[0];
    var guid = params[1];

    try {
        var $cnt = $(this);
        var symbol_name = $cnt.attr("symbol_name");
        var units = $cnt.attr("units");

        var counter = new Counter();
        counter.mathml_xml = ($cnt.find("mathml_equation"));
        counter.xml_ = $cnt;
        counter.units = units;
        metric.add_new_counter(guid, symbol_name, counter);
    } catch (e) {
        gputop_ui.syslog("Catch parsing counter " + e);
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

function gputop_read_metrics_set() {
    try {
        var $set = $(this);
        var guid = $set.attr("guid");

        gputop_ui.weblog('---------------------------------------');

        var metric = gputop.get_map_metric(guid);
        metric.xml_ = $set;
        metric.name_ = $set.attr("name");
        metric.chipset_ = $set.attr("chipset");

        gputop_ui.weblog(guid + '\n Found metric ' + metric.name_);

        // We populate our array with metrics in the same order as the XML
        // The metric will already be defined when the features query finishes
        metric.metric_set_ = Object.keys(gputop.metrics_).length;
        gputop.metrics_[metric.metric_set_] = metric;

        params = [ metric, guid ];
        $set.find("counter").each(gputop.read_counter_xml, params);
    } catch (e) {
        gputop_ui.syslog("Catch parsing metric " + e);
    }
} // read_metrics_set

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
            gputop_ui.show_alert("No query active for data from "+ stream_ptr +" ","alert-danger");
        return;
    }

    var counter = metric.emc_counters_[counterId];
    if (counter == null) {
        gputop_ui.show_alert("Counter missing in set "+ metric.name_ +" ","alert-danger");
        return;
    }

    counter.append_counter_data(start_timestamp, end_timestamp,
                                d_value, max, reason);

    gputop_ui.queue_redraw();
}

Gputop.prototype.parse_xml_metrics = function(xml) {
    gputop.metrics_xml_ = xml;
    $(xml).find("set").each(gputop_read_metrics_set);
    if (gputop_is_demo()) {
        $('#pane2').load("ajax/metrics.html");
    }
}

Gputop.prototype.set_demo_architecture = function(architecture) {
    if (this.active_oa_metric_) {
        gputop.close_oa_metric_set(this.active_oa_metric_,
            function() {
                gputop_ui.show_alert(" Success closing query", "alert-info");
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
    _gputop_webc_update_stream_period(metric.webc_stream_ptr_, period_ns);
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

        var oa_query = new this.builder_.OAQueryInfo();

        oa_query.guid = config.guid;
        oa_query.period_exponent = oa_exponent;

        var open = new this.builder_.OpenQuery();

        metric.server_handle = gputop.next_server_handle++;

        open.id = metric.server_handle;
        open.oa_query = oa_query;
        open.overwrite = false;   /* don't overwrite old samples */
        open.live_updates = true; /* send live updates */
        open.per_ctx_mode = per_ctx_mode;

        this.server_handle_to_metric_map[open.id] = metric;

        var self = this;
        this.rpc_request('open_query', open, function () {
            gputop_ui.syslog("Opened OA metric set " + metric.name_);

            metric.exponent = oa_exponent;
            metric.per_ctx_mode_ = per_ctx_mode;

            var sp = Runtime.stackSave();

            metric.webc_stream_ptr_ =
                _gputop_webc_stream_new(String_pointerify_on_stack(config.guid),
                                        per_ctx_mode,
                                        metric.period_ns_);

            Runtime.stackRestore(sp);

            self.webc_stream_ptr_to_metric_map[metric.webc_stream_ptr_] = metric;

            if (callback != undefined)
                callback(metric);
        });

        this.active_oa_metric_ = metric;

        if (open.per_ctx_mode)
            gputop_ui.show_alert("Opening metric set " + metric.name_ + " in per context mode", "alert-info");
        else
            gputop_ui.show_alert("Opening metric set " + metric.name_, "alert-info");

        gputop_ui.queue_redraw();
    }

    var metric = this.get_map_metric(config.guid);
    if (metric == undefined) {
        console.error('Error: failed to lookup OA metric set with guid = "' + config.guid + '"');
        return;
    }

    if (metric.supported_ == false) {
        gputop_ui.show_alert(config.guid + " " + metric.name_ + " not supported on this kernel", "alert-danger");
        return;
    }

    if (metric.closing_) {
        gputop_ui.show_alert("Ignoring attempt to open OA metrics while waiting for close ACK", "alert-danger");
        return;
    }

    if (this.active_oa_metric_ != undefined) {
        var self = this;
        this.close_oa_metric_set(this.active_oa_metric_, function () {
            _real_open_oa_metric_set.call(self, config, callback);
        });
    } else
        _real_open_oa_metric_set.call(this, config, callback);
}

Gputop.prototype.close_oa_metric_set = function(metric, callback) {

    if (metric.closing_ == true ) {
        gputop_ui.syslog("Pile Up: ignoring repeated request to close oa metric set (already waiting for close ACK)");
        return;
    }

    gputop_ui.show_alert("Closing query " + metric.name_, "alert-info");

    this.rpc_request('close_query', metric.server_handle, function (msg) {
        _gputop_webc_stream_destroy(metric.webc_stream_ptr_);
        delete gputop.webc_stream_ptr_to_metric_map[metric.webc_stream_ptr_];
        delete gputop.server_handle_to_metric_map[metric.server_handle];

        metric.webc_stream_ptr_ = 0;
        metric.server_handle = 0;

        metric.closing_ = false;

        if (callback != undefined)
            callback();
    });

    metric.closing_ = true;

    this.active_oa_metric_ = undefined;
}

function String_pointerify_on_stack(js_string) {
    return allocate(intArrayFromString(js_string), 'i8', ALLOC_STACK);
}

Gputop.prototype.get_server_url = function() {
    return this.config_.url_path+':'+this.config_.uri_port;
}

Gputop.prototype.get_websocket_url = function() {
    return 'ws://'+this.get_server_url()+'/gputop/';
}

/* Native compiled Javascript from emscripten to process the counters data */
Gputop.prototype.get_gputop_native_js = function() {
    return 'http://'+this.get_server_url()+'/gputop-web.js';
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

    gputop_ui.syslog("RPC: " + msg.req + " request: ID = " + msg.uuid);

    if (closure != undefined) {
        gputop.rpc_closures_[msg.uuid] = closure;

        console.assert(Object.keys(this.rpc_closures_).length < 1000,
                       "Leaking RPC closures");
    }
}

Gputop.prototype.request_features = function() {
    if (!gputop_is_demo()) {
        if (this.socket_.readyState == is_nodejs ? 1 : WebSocket.OPEN) {
            gputop.rpc_request('get_features', true);
        } else {
            gputop_ui.syslog("Not connected");
        }
    } else {
        var demo_devinfo = new this.builder_.DevInfo();

        demo_devinfo.set('timestamp_frequency', 12500000);

        var n_eus = 0;
        var threads_per_eu = 7;

        switch (gputop.config_.architecture) {
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

    gputop.devinfo = di;

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
    if (!is_nodejs) {
        _gputop_webc_update_features(di.devid,
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
    }

    gputop.xml_file_name_ = this.config_.architecture + ".xml";
    console.log(this.config_.architecture);
    $.get(gputop.xml_file_name_, function (xml) {
        gputop.parse_xml_metrics(xml);

        if (gputop_is_demo())
            gputop.metrics_.forEach(function (metric) { metric.supported_ = true; });
        else {
            gputop.metrics_.forEach(function (metric) { metric.supported_ = false; });

            if (features.supported_oa_query_guids.length == 0) {
                gputop_ui.show_alert("No OA metrics are supported on this Kernel " +
                                     features.get_kernel_release(), "alert-danger");
            } else {
                features.supported_oa_query_guids.forEach(function (guid, i, a) {
                    var metric = gputop.get_map_metric(guid);
                    metric.supported_ = true;
                    metric.print();
                });
            }
        }

        gputop_ui.update_features(features);
    });
}

Gputop.prototype.load_emscripten = function() {
    if (gputop.is_connected_)
        return;

    gputop.is_connected_ = true;
    if (gputop.native_js_loaded_ == true) {
        gputop.request_features();
        return;
    }

    if (!is_nodejs) {
        var req = new XMLHttpRequest();
        req.open('GET', gputop.get_gputop_native_js());
        req.onload = function () {
            $('<script type="text/javascript">').text(this.responseText + '\n' +
                              '//# sourceURL=gputop-web.js\n'
                             ).appendTo(document.body);
            gputop.request_features();
            gputop.native_js_loaded_ = true;
            console.log("GPUTop Emscripten code loaded\n");
        };
        req.onerror = function () {
            console.log( "Failed loading emscripten" );
        };
        req.send();
    } else {
        gputop.request_features();
        gputop.native_js_loaded_ = true;
    }
}

Gputop.prototype.dispose = function() {
    gputop.is_connected_ = false;

    gputop.metrics_.forEach(function (metric) {
        if (!metric.closing_ && metric.webc_stream_ptr_)
            _gputop_webc_stream_destroy(metric.webc_stream_ptr_);
    });

    gputop.metrics_ = [];
    gputop.map_metrics_ = {}; // Map of metrics by GUID

    gputop.webc_stream_ptr_to_metric_map = {};
    gputop.server_handle_to_metric_map = {};
    gputop.active_oa_metric_ = undefined;
}

function gputop_socket_on_open() {
    gputop_ui.syslog("Connected");
    gputop_ui.show_alert("Succesfully connected to GPUTOP","alert-success");
    gputop.load_emscripten();
    gputop.flush_test_log();
}

function gputop_socket_on_close() {
    gputop.dispose();

    gputop_ui.syslog("Disconnected");
    gputop_ui.show_alert("Failed connecting to GPUTOP <p\>Retry in 5 seconds","alert-warning");
    setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
        gputop.connect();
    }, 5000);

    gputop.is_connected_ = false;
}

function gputop_socket_on_message(evt) {
    var dv = new DataView(evt.data, 0);
    var data = new Uint8Array(evt.data, 8);
    var msg_type = dv.getUint8(0);

    switch(msg_type) {
    case 1: /* WS_MESSAGE_PERF */
        var id = dv.getUint16(4, true /* little endian */);
        _gputop_webc_handle_perf_message(id, data);
        break;
    case 2: /* WS_MESSAGE_PROTOBUF */
        var msg = gputop.builder_.Message.decode(data);
        if (msg.features != undefined) {
            gputop_ui.syslog("Features: "+msg.features.get_cpu_model());
            gputop.process_features(msg.features);
        }
        if (msg.error != undefined) {
            gputop_ui.show_alert(msg.error,"alert-danger");
            gputop_ui.syslog(msg.reply_uuid + " recv: Error " + msg.error);
            gputop_ui.log(4, msg.error);
        }
        if (msg.log != undefined) {
            var entries = msg.log.entries;
            entries.forEach(function(entry) {
                gputop_ui.log(entry.log_level, entry.log_message);
            });
        }
        if (msg.process_info != undefined) {
            var pid = msg.process_info.pid;
            var process = gputop.get_process_by_pid(pid);

            process.update(msg.process_info);
            gputop_ui.syslog(msg.reply_uuid + " recv: Console process info "+pid);
        }

        if (msg.reply_uuid in gputop.rpc_closures_) {
            var closure = gputop.rpc_closures_[msg.reply_uuid];
            closure(msg);
            delete gputop.rpc_closures_[msg.reply_uuid];
        }

        break;
    case 3: /* WS_MESSAGE_I915_PERF */
        var server_handle = dv.getUint16(4, true /* little endian */);

        if (server_handle in gputop.server_handle_to_metric_map) {
            gputop_ui.queue_redraw();

            var sp = Runtime.stackSave();

            var stack_data = allocate(data, 'i8', ALLOC_STACK);

            var metric = gputop.server_handle_to_metric_map[server_handle];

            _gputop_webc_handle_i915_perf_message(metric.webc_stream_ptr_,
                                                  stack_data,
                                                  data.length);

            Runtime.stackRestore(sp);
        } else {
            console.log("Ignoring i915 perf data for unknown Metric object")
        }
        break;
    }
}

Gputop.prototype.get_process_info = function(pid, callback) {
    this.rpc_request('get_process_info', pid, callback);
}

function gputop_get_socket_web(websocket_url) {
    var socket = new WebSocket(websocket_url, "binary");
    socket.binaryType = "arraybuffer";

    socket.onopen = gputop_socket_on_open;
    socket.onclose = gputop_socket_on_close;
    socket.onmessage = gputop_socket_on_message;

    return socket;
}

function gputop_get_socket_nodejs(websocket_url) {
    var WebSocket = require('ws');
    var socket = new WebSocket(websocket_url, "binary");

    socket.onopen = gputop_socket_on_open;
    socket.onclose = gputop_socket_on_close;
    socket.onmessage = function (evt) {
        //node.js ws api doesn't support .binaryType = "arraybuffer"
        //so manually convert evt.data to an ArrayBuffer...
        evt.data = new Uint8Array(evt.data).buffer;

        gputop_socket_on_node_message(evt);
    }

    return socket;
}

Gputop.prototype.get_socket = function(websocket_url) {
    if (!is_nodejs)
        return gputop_get_socket_web(websocket_url);
    else
        return gputop_get_socket_nodejs(websocket_url);
}

// Connect to the socket for transactions
Gputop.prototype.connect = function() {
    if (!gputop_is_demo()) {
        var websocket_url = this.get_websocket_url();
        gputop_ui.syslog('Connecting to port ' + websocket_url);
        //----------------- Data transactions ----------------------
        this.socket_ = this.get_socket(websocket_url);
    } else
        this.load_emscripten();
}

if (!is_nodejs)
    gputop = new Gputop();
