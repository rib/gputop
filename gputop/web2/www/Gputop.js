// https://google.github.io/styleguide/javascriptguide.xml

//------------------------------ Protobuffer Init ----------------------------------------

if (typeof dcodeIO === 'undefined' || !dcodeIO.ProtoBuf) {
    throw(new Error("ProtoBuf.js is not present. Please see www/index.html for manual setup instructions."));
}
// Initialize ProtoBuf.js
var ProtoBuf = dcodeIO.ProtoBuf;

var proto_builder = ProtoBuf.loadProtoFile("./proto/gputop.proto");

//------------------------------ GPUTOP ---------------------------------------------------
function Gputop () {

    this.is_connected_ = false;
    // Gputop generic configuration
    this.config_ = {
        url_path: 'localhost',
        uri_port: 7890,
        architecture: 'skl'
    }


    // Initialize protobuffers
    this.builder_ = proto_builder.build("gputop");

    // Array initializer for tabs
    this.tabs_ = [
    'Test tab 1',
    'Test tab 2'
    ];

    // Queries
    this.all_oa_queries_ = [];
    this.current_oa_query = 0;
    this.query_id_ = 0;
    this.query_id_next_ = 1;
    this.query_id_current_oa_ = 0;
    this.query_handles_ = [];
}

Gputop.prototype.load_oa_queries = function(architecture) {
    console.log(" Loading " + architecture);
    this.config_.architecture = architecture;

    $( '#pane2' ).load( "ajax/Panel2.html", function() {
        console.log('Panel2 load)');
        //timeout();
    });
}

Gputop.prototype.open_oa_query_for_trace = function(idx)
{
    var oa_query = this.all_oa_queries_[idx];

    if (typeof oa_query === 'undefined') {
        oa_query = new this.builder_.OAQueryInfo();
        oa_query.metric_set = 1;    /* 3D test */
    }

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

    this.query_id_ = this.query_id_next_++;

    var msg = new this.builder_.Request();
    msg.uuid = this.generate_uuid();

    var open = new this.builder_.OpenQuery();

    oa_query.period_exponent = 16 ;

    open.id = 1; //[TODO @sergioam] oa_query ID
    open.overwrite = false;   /* don't overwrite old samples */
    open.live_updates = true; /* send live updates */
                         /* nanoseconds of aggregation
				          * i.e. request updates from the worker
				          * as values that have been aggregated
				          * over this duration */
    open.oa_query = oa_query;

    _gputop_webworker_on_open_oa_query(
        this.query_id_,
        oa_query.metric_set,
        oa_query.period_exponent,
        open.overwrite, /* don't overwrite old samples */
		      100000000, /* nanoseconds of aggregation
				          * i.e. request updates from the worker
				          * as values that have been aggregated
				          * over this duration */
        open.live_updates /* send live updates */);


    msg.open_query = open;
    msg.encode();
    this.socket_.send(msg.toArrayBuffer());

    log.value += "Sent: Request "+msg.uuid+"\n";

    this.query_handles_.push(this.query_id);

    this.current_oa_query = oa_query;
    this.current_oa_query_id = this.query_id_;
}

Gputop.prototype.get_server_url = function() {
    return this.config_.url_path+':'+this.config_.uri_port;
}

Gputop.prototype.get_websocket_url = function() {
    return 'ws://'+this.get_server_url()+'/gputop/';
}

/* Native compiled Javascript from emscripten to process the counters data */
Gputop.prototype.get_gputop_native_js = function() {
    return 'http://'+this.get_server_url()+'/gputop-web-v2.js';
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
    if (this.socket_.readyState == WebSocket.OPEN) {
        var msg = new this.builder_.Request();

        msg.uuid = this.generate_uuid();
        msg.get_features = true;

        msg.encode();
        this.socket_.send(msg.toArrayBuffer());
        log.value += "Sent: Request "+msg.uuid+"\n";
    } else {
        log.value += "Not connected\n";
    }
}

Gputop.prototype.display_features = function(features) {
    $( "#gputop-connecting" ).hide();
    $( "#gputop-cpu" ).append( features.get_cpu_model() );
    $( "#gputop-kernel-build" ).append( features.get_kernel_build() );
    $( "#gputop-kernel-release" ).append( features.get_kernel_release() );
    $( "#gputop-n-cpus" ).append( features.get_n_cpus() );
    $( "#gputop-kernel-performance-query" ).append( features.get_has_gl_performance_query() );
    $( "#gputop-kernel-performance-i915-oa" ).append( features.get_has_i915_oa() );
}

// types of alerts: alert-success alert-info alert-warning alert-danger
Gputop.prototype.show_alert = function(message,alerttype){
    $('#alert_placeholder').append('<div id="alertdiv" class="alert ' +
        alerttype + '"><a class="close" data-dismiss="alert">×</a><span>'+message+'</span></div>')
        setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
            $("#alertdiv").remove();
        }, 5000);
}

Gputop.prototype.get_socket = function(websocket_url) {
    var socket = new WebSocket( websocket_url);
    socket.binaryType = "arraybuffer"; // We are talking binary

    socket.onopen = function() {
        log.value += "Connected\n";
        gputop.show_alert("Succesfully connected to GPUTOP","alert-info");
        gputop.request_features();
        gputop.is_connected_ = true;

    };

    socket.onclose = function() {
        log.value += "Disconnected\n";
        gputop.is_connected_ = false;
        gputop.show_alert("Failed connecting to GPUTOP <p\>Retry in 5 seconds","alert-danger");
        setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
            log.value += "Connection retry\n";
            gputop.connect();
        }, 5000);
    };

    socket.onmessage = function(evt) {
        try {
            var msg_type = new Uint8Array(evt.data, 0);
            var data = new Uint8Array(evt.data, 8);

            switch(msg_type[0]) {
                case 1: /* WS_MESSAGE_PERF */
                    var id = new Uint16Array(evt.data, 4, 1);
                    // Included in webworker
                    //handle_perf_message(id, data);
                break;
                case 2: /* WS_MESSAGE_PROTOBUF */
                    var msg = gputop.builder_.Message.decode(data);
                    if (msg.features != undefined) {
                        log.value += "Features: "+msg.features.get_cpu_model()+"";
                        gputop.display_features(msg.features);
                        gputop.open_oa_query_for_trace(this.query_id);

                        var di = msg.features.devinfo;
                        _update_features(di.devid, di.n_eus,  di.n_eu_slices,
                            di.n_eu_sub_slices, di.eu_threads_count, di.subslice_mask,
                            di.slice_mask);
                    }
                    if (msg.log != undefined) {
                        var entries = msg.log.entries;
                        entries.forEach(function(entry) {
                        	var color = "red";
                        	switch(entry.log_level) {
                        		case 0: color = "orange"; break;
                        		case 1: color = "green"; break;
                        		case 2: color = "yellow"; break;
                        		case 3: color = "blue"; break;
                        		case 4: color = "black"; break;
                        	}
                        	$('#editor').append("<font color='"+color+"'>"+entry.log_message+"<br/></font>");
                        });
                    }


                break;
                case 3: /* WS_MESSAGE_I915_PERF */
                    var id = new Uint16Array(evt.data, 4, 1);
                    var dataPtr = Module._malloc(data.length);
                    var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, data.length);
                    dataHeap.set(data);
                    _handle_i915_perf_message(id, dataHeap.byteOffset, data.length);
                    Module._free(dataHeap.byteOffset);
                break;
            }
        } catch (err) {
            log.value += "Error: "+err+"\n";
        }
    };

    return socket;
}

// Connect to the socket for transactions
Gputop.prototype.connect = function() {
    var websocket_url = this.get_websocket_url();
    console.log('Connecting to port ' + websocket_url);
    //----------------- Data transactions ----------------------
    this.socket_ = this.get_socket(websocket_url);
}

Gputop.prototype.init = function() {
    console.log(' Gputop Init ');
    $( "#gputop-overview-panel" ).load( "ajax/overview.html", function() {
        console.log('gputop-overview-panel load');
        gputop.connect();
    });
}

Gputop.prototype.dispose = function() {
  this.tabs_ = null;
};

var gputop = new Gputop();

// jquery code
$( document ).ready(function() {
    //log = $( "#log" );
    log = document.getElementById("log");
    $( "#gputop-entries" ).append( '<li><a href="#">Test</a></li>' );
    gputop.init();
    $('#editor').wysiwyg();
});

function allReady() {
    console.log(" All ready for emscripten ");
}
