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


var active_tab = $("#overview_tab");

var all_oa_queries;
var current_oa_query;

var next_query_id = 1;
var oa_query_handles = [];

var current_oa_query_update_handler = function (update) {};

var trace_graphs = [];

var redraw_queued = false;

function queue_redraw(callback) {
    if (!redraw_queued) {
	window.requestAnimationFrame(function (timestamp) { redraw_queued = false; callback(timestamp); } );
	redraw_queued = true;
    }
}

var trace_ui_updates = [];

function trace_ui_redraw(timestamp)
{
    var n_updates = trace_ui_updates.length;
    var n_counters = trace_ui_updates[0].counters.length;
    var value = [0, 100];
    var x_min = 0;
    var x_max = 1;

    x_max = trace_ui_updates[n_updates - 1].gpu_end;
    x_min = x_max - 5000000000; /* five second */

    for (var i = 0; i < n_counters; i++) {
	var graph = trace_graphs[i];
	if (!graph)
	    continue;

	var plot_data = graph.data("plot-data");
	var graph_data = plot_data.values;

	/* what samples should we discard?... */
	for (var j = 0; j < graph_data.length && graph_data[j][0] < x_min; j++) {}

	graph_data = graph_data.slice(j);

	for (var j = 0; j < n_updates; j++) {
	    var update = trace_ui_updates[j];
	    var start = update.gpu_start;
	    var end = update.gpu_end;

	    if (start < x_min || start === end || end < start || start === 0 || end === 0) {
		console.warn("Spurious update timestamps: start=" + start + ", end=" + end);
		continue;
	    }
	    var mid = start + ((end - start) / 2);

	    graph_data.push([mid, update.counters[i][0]]);
	}

	plot_data.values = graph_data;

	$.plot($(graph), [ graph_data ], { series: { lines: { show: true, fill: true }, shadowSize: 0 },
					   xaxis: { min: x_min, max: x_max },
					   yaxis: { max: 100 }
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

    if (!update) {
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


function trace_ui_activate()
{
    var oa_query = all_oa_queries[0];

    close_oa_queries();

    current_oa_query_update_handler = trace_ui_handle_oa_query_update;

    trace_graphs = [];
    $("#sidebar").empty();

    for (var query of all_oa_queries) {
	var h3 = $("<h3/>", { html: query.name });
	$("#sidebar").append(h3);
	var div = document.createElement("div");
	$("#sidebar").append(div);

	for (var counter of query.counters) {
	    var counter_div = $("<div/>", { id: "counter-trace-" + counter.index, "class": "counter", html: counter.name });
	    counter_div.data("counter", counter);

	    counter_div.draggable({ containment: "window", scroll: false, revert: true, helper: "clone" });

	    $(div).append(counter_div);
	}
    }
    $("#sidebar").accordion({heightStyle: "content"});
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
		hbox_ltr.append(name, hbox_rtl);
		hbox_rtl.append(close);

		var graph = $("<div/>", { "class": "trace-graph", style: "flex:0 1 auto; height:10em;"});
		var plot_data = { "counter": counter, values: [] };
		graph.data("plot-data", plot_data);
		//graph.data("counter", counter);
		//graph.data("counter-values", []);
		trace_graphs[counter.index] = graph;

		vbox.append(graph);

		vbox.append(hbox_ltr, graph);
		$(this).append(vbox);

		$.plot($(graph), [ [[0, 0], [1, 1]] ], { yaxis: { max: 1 } });
		//$("#sidebar").accordion();
	    }
	}
    });
    $("#timelines").sortable();

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

    ww.postMessage({ "method": "open_oa_query",
		     "params": [ next_query_id,
				 oa_query.metric_set,
				 period_exponent,
				 false, /* don't overwrite old samples */
				 100 /* milliseconds of aggregation
				      * i.e. request updates from the worker
				      * as values that have been aggregated
				      * over one second */ ] });

    oa_query_handles.push(next_query_id++);
    current_oa_query = oa_query;
}

function forensic_ui_activate()
{
    close_oa_queries();

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

    ww.postMessage({ "method": "open_oa_query",
		     "params": [ next_query_id,
				 1, /* 3D metric set */
				 period_exponent,
				 false, /* don't overwrite old samples */
				 1000 /* milliseconds of aggregation
				       * i.e. request updates from the worker
				       * as values that have been aggregated
				       * over one second */ ] });
    oa_query_handles.push(next_query_id++);
}

function gputop_ui_on_features_notify(features)
{
    all_oa_queries = features.oa_queries;
    $("#tabs").tabs("option", "disabled", false);
    overview_ui_activate();
}

function close_oa_queries()
{
    console.log("close_oa_queries()");
    for (var id of oa_query_handles) {
	console.log(" > close ID = " + id);
	ww.postMessage({ "method": "close_oa_query",
			 "params": [ id ] });
    }

    oa_query_handles = [];
}

function gputop_ui_on_oa_query_update(update)
{
    current_oa_query_update_handler(update);
}

var overview_elements = [];

function overview_ui_handle_oa_query_update(update)
{
    var oa_query = current_oa_query;
    var i = 0;

    //console.log("overview update");

    for (var value of update.counters) {
	var counter = oa_query.counters[i];
	var hbar = overview_elements[i];

	var canvas = $(hbar).find(".bar-canvas");

	var val = value[0];
	var maximum = value[1];
	if (maximum) {
	    var norm = (val / maximum) * 100;
	    if (norm > 100) {
		norm = 120;
		canvas.css("background-color", "#ffff00");
	    } else if (norm > 90)
		canvas.css("background-color", "#ff0000");
	    else
		canvas.css("background-color", "#00ff00");

	    canvas.css("width", norm);
	} else {
	    var norm = val;
	    if (norm > 100)
		norm = 100;
	    canvas.css("width", norm);
	    canvas.css("background-color", "#000000");
	    //canvas.css("display", "none");
	}

	/*
	 * var ctx = canvas.getContext("2d");
	 * ctx.fillRect(0, 0, norm, canvas.height);
	 */

	i++;
    }
}

function overview_ui_activate()
{
    var oa_query = all_oa_queries[0];

    close_oa_queries();

    current_oa_query_update_handler = overview_ui_handle_oa_query_update;

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

    ww.postMessage({ "method": "open_oa_query",
		     "params": [ next_query_id,
				 oa_query.metric_set,
				 period_exponent,
				 false, /* don't overwrite old samples */
				 1000 /* milliseconds of aggregation
				       * i.e. request updates from the worker
				       * as values that have been aggregated
				       * over one second */ ] });
    oa_query_handles.push(next_query_id++);
    current_oa_query = oa_query;

    $("#overview_tab").empty();
    overview_elements = [];

    for (var counter of oa_query.counters) {
	var template = $("#hbar-template").clone();

	var name = $(template).find(".bar-name");
	name.html(counter.name);

	overview_elements.push(template);
	$("#overview_tab").append(template);
    }
}

function architecture_ui_activate()
{
    close_oa_queries();

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
    }
    console.log(entries);
}

function gputop_ui_on_close_notify(query_id)
{
    console.log("close notify: ID=" + query_id);
}

var ww = new Worker("gputop-web-worker.js")

var rpc_closures = {};

ww.onmessage = function(e) {
    //console.log(e.data);
    var rpc = JSON.parse(e.data);
    var args = rpc.params;

    if (rpc.method)
	window["gputop_ui_on_" + rpc.method].apply(this, args);
    else {
	var closure = rpc_closures[rpc.id];
	if (closure) {
	    closure.ondone.apply(this, args);
	    delete rpc_closures[rpc.id];
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

ww.postMessage({ "method": "test", "params": ["arg0", 3.1415 ] });

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
