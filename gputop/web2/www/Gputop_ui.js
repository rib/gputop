//@ sourceURL=Gputop_ui.js

function Gputop_ui () {

}

Gputop_ui.prototype.display_counter = function(counter) {
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

        //console.log("  "+delta+" = "+ d_value + "/"+ max +" " + counter.symbol_name);
    } else {
        counter.div_txt_.text(d_value.toFixed(0));// + " " +counter.samples_);
        counter.div_.css("width", "0%");
    }
}

Gputop_ui.prototype.render_bars = function() {
    window.requestAnimationFrame(gputop_ui.window_render_animation_bars);
}

Gputop_ui.prototype.window_render_animation_bars = function(timestamp) {
    var metric = gputop.query_active_;
    if (metric == undefined)
        return;

    window.requestAnimationFrame(gputop_ui.window_render_animation_bars);

    for (var i = 0, l = metric.emc_counters_.length; i < l; i++) {
        var counter = metric.emc_counters_[i];
        gputop_ui.display_counter(counter);
    }
}

Gputop_ui.prototype.metric_not_supported = function(metric) {
    alert(" Metric not supported " + metric.title_)
}

Gputop_ui.prototype.display_features = function(features) {
    $( "#gputop-connecting" ).hide();
    $( "#gputop-cpu" ).append( features.get_cpu_model() );
    $( "#gputop-kernel-build" ).append( features.get_kernel_build() );
    $( "#gputop-kernel-release" ).append( features.get_kernel_release() );
    $( "#gputop-n-cpus" ).append( features.get_n_cpus() );
    $( "#gputop-kernel-performance-query" ).append( features.get_has_gl_performance_query() );
    $( "#gputop-kernel-performance-i915-oa" ).append( features.get_has_i915_oa() );
}

// types of alerts: alert-success alert-info alert-warning alert-danger
Gputop_ui.prototype.show_alert = function(message,alerttype){
    $('#alert_placeholder').append('<div id="alertdiv" class="alert ' +
        alerttype + '"><a class="close" data-dismiss="alert">×</a><span>'+message+'</span></div>')
        setTimeout(function() { // this will automatically close the alert and remove this if the users doesnt close it in 5 secs
            $("#alertdiv").remove();
        }, 5000);
}

Gputop_ui.prototype.log = function(log_level, log_message){
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

Gputop_ui.prototype.syslog = function(message){
    log.value += message + "\n";
}

Gputop_ui.prototype.init_interface = function(){
    $( "#gputop-overview-panel" ).load( "ajax/overview.html", function() {
        console.log('gputop-overview-panel load');
        gputop.connect();
    });
}

Gputop_ui.prototype.load_metrics_panel = function(callback_success) {
    $( '#pane2' ).load( "ajax/metrics.html", function() {
        console.log('Metrics panel loaded');
        callback_success();
    });
}

var gputop_ui = new Gputop_ui();

Gputop_ui.prototype.btn_close_current_query = function() {
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

    $( "#gputop-entries" ).append( '<li><a id="close_query" href="#" onClick>Close Query</a></li>' );
    $('#close_query').click( gputop_ui.btn_close_current_query);

    gputop_ui.init_interface();
    $('#editor').wysiwyg();
});
