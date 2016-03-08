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

        if (d_value > this.inferred_max)
            this.inferred_max = d_value;
    }
}

//------------------------------ METRIC --------------------------------------
function Metric () {
    // Id for the interface to know on click
    this.name_ = "not loaded";
    this.chipset_ = "not loaded";

    this.set_id_ = 0;
    this.guid_ = "undefined";
    this.xml_ = "<xml/>";
    this.supported_ = false;
    this.emc_counters_ = []; // Array containing only available counters
    this.counters_ = [];     // Array containing all counters
    this.counters_map_ = {}; // Map of counters by with symbol_name
    this.metric_set_ = 0;

    this.oa_query_id_ = -1; // if there is an active query it will be >0

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

Metric.prototype.add_new_counter = function(emc_guid, symbol_name, counter) {
    counter.idx_ = this.n_total_counters_++;
    counter.symbol_name = symbol_name;

    var emc_symbol_name = emc_str_copy(symbol_name);
    var counter_idx = _get_counter_id(emc_guid, emc_symbol_name);
    emc_str_free(emc_symbol_name);

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

    // Next query ID
    this.query_id_next_ = 1;

    // Current active query sets
    // Indexes by query_id_next_
    this.query_metric_handles_ = [];
    this.query_active_ = undefined;

    // Current metric on display
    this.metric_visible_ = undefined;
}

Gputop.prototype.get_metrics_xml = function() {
    return this.metrics_xml_;
}

// Remember to free this tring
function emc_str_copy(string_to_convert) {
    var buf = Module._malloc(string_to_convert.length+1); // Zero terminated
    stringToAscii(string_to_convert, buf);
    return buf;
}

function emc_str_free(buf) {
    Module._free(buf);
}

var params = [ ];
Gputop.prototype.read_counter_xml = function() {
    var metric = params[0];
    var emc_guid = params[1];

    try {
        var $cnt = $(this);
        var symbol_name = $cnt.attr("symbol_name");
        var units = $cnt.attr("units");

        var counter = new Counter();
        counter.mathml_xml = ($cnt.find("mathml_equation"));
        counter.xml_ = $cnt;
        counter.units = units;
        metric.add_new_counter(emc_guid, symbol_name, counter);
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

        params = [ metric, gputop.get_emc_guid(guid) ];
        $set.find("counter").each(gputop.read_counter_xml, params);
    } catch (e) {
        gputop_ui.syslog("Catch parsing metric " + e);
    }
} // read_metrics_set

Gputop.prototype.query_update_counter = function (counterId,
                                                  id,
                                                  start_timestamp,
                                                  end_timestamp,
                                                  max,
                                                  d_value,
                                                  reason) {
    var metric = this.query_metric_handles_[id];
    if (metric == undefined) {
        //TODO Close this query which is not being captured
        if (counterId == 0)
            gputop_ui.show_alert("No query active for data from "+ id +" ","alert-danger");
        return;
    }

    var counter = metric.emc_counters_[counterId];
    if (counter == null) {
        gputop_ui.show_alert("Counter missing in set "+ metric.name_ +" ","alert-danger");
        return;
    }

    counter.append_counter_data(start_timestamp, end_timestamp,
                                d_value, max, reason);
}

Gputop.prototype.load_xml_metrics = function(xml) {
    gputop.metrics_xml_ = xml;
    $(xml).find("set").each(gputop_read_metrics_set);
}

Gputop.prototype.load_oa_queries = function(architecture) {
    this.config_.architecture = architecture;
    // read counters from xml file and populate the website
    gputop.xml_file_name_ = architecture +".xml";
    console.log(architecture);
    $.get(gputop.xml_file_name_, this.load_xml_metrics);
}

Gputop.prototype.update_period = function(guid, period_ns) {
    var metric = this.map_metrics_[guid];
    metric.period_ns_ = period_ns;
    _gputop_webworker_update_query_period(metric.oa_query_id_, period_ns);
}

Gputop.prototype.open_oa_query_for_trace = function(guid) {
    if (this.no_supported_metrics_ == true) {
        return;
    }

    if (guid == undefined) {
        gputop_ui.show_alert("GUID missing while trying to opening query","alert-danger");
        return;
    }

    var metric = this.get_map_metric(guid);

    // Check if we have to close the old query before opening this one
    var active_metric = this.query_active_;
    if (active_metric != undefined) {
        if (active_metric.on_close_callback_ != undefined) {
            gputop_ui.show_alert("Closing in progress","alert-info");
            active_metric.on_close_callback_ = function() {
                gputop.open_oa_query_for_trace(guid);
            }
            return;
        } else
        this.close_oa_query(this.query_active_.oa_query_id_, function() {
            console.log("Success! Opening new query "+guid);
            gputop.open_oa_query_for_trace(guid);
        });
        return;
    }

    if (metric.supported_ == false) {
        gputop_ui.show_alert(guid+" "+metric.name_ +" not supported on this kernel","alert-danger");
        return;
    }
    gputop_ui.syslog("Launch query GUID " + guid);

    var oa_query = new this.builder_.OAQueryInfo();
    oa_query.guid = guid;
    oa_query.metric_set = metric.metric_set_;

    /* The timestamp for HSW+ increments every 80ns
     *
     * The period_exponent gives a sampling period as follows:
     *   sample_period = 80ns * 2^(period_exponent + 1)
     *
     * The overflow period for Haswell can be calculated as:
     *
     * 2^32 / (n_eus * max_gen_freq * 2)
     * (E.g. 40 EUs @ 1GHz = ~53ms)
     *
     * We currently sample ~ every 10 milliseconds...
     */

    metric.oa_query_ = oa_query;
    metric.oa_query_id_ = this.query_id_next_++;

    var msg = new this.builder_.Request();
    msg.uuid = this.generate_uuid();

    var open = new this.builder_.OpenQuery();

    oa_query.period_exponent = metric.exponent;

    open.id = metric.oa_query_id_; // oa_query ID
    open.overwrite = false;   /* don't overwrite old samples */
    open.live_updates = true; /* send live updates */
                              /* nanoseconds of aggregation
                               * i.e. request updates from the worker
                               * as values that have been aggregated
                               * over this duration */

    open.per_ctx_mode = metric.is_per_ctx_mode();
    open.oa_query = oa_query;

    _gputop_webworker_on_open_oa_query(
          metric.oa_query_id_,
          this.get_emc_guid(guid),
          open.per_ctx_mode,
          metric.period_ns_
          ); //100000000

    msg.open_query = open;
    msg.encode();
    this.socket_.send(msg.toArrayBuffer());

    gputop_ui.syslog(msg.uuid + " sent: Open Query Request ");

    this.query_metric_handles_[metric.oa_query_id_] = metric;
    this.query_active_ = metric;
    metric.waiting_ack_ = true;

    console.log(" Render animation bars ");

    if (open.per_ctx_mode)
        gputop_ui.show_alert("Opening per context query "+ metric.name_, "alert-info");
    else
        gputop_ui.show_alert("Opening query "+ metric.name_, "alert-info");

    gputop_ui.render_bars();
}

Gputop.prototype.close_oa_query = function(id, callback) {
    var metric = this.query_metric_handles_[id];

    if ( metric.waiting_ack_ == true ) {
        gputop_ui.show_alert("Waiting ACK","alert-danger");
        return;
    }

    if (metric == undefined) {
        gputop_ui.show_alert("Cannot close query "+id+", which does not exist ","alert-danger");
        return;
    }

    metric.on_close_callback_ = callback;

    gputop_ui.show_alert("Closing query "+ metric.name_, "alert-info");

    var msg = new this.builder_.Request();
    msg.uuid = this.generate_uuid();

    _gputop_webworker_on_close_oa_query(metric.oa_query_id_);

    msg.close_query = metric.oa_query_id_;
    msg.encode();
    this.socket_.send(msg.toArrayBuffer());

    gputop_ui.syslog(msg.uuid + " sent: Request close query ");
}

// Moves the guid into the emscripten HEAP and returns a ptr to it
Gputop.prototype.get_emc_guid = function(guid) {
    // Allocate a temporal buffer for the IDs in gputop, we will reuse this buffer.
    // This string will be free on dispose.
    if (gputop.buffer_guid_ == undefined)
        gputop.buffer_guid_ = Module._malloc(guid.length+1); // Zero terminated

    stringToAscii(guid,  gputop.buffer_guid_);
    return gputop.buffer_guid_;
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

Gputop.prototype.request_features = function() {
    if (this.socket_.readyState == is_nodejs ? 1 : WebSocket.OPEN) {
        var msg = new this.builder_.Request();

        msg.uuid = this.generate_uuid();
        msg.get_features = true;

        msg.encode();
        this.socket_.send(msg.toArrayBuffer());
        gputop_ui.syslog(msg.uuid + " sent: Request features");
    } else {
        gputop_ui.syslog("Not connected");
    }
}

Gputop.prototype.metric_supported = function(element, index, array){
    var metric = gputop.get_map_metric(element);
    metric.supported_ = true;
    metric.print();
}

Gputop.prototype.process_features = function(features){
    if (features.supported_oa_query_guids.length == 0) {
        gputop.no_supported_metrics_ = true;
        gputop_ui.show_alert("No OA metrics are supported on this Kernel "+features.get_kernel_release(),"alert-danger");
    } else {
        features.supported_oa_query_guids.forEach(this.metric_supported);
    }

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
        _update_features(di.devid,
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

    gputop_ui.update_features(features);
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
        $.getScript( gputop.get_gputop_native_js() )
            .done(function( script, textStatus ) {
                gputop.request_features();
                gputop.native_js_loaded_ = true;
                console.log("gputop weqafrersrgrdfh\n");
            }).fail(function( jqxhr, settings, exception ) {
                console.log( "Failed loading emscripten" );
                setTimeout(function() {
                    gputop.connect();
                }, 5000);
        });
    } else {
        gputop.request_features();
        gputop.native_js_loaded_ = true;
    }
}

Gputop.prototype.dispose = function() {
    gputop.metrics_ = [];
    gputop.map_metrics_ = {}; // Map of metrics by GUID

    gputop.is_connected_ = false;
    gputop.query_id_next_ = 1;

    gputop.query_metric_handles_.forEach(function(metric) {
        // the query stopped being tracked
        metric.oa_query = undefined;
        metric.oa_query_id_ = undefined;
    });

    // Current active query sets
    // Indexes by query_id_next_
    gputop.query_metric_handles_ = [];
    gputop.query_active_ = undefined;
}

function gputop_socket_on_open() {
    gputop_ui.syslog("Connected");
    gputop_ui.show_alert("Succesfully connected to GPUTOP","alert-success");
    gputop.load_emscripten();
}

function gputop_socket_on_close() {
    // Resets the connection
    gputop.dispose();

    gputop_ui.syslog("Disconnected");
    gputop_ui.show_alert("Failed connecting to GPUTOP <p\>Retry in 5 seconds","alert-warning");
    setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
        gputop.connect();
    }, 5000);

    gputop.is_connected_ = false;
}

function gputop_socket_on_message(evt) {
    try {
        var dv;
        var msg_type;
        var data;

        if (!is_nodejs) {
            dv = new DataView(evt.data, 0);
            msg_type = dv.getUint8(0);
            data = new Uint8Array(evt.data, 8);
        } else {
            var buffer = new ArrayBuffer(evt.length);
            data = new Uint8Array(buffer);
            for (i = 0; i < evt.length; i++) {
                data[i] = evt[i];
            }

            dv = new DataView(buffer, 0);
            msg_type = dv.getUint8(0);
        }

        switch(msg_type) {
            case 1: /* WS_MESSAGE_PERF */
                var id = dv.getUint16(4, true /* little endian */);
                //handle_perf_message(id, data);
            break;
            case 2: /* WS_MESSAGE_PROTOBUF */
                var msg = gputop.builder_.Message.decode(data);
                if (msg.features != undefined) {
                    gputop_ui.syslog("Features: "+msg.features.get_cpu_model());
                    gputop.process_features(msg.features);
                }
                if (msg.ack != undefined) {
                    //gputop_ui.log(0, "Ack");
                    gputop_ui.syslog(msg.reply_uuid + " recv: ACK ");
                    if (gputop.query_active_!=undefined) {
                        gputop.query_active_.waiting_ack_ = false;
                    }
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
                if (msg.close_notify != undefined) {
                    var id = msg.close_notify.id;
                    gputop_ui.syslog(msg.reply_uuid + " recv: Close notify "+id);
                    gputop.query_metric_handles_.forEach(function(metric) {
                        if (metric.oa_query_id_ == id) {
                            if (gputop.query_active_ == metric) {
                                gputop.query_active_ = undefined;
                            } else {
                                gputop_ui.syslog("* Query was NOT active "+id);
                            }

                            delete gputop.query_metric_handles_[id];
                            // the query stopped being tracked
                            metric.oa_query = undefined;
                            metric.oa_query_id_ = undefined;

                            var callback = metric.on_close_callback_;
                            if (callback != undefined) {
                                gputop_ui.syslog("* Callback! ");
                                metric.on_close_callback_ = undefined;
                                callback();
                            }

                        }
                    });
                }

            break;
            case 3: /* WS_MESSAGE_I915_PERF */
                var id = dv.getUint16(4, true /* little endian */);
                var dataPtr = Module._malloc(data.length);
                var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, data.length);
                dataHeap.set(data);
                _handle_i915_perf_message(id, dataHeap.byteOffset, data.length);
                Module._free(dataHeap.byteOffset);
            break;
        }
    } catch (err) {
        console.log("Error: "+err);
        gputop_ui.log(0, "Error: "+err+"\n");
    }
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
    var socket = new WebSocket(websocket_url);

    socket.on('open', gputop_socket_on_open);
    socket.on('close', gputop_socket_on_close);
    socket.on('message', gputop_socket_on_message);

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
    var websocket_url = this.get_websocket_url();
    gputop_ui.syslog('Connecting to port ' + websocket_url);
    //----------------- Data transactions ----------------------
    this.socket_ = this.get_socket(websocket_url);
}

if (!is_nodejs)
    gputop = new Gputop();
