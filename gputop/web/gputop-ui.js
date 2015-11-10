/*
 * GPU Top
 *
 * Copyright (C) 2015 Intel Corporation
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


var features = null;

var active_tab = $("#overview_tab");

var all_oa_queries;
var current_oa_query;

var rpc_id = 1;
var next_query_id = 1;
var query_handles = [];

var current_oa_query_update_handler = function (update) {};

var trace_graphs = [];

var redraw_queued = false;

function queue_redraw(callback) {
    if (!redraw_queued) {
	window.requestAnimationFrame(function (timestamp) { redraw_queued = false; callback(timestamp); } );
	redraw_queued = true;
    }
}

var rpc_closures = {};

function generate_uuid()
{
    /* Concise uuid generator from:
     * http://stackoverflow.com/questions/105034/create-guid-uuid-in-javascript
     */
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
	var r = Math.random()*16|0, v = c == 'x' ? r : (r&0x3|0x8);
	return v.toString(16);
    });
}

function rpc(msg, closure)
{
    msg.uuid = generate_uuid();

    console.log("RPC: " + JSON.stringify(msg));

    if (closure !== undefined) {
	rpc_closures[msg.uuid] = closure;

	console.assert(Object.keys(rpc_closures).length < 1000, "Leaking RPC closures");
    }

    ww.postMessage(msg);
}


var start_timestamp = null;
var start_gpu_timestamp = null;
var trace_ui_updates = [];

function trace_ui_redraw(timestamp)
{
    var n_updates = trace_ui_updates.length;
    var n_counters;
    //var value = [0, 100];
    var x_min = 0;
    var x_max = 1;

    if (n_updates == 0) {
	console.log("Spurious queue redraw with no updates");
	return;
    }

    if (!start_timestamp) {
	start_timestamp = timestamp;
	start_gpu_timestamp = trace_ui_updates[n_updates - 1].gpu_start;
	//start_gpu_timestamp = trace_ui_updates[0].gpu_start;
	console.log("GPU start timestamp = " + start_gpu_timestamp);
    }

    var elapsed = timestamp - start_timestamp;
    //console.log("cpu elapsed = " + elapsed);

    //var latest_gpu_timestamp = trace_ui_updates[n_updates - 1].gpu_end / 1000000;
    //var latest_gpu_timestamp = trace_ui_updates[n_updates - 1].gpu_end;
    //console.assert(start_gpu_timestamp < latest_gpu_timestamp, "GPU clock went backwards!");
    //console.log("gpu start = " +start_gpu_timestamp + "latest = " + latest_gpu_timestamp + "elapsed = " + (latest_gpu_timestamp - start_gpu_timestamp));


    x_max = start_gpu_timestamp + elapsed * 1000000;
    x_min = x_max - 5000000000; /* five second */

    /* Delete old updates */
    for (var j = 0; j < n_updates; j++) {
	var update = trace_ui_updates[j];
	var end = update.gpu_end;

	if (end >= x_min)
	    break;
    }

    if (j > 0)
	trace_ui_updates.splice(0, j);

    if (!trace_ui_updates.length) {
	console.log("No recent counter updates to redraw");
	return;
    }

    /*
    if (trace_ui_updates[n_updates - 1].gpu_start > x_max) {
	var latest_gpu_timestamp = trace_ui_updates[n_updates - 1].gpu_end;

	console.log("More recent counter updates than expected: latest gpu ts = " + latest_gpu_timestamp + " expected max = " + x_max);
	console.log("  > gpu start = " + start_gpu_timestamp + " elapsed ms = " + elapsed + " ns = " + (elapsed * 1000000));
	return;
    }*/

    n_counters = trace_ui_updates[0].counters.length;
    for (var i = 0; i < n_counters; i++) {
	var graph = trace_graphs[i];
	if (!graph)
	    continue;

	var counter = update.counters[i];

	var plot_data = graph.data("plot-data");
	var graph_data = plot_data.values;
	var max_value = plot_data.max;

	/* what samples should we discard?... */
	for (var j = 0; j < graph_data.length && graph_data[j][0] < x_min; j++) {}

	graph_data = graph_data.slice(j);

	for (var j = 0; j < n_updates; j++) {
	    var update = trace_ui_updates[j];
	    var start = update.gpu_start;
	    var end = update.gpu_end;

	    if (start > x_max)
		break;

	    if (start < x_min || start === end || end < start || start === 0 || end === 0) {
		console.warn("Spurious update timestamps: start=" + start + ", end=" + end);
		continue;
	    }
	    //var mid = start + ((end - start) / 2);
	    var mid = start;

	    var val = counter[0];
	    var maximum = counter[1];

	    if (maximum !== 0) {
		var norm = (val / maximum) * 100;
		max_value = 100;
		graph_data.push([mid, norm]);
	    } else {
		if (val > max_value) {
		    plot_data.max = val;
		    max_value = val;
		}
		graph_data.push([mid, val]);
	    }
	}

	plot_data.values = graph_data;

	$.plot($(graph), [ graph_data ], { series: { lines: { show: true, fill: true }, shadowSize: 0 },
					   xaxis: { min: x_min, max: x_max },
					   yaxis: { max: max_value }
	});
    }
/*
    for (var i = 0; i < trace_ui_updates.length; i++) {
	var update = trace_ui_updates[i];
	for (var j = 0; j < update.counters.length; j++) {
	    var graph = trace_graphs[j];
	    if (!graph)
		continue;

	    var value = update.counters[j];

	    var plot = [];
	    for (var x = 0; x < 100; x++) {
		plot[x] = [x, x * x]
	    }

	    $.plot($(graph), [ plot ], { yaxis: { max: value[1] } });
	}
    }
    */

    trace_ui_updates = [];
}

function trace_ui_handle_oa_query_update(update)
{
    var oa_query = current_oa_query;
    //var i = 0;

    //var graph_data = [];

    if (!update || !update.hasOwnProperty("counters") || !Array.isArray(update.counters)) {
	console.warn("Spurious undefined counters update");
	return;
    }

    trace_ui_updates.push(update);
    queue_redraw(trace_ui_redraw);

/*
    for (var i = 0; i < update.counters.length; i++) {
	var value = update.counters[i];
	var graph = trace_graphs[i];
	if (graph) {
	    var counter = graph.data("counter");
	    var data = graph.data("counter-values");
	    var updates = graph.data("counter-updates");
	    updates.push(

	    trace_ui_updates.push(data);

	    while (data.length < 100) {
		data[data.length] = 0;
	    }

	    data = data.slice(1);
	    data[99] = value[0];

	    graph.data("counter-values", data);
	    var plot = [];
	    for (var x = 0; x < data.length; x++) {
		plot[x] = [x, x * x]
	    }

	    //console.log("update counter trace: " + counter.name);
	    queue_redraw(trace_ui_redraw);
	}
    }
*/
}

/*
    console.log(graph_data);

    $.plot($("#overview-graph"), [ graph_data ],
	   {
	       series: {
		   bars: {
		       show: true,
		       barWidth: 0.6,
		       align: "center"
		   }
	       },
	       xaxis: {
		   mode: "categories",
		   tickLength: 0
	       }

	       //yaxis: { max: 100 }
	   });
	   */

function open_oa_query_for_trace(idx)
{
    var oa_query = all_oa_queries[idx];

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
    var period_exponent = 16;

    query_id = next_query_id++;
    rpc({ "method": "open_oa_query",
	  "params": [ query_id,
		      oa_query.metric_set,
		      period_exponent,
		      false, /* don't overwrite old samples */
		      100000000, /* nanoseconds of aggregation
				  * i.e. request updates from the worker
				  * as values that have been aggregated
				  * over this duration */
		      true /* send live updates */
		    ] });

    query_handles.push(query_id);
    current_oa_query = oa_query;
    current_oa_query_id = query_id;
}

function setup_trace_ui_for_oa_query(idx)
{
    var oa_query = all_oa_queries[idx];

    trace_graphs = [];
    $("#trace_counters").empty();

    //for (var query of all_oa_queries) {
	//var h3 = $("<h3/>", { html: query.name });
	//$("#trace_counters").append(h3);
	//var div = document.createElement("div");
	//$("#trace_counters").append(div);

	for (var counter of oa_query.counters) {
	    var counter_div = $("<div/>", { id: "counter-trace-" + counter.index, "class": "counter", html: counter.name });
	    counter_div.data("counter", counter);

	    counter_div.draggable({ containment: "window", scroll: false, revert: true, helper: "clone" });

	    $("#trace_counters").append(counter_div);
	}
    //}
    //$("#trace_counters").accordion({heightStyle: "content"});
    $("#timelines").droppable({
	drop: function(event, ui) {
	    console.log("dropped class = " + ui.draggable.class);
	    if (ui.draggable.hasClass("counter")) {
		var counter = ui.draggable.data("counter");

		var vbox = $("<div/>", { style: "display:flex; flex-direction:column;" });
		var hbox_ltr = $("<div/>", { style: "display:flex; width:100%;" })
		var hbox_rtl = $("<div/>", { style: "display:flex; flex-direction:row-reverse; flex:1 0 auto;" })
		var name = $("<div/>", { html: ui.draggable.html(), style: "flex: 1 1 auto;" });
		var close = $("<div/>").button( { icons: { primary: "ui-icon-close" }, text: false, style: "flex: 0 0 auto;" });
		/* XXX: be careful to ensure we also free the plot data */
		close.click(function() {
		    vbox.remove();
		    delete trace_graphs[counter.index];
		})
		hbox_ltr.append(name, hbox_rtl);
		hbox_rtl.append(close);

		var graph = $("<div/>", { "class": "trace-graph", style: "flex:0 1 auto; height:10em;"});
		var plot_data = { "counter": counter, values: [], "max": 0 };
		graph.data("plot-data", plot_data);
		//graph.data("counter", counter);
		//graph.data("counter-values", []);
		trace_graphs[counter.index] = graph;

		vbox.append(graph);

		vbox.append(hbox_ltr, graph);
		$(this).append(vbox);

		$.plot($(graph), [ [[0, 0], [1, 1]] ], { yaxis: { max: 1 } });
		//$("#trace_counters").accordion();
	    }
	}
    });
    $("#timelines").sortable();
}

function trace_ui_activate()
{
    start_timestamp = null;
    current_oa_query_update_handler = trace_ui_handle_oa_query_update;

    var select = $("#trace_metrics_select");
    select.empty();

    for (var i in all_oa_queries) {
	var q = all_oa_queries[i];
	var opt = document.createElement("option");
	opt.setAttribute("value", i);
	if (i === 0)
	    opt.setAttribute("selected", "selected");
	opt.innerHTML = q.name;

	select.append($(opt));
    }

    select.selectmenu({
	change: function() {
	    var selected = $("#trace_metrics_select").val();
	    setup_trace_ui_for_oa_query(selected);
	    if (current_oa_query_id) {
		close_query(current_oa_query_id, function() {
		    open_oa_query_for_trace(selected);
		})
	    }
	}
    });

    setup_trace_ui_for_oa_query(0);
    if (!current_oa_query_id) {
	open_oa_query_for_trace(0);
    } else {
	close_query(current_oa_query_id, function () {
	    open_oa_query_for_trace(0);
	});
    }
}

function forensic_trace_ui_handle_oa_query_update(update)
{
    var oa_query = current_oa_query;
    //var i = 0;

    //var graph_data = [];

    if (!update) {
	console.warn("Spurious undefined counters update");
	return;
    }
}

function forensic_ui_activate()
{
    var oa_query = all_oa_queries[0];

    close_queries();

    current_oa_query_update_handler = forensic_trace_ui_handle_oa_query_update;

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
    var period_exponent = 16;

    rpc({ "method": "open_oa_query",
	  "params": [ next_query_id,
		      1, /* 3D metric set */
		      period_exponent,
		      true, /* overwrite old samples */
		      100, /* milliseconds of aggregation
			    * i.e. request updates from the worker
			    * as values that have been aggregated
			    * over this duration */
		      false /* don't send live updates */
	  ] });

    //rpc({ method: "set_aggregation_duration",
	//  params: [ 100 ] });

    query_handles.push(next_query_id++);
    current_oa_query = oa_query;

    rpc({ "method": "open_generic",
	  "params": [ next_query_id,
		      -1, /* pid */
		      0, /* cpu - FIXME: open trace for all cpus */
		      1, /* _TYPE_SOFWARE */
		      3, /* _COUNT_SW_CONTEXT_SWITCHES */
		      true, /* overwrite old samples */
		      false /* don't send live updates */
		    ] });
    query_handles.push(next_query_id++);

    rpc({ "method": "open_tracepoint",
	  "params": [ next_query_id,
		      -1, /* pid */
		      0, /* cpu - FIXME: open trace for all cpus */
		      "i915",
		      "intel_gpu_freq_change",
		      true, /* overwrite old samples */
		      false /* don't send live updates */
		    ] });
    query_handles.push(next_query_id++);

}

function gputop_ui_on_features_notify(f)
{
    features = f;

    all_oa_queries = features.oa_queries;
    $("#tabs").tabs("option", "disabled", false);

    $("#n_cpus").html(features.n_cpus);
    $("#cpu_model").html(features.cpu_model);
    $("#kernel_release").html(features.kernel_release);
    $("#kernel_build").html(features.kernel_build);

    overview_ui_activate();
}

function close_query(id, ondone)
{
    rpc({ "method": "close_oa_query",
	  "params": [ id ] },
	ondone);
}

function close_queries(ondone)
{
    var closing = [];

    for (var id of query_handles) {
	closing.push(id);
	close_query(id, function (c) {
	    delete closing[id];
	    if (closing.length === 0)
		ondone();
	});
    }

    query_handles = [];
}

function gputop_ui_on_oa_query_update(update)
{
    current_oa_query_update_handler(update);
}

var current_oa_query_id = 0;
var overview_elements = [];

function overview_ui_handle_oa_query_update(update)
{
    var oa_query = current_oa_query;
    var i = 0;

    //console.log("overview update");

    for (var value of update.counters) {
	var counter = oa_query.counters[i];
	var hbar = overview_elements[i];

	var canvas = $(hbar).find(".bar-canvas")[0];
	var ctx = canvas.getContext("2d");

	ctx.clearRect(0, 0, canvas.width, canvas.height);

	var bar_width = 200;
	var val = value[0];
	if (val !== 0)
	    hbar.css("display", "flex");
	var maximum = value[1];
	if (maximum) {
	    var norm = (val / maximum);
	    if (norm > 1) {
		norm = 1.1;
		ctx.fillStyle = "#ffff00";
	    } else if (norm > 0.9)
		ctx.fillStyle = "#ff0000";
	    else
		ctx.fillStyle = "#00ff00";

	    ctx.fillRect(0, 0, norm * 200, canvas.height);
	} else {
	    var norm = val;
	    ctx.font = "30px Ariel";
	    ctx.fillText("" + val, 0, 0);
	}

	/*
	 * var ctx = canvas.getContext("2d");
	 * ctx.fillRect(0, 0, norm, canvas.height);
	 */

	i++;
    }
}

function open_oa_query_for_overview(idx)
{
    var oa_query = all_oa_queries[idx];

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
    var period_exponent = 16;

    query_id = next_query_id++;
    rpc({ "method": "open_oa_query",
	  "params": [ query_id,
		      oa_query.metric_set,
		      period_exponent,
		      false, /* don't overwrite old samples */
		      1000000000, /* nanoseconds of aggregation
			     * i.e. request updates from the worker
			     * as values that have been aggregated
			     * over this duration */
		      true /* send live updates */
		    ] });
    query_handles.push(query_id);
    current_oa_query = oa_query;
    current_oa_query_id = query_id;
}

function setup_overview_for_oa_query(idx)
{
    var oa_query = all_oa_queries[idx];

    $("#overview_bar_graph").empty();
    overview_elements = [];

	//$.plot($(graph), [ graph_data ], { series: { lines: { show: true, fill: true }, shadowSize: 0 },
//					   xaxis: { min: x_min, max: x_max },
//					   yaxis: { max: 100 }
    var axis = $("<div/>", { style: "height:2em; width:200px;"});
    var div = $("<div/>", { style: "display:flex; flex-direction:row;" } )
		.append($("<div/>", { style: "width:20em; flex: 0 1 auto;" }))
		    .append(axis);
    $.plot($(axis), [], { yaxis: { show: false }, xaxis: { show: true, position: "top", min: 0, max: 100 }, grid: { borderWidth: 0 } });
    $("#overview_bar_graph").append(div);

    for (var counter of oa_query.counters) {
	var template = $("#hbar-template").clone();

	var name = $(template).find(".bar-name");
	name.html(counter.name);

	overview_elements.push(template);
	$("#overview_bar_graph").append(template);
    }
}

function overview_ui_activate()
{
    current_oa_query_update_handler = overview_ui_handle_oa_query_update;

    var select = $("#overview_metrics_select");
    select.empty();

    for (var i in all_oa_queries) {
	var q = all_oa_queries[i];
	var opt = document.createElement("option");
	opt.setAttribute("value", i);
	if (i === 0)
	    opt.setAttribute("selected", "selected");
	opt.innerHTML = q.name;

	select.append($(opt));
    }

    select.selectmenu({
	change: function() {
	    var selected = $("#overview_metrics_select").val();
	    setup_overview_for_oa_query(selected);
	    if (current_oa_query_id) {
		close_query(current_oa_query_id, function() {
		    open_oa_query_for_overview(selected);
		})
	    }
	}
    });

    setup_overview_for_oa_query(0);
    if (!current_oa_query_id) {
	open_oa_query_for_overview(0);
    } else {
	close_query(current_oa_query_id, function () {
	    open_oa_query_for_overview(0);
	});
    }
}

function architecture_ui_activate()
{
    close_queries();

    var svg = $("#architecture-diagram");
    //svg.empty();

    var diag = Snap(svg[0]);

    Snap.load("haswell.svg", function(frag) {
	var g = frag.select("#eugroup");
	diag.append(g);
    });
}

function architecture_ui_deactivate()
{

}

function activate_log_ui()
{
}

function gputop_ui_on_log(entries)
{
    console.log("LOG:");
    for (var entry of entries) {
	var div = document.createElement("div");
	div.innerHTML = entry.message;
	switch (entry.level) {
	    case 1:
		div.className="level-high";
		break;
	    case 2:
		div.className="level-medium";
		break;
	    case 3:
		div.className="level-low";
		break;
	    case 4:
		div.className="level-notification";
		break;
	    default:
	}
	$("#log_tab").append(div);
	console.log(JSON.stringify(entry));
    }
}

function gputop_ui_on_close_notify(query_id)
{
    console.log("close notify: ID=" + query_id);
}

function gputop_ui_on_bad_report(e)
{
    console.warn("Bad report: query ID = " + e.id + " timestamp = " + e.timestamp);
}

var ww = new Worker("gputop-web-worker.js")

ww.onmessage = function(e) {
    //console.log(e.data);
    try {
	var rpc = JSON.parse(e.data);
    } catch (err) {
	console.warn("Failed to parse message from worker: " + e.data + "\n> because: " + err.message);
	return;
    }

    var args = rpc.params;

    if (rpc.method)
	window["gputop_ui_on_" + rpc.method].apply(this, args);
    else {
	if (rpc.uuid in rpc_closures) {
	    var closure = rpc_closures[rpc.uuid];
	    closure.apply(this, args);
	    delete rpc_closures[rpc.uuid];
	}
    }
}

var tab_activators = {}
tab_activators.overview_tab = overview_ui_activate;
tab_activators.trace_tab = trace_ui_activate;
tab_activators.forensic_tab = forensic_ui_activate;
tab_activators.architecture_tab = architecture_ui_activate;
tab_activators.log_tab = activate_log_ui;

var tab_deactivators = {}
//tab_deactivators.overview_tab = overview_ui_deactivate;
//tab_deactivators.trace_tab = trace_ui_deactivate;
//tab_activators.forensic_tab = forensic_ui_deactivate;
tab_deactivators.architecture_tab = architecture_ui_deactivate;
//tab_deactivators.log_tab = log_ui_deactivate;

$("#tabs").tabs({ heightStyle: "content",
		  disabled: true,
		  activate: function(event, ui) {
		      var new_id = $(ui.newPanel).attr('id');
		      var activator = tab_activators[new_id];
		      var old_id = $(ui.oldPanel).attr('id');
		      var deactivator = tab_deactivators[old_id];
		      if (deactivator)
			  deactivator();
		      if (activator)
			  activator();
		  },
		});

rpc({ "method": "test", "params": ["arg0", 3.1415 ] },
    function test_method_done(msg) {
	console.log("Test method reply received: " + msg);
    });

//$.plot($("#overview-graph"), [ [[0, 0], [1, 1]] ], { yaxis: { max: 1 } });
/*
var data = [ ["January", 10], ["February", 8], ["March", 4], ["April", 13], ["May", 17], ["June", 9] ];
$.plot($("#overview-graph"), [ data ],
       {
	   series: {
	       bars: {
		   show: true,
		   barWidth: 0.6,
		   align: "center"
	       }
	   },
	   xaxis: {
	       mode: "categories",
	       tickLength: 0
	   }

	   //yaxis: { max: 100 }
       });
*/
