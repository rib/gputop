//@ sourceURL=gputop-ui.js

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
    this.graph_array = [];
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
            // dont know what this is for?!
            markings: function(axes) {
                var markings = [];
                var xaxis = axes.xaxis;
                for (var x = Math.floor(xaxis.min); x < xaxis.max; x += xaxis.tickSize * 2) {
                    markings.push({ xaxis: { from: x, to: x + xaxis.tickSize }, color: "rgba(232, 232, 255, 0.2)" });
                }
                return markings;
            }
        },
        xaxis: {
            show: true,
            min: 0,
            max: 1000
        },
        yaxis: {
            min: 0,
            max: 110
        },
        legend: {
            show: true
        }
    };// options

    this.series = [{
        lines: {
            fill: true
        },
        color: "#6666ff"
    }];

    this.start_timestamp = 0;
    this.start_gpu_timestamp = 0;

}

GputopUI.prototype.update_slider_period = function(period_) {
    slider.setValue(period_);
}

GputopUI.prototype.display_graph = function(timestamp) {
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

        x_max = this.start_gpu_timestamp + elapsed;
        x_min = x_max - 10000000000; // 10 seconds time interval

        // remove the older than 10 seconds samples from the graph data
        for (var j = 0; j < counter.graph_data.length &&
            counter.graph_data[j][0] < (x_min / 100000) - 10000; j++) {}
        if (j > 0)
            counter.graph_data = counter.graph_data.slice(j);

        var save_index = -1;
        for (var j = 0; j < length; j++) {
            var start = counter.updates[j][0];
            var end = counter.updates[j][1];
            var val = counter.updates[j][2]; // value
            counter.graph_data.push([start / 100000, val]);
            save_index = j;
        }

        // resync the timestamps (the javascript timestamp with the counter timestamp)
        if (elapsed > 5000000000 && save_index > -1)
        {
            this.start_timestamp = timestamp;
            this.start_gpu_timestamp = counter.updates[save_index][0];
        }

        // adjust the min and max (start and end of the graph)
        this.graph_options.xaxis.min = (x_min / 100000);
        this.graph_options.xaxis.max = (x_max / 100000) - (gputop.period * 1.5 / 100000);
        this.series[0].data = counter.graph_data;
        $.plot(container, this.series, this.graph_options);

        // remove all the samples from the updates array
        counter.updates.splice(0, counter.updates.length);
    }
}


GputopUI.prototype.display_counter = function(counter) {
    if (counter.invalidate_ == false)
        return;

    if (counter.data_.length == 0)
        return;

    var delta = counter.data_.shift();
    var d_value = counter.data_.shift();
    var max = counter.data_.shift();

    if (counter.div_ == undefined)
        counter.div_ = $('#'+counter.div_bar_id_ );

    if (counter.div_txt_ == undefined)
        counter.div_txt_ = $('#'+counter.div_txt_id_ );

    if (max != 0) {
        var value = 100 * d_value / max;
        counter.div_.css("width", value + "%");
        counter.div_txt_.text(value.toFixed(2));// + " " +counter.samples_);

    } else {
        counter.div_txt_.text(d_value.toFixed(0));// + " " +counter.samples_);
        counter.div_.css("width", "0%");
    }
}

GputopUI.prototype.render_bars = function() {
    window.requestAnimationFrame(gputop_ui.window_render_animation_bars);
    // add support for render graphs as well
}

GputopUI.prototype.window_render_animation_bars = function(timestamp) {
    var metric = gputop.query_active_;
    if (metric == undefined)
        return;

    gputop_ui.display_graph(timestamp);

    window.requestAnimationFrame(gputop_ui.window_render_animation_bars);

    for (var i = 0, l = metric.emc_counters_.length; i < l; i++) {
        var counter = metric.emc_counters_[i];
        gputop_ui.display_counter(counter);
    }
}

GputopUI.prototype.metric_not_supported = function(metric) {
    alert(" Metric not supported " + metric.title_)
}

GputopUI.prototype.display_features = function(features) {
    if (features.devinfo.get_devid() == 0 ) {
        gputop_ui.show_alert(" No device was detected, is it the functionality on kernel ? ","alert-danger");
    }

    $( "#gputop-gpu" ).html( gputop.get_arch_pretty_name() );

    $( ".gputop-connecting" ).hide();
    $( "#gputop-cpu" ).html( features.get_cpu_model() );
    $( "#gputop-kernel-build" ).html( features.get_kernel_build() );
    $( "#gputop-kernel-release" ).html( features.get_kernel_release() );
    $( "#gputop-n-cpus" ).html( features.get_n_cpus() );
    $( "#gputop-kernel-performance-query" ).html( features.get_has_gl_performance_query() );
    $( "#gputop-kernel-performance-i915-oa" ).html( features.get_has_i915_oa() );

    $( "#gputop-n-eus" ).html( features.devinfo.get_n_eus().toInt() );
    $( "#gputop-n-eus-slices" ).html( features.devinfo.get_n_eu_slices().toInt()  );
    $( "#gputop-n-eu-threads-count" ).html( features.devinfo.get_eu_threads_count().toInt()  );

    if (features.get_fake_mode())
        $( "#metrics-tab-a" ).html("Metrics (Fake Mode) ");
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
    $('#editor').append("<font color='"+color+"'>"+log_message+"<br/></font>");
}

GputopUI.prototype.syslog = function(message){
    log.value += message + "\n";
}

GputopUI.prototype.weblog = function(message){
    //log.value += message + "\n";
}

GputopUI.prototype.init_interface = function(){
    $( "#gputop-overview-panel" ).load( "ajax/overview.html", function() {
        console.log('gputop-overview-panel load');
        gputop.connect();
    });
}

GputopUI.prototype.load_metrics_panel = function(callback_success) {
    $( '#pane2' ).load( "ajax/metrics.html", function() {
        console.log('Metrics panel loaded');
        callback_success();
    });
}

var gputop_ui = new GputopUI();

GputopUI.prototype.btn_close_current_query = function() {
    var active_query = gputop.query_active_;
    if (active_query==undefined) {
        gputop_ui.show_alert(" No Active Query","alert-info");
        return;
    }

    gputop.close_oa_query(active_query.oa_query_id_, function() {
       gputop_ui.show_alert(" Success closing query","alert-info");
    });
}

// jquery code
$( document ).ready(function() {
    //log = $( "#log" );
    log = document.getElementById("log");

/*
    $( "#gputop-entries" ).append( '<li><a id="close_query" href="#" onClick>Close Query</a></li>' );
    $( '#close_query' ).click( gputop_ui.btn_close_current_query);
*/
    gputop_ui.init_interface();
    $( '#editor' ).wysiwyg();

    // Display tooltips
    $( '[data-toggle="tooltip"]' ).tooltip();
});
