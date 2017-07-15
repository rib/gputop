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
const fs = require('fs');
const ArgumentParser = require('argparse').ArgumentParser;
const sp = require('sprintf');

/* Don't want to pollute CSV output to stdout with log messages... */
var stderr_log = new console.Console(process.stderr, process.stderr);

function GputopCSV(pretty_print)
{
    Gputop.Gputop.call(this);

    this.pretty_print_csv_ = pretty_print;

    this.stream = undefined;
    this.metric = undefined;

    this.requested_columns_ = [];

    this.counters_ = [];

    this.dummy_timestamp_counter = {
        symbol_name: "Timestamp"
    };

    /* It's possible some columns will not correspond to
     * available counters but we need to know at least
     * one column with real data to check how many updates
     * we've received.
     */
    this.reference_column = -1;

    this.write_queued_ = false;
    this.endl = process.platform === "win32" ? "\r\n" : "\n";

    this.term_row_ = 0;

    this.console = {
        log: (msg) => {
            if (args.debug)
                stderr_log.log(msg);
        },
        warn: (msg) => {
            if (args.debug)
                stderr_log.warn(msg);
        },
        error: (msg) => {
            stderr_log.error(msg);
        },
    };
}

GputopCSV.prototype = Object.create(Gputop.Gputop.prototype);

GputopCSV.prototype.list_metrics = function(features) {
    var all_metrics = [];

    for (var i = 0; i < features.supported_oa_uuids.length; i++) {
        var uuid = features.supported_oa_uuids[i];
        all_metrics.push(this.lookup_metric_for_uuid(uuid));
    }

    all_metrics.sort((a, b) => {
        if (a.symbol_name < b.symbol_name)
            return -1;
        if (a.symbol_name > b.symbol_name)
            return 1;
        return 0;
    });

    for (var i = 0; i < all_metrics.length; i++) {
        stderr_log.log("" + all_metrics[i].symbol_name + ": " + all_metrics[i].name + ", hw-config-guid=" + uuid);
    }

};

GputopCSV.prototype.list_metric_set_counters = function(metric) {
    var all_counters = [{ symbol_name: "Timestamp", name: "Timestamp", desc: "Sample timestamp (nanosecond resolution)" }];
    var all = "Timestamp"

    metric.cc_counters.forEach((counter, idx, arr) => {
        var units = counter.units;

        if (counter.duration_dependent)
            units += '/s';

        all_counters.push({ symbol_name: counter.symbol_name, name: counter.name, desc: counter.description + " (" + units + ")" });
        all += "," + counter.symbol_name;
    });
    all_counters.sort((a, b) => {
        if (a.symbol_name < b.symbol_name)
            return -1;
        if (a.symbol_name > b.symbol_name)
            return 1;
        return 0;
    });
    for (var i = 0, len = all_counters.length; i < len; i++) {
        stderr_log.log(sp.sprintf("%-25s:%-25s - %s",
                       all_counters[i].symbol_name,
                       all_counters[i].name,
                       all_counters[i].desc));
    }
    stderr_log.log("\nALL: " + all);
}

GputopCSV.prototype.update_features = function(features)
{
    if (features.supported_oa_uuids.length == 0) {
        stderr_log.error("No OA metrics supported");
        process.exit(1);
        return;
    }

    if (args.metrics === 'list') {
        stderr_log.log("\nList of metric sets selectable with --metrics=...");
        this.list_metrics(features);
        process.exit(1);
    }

    for (var i = 0; i < features.supported_oa_uuids.length; i++) {
        var uuid = features.supported_oa_uuids[i];
        var metric = this.lookup_metric_for_uuid(uuid);

        if (metric.symbol_name === args.metrics) {
            this.metric = metric;
            break;
        }
    }

    if (this.metric === undefined) {
        stderr_log.error('Failed to look up metric set "' + args.metrics + '"');
        process.exit(1);
        return;
    }

    if (args.period < 0 || args.period > 1000000000) {
        stderr_log.error('Sampling period out of range [0, 1000000000]');
        process.exit(1);
        return;
    }

    if (args.oa_sample_exponent >= 0) {
        if (args.oa_sample_exponent > 31) {
            stderr_log.error('OA exponent out of range [0, 31]');
            process.exit(1);
            return;
        }

        var hw_period = this.oa_exponent_to_nsec(args.oa_sample_exponent);

        if (hw_period > 40000000)
            stderr_log.warn("WARNING: EU counters may overflow 32 bits with a long hardware sampling period (recommend < 40 millisecond period)");

        var oa_sampling_state = {
            oa_exponent: args.oa_sample_exponent,
            period: hw_period,
            factor: Math.ceil(args.period / hw_period)
        };
    } else {
        var oa_sampling_state =
            this.calculate_sample_state_for_accumulation_period(args.period,
                                                                40000000, // max period to account for 32bit counter overflow
                                                                args.period / 10); // 10% error margin
    }

    if (args.columns === 'list') {
        stderr_log.log("\nList of counters selectable with --columns=... (comma separated):\n");
        this.list_metric_set_counters(this.metric);
        process.exit(1);
    }

    var counter_index = {};
    this.metric.cc_counters.forEach((counter, idx, arr) => {
        counter_index[counter.symbol_name] = counter;
    });

    for (var i = 0; i < this.requested_columns_.length; i++) {
        var name = this.requested_columns_[i];

        if (name === "Timestamp") {
            this.counters_.push(this.dummy_timestamp_counter);
        } else if (name in counter_index) {
            var counter = counter_index[name];
            counter.record_data = true;
            this.reference_column = this.counters_.length;
            this.counters_.push(counter);
        } else if (args.allow_unknown_columns) {
            var skip = { symbol_name: name, record_data: false };
            this.counters_.push(skip);
        } else {
            stderr_log.warn("Unsupported counter \"" + name + "\" - possible reasons:");
            stderr_log.warn("> Typo?");
            stderr_log.warn("> The counter might be conditional on a hardware feature - e.g. GT2 vs GT3 vs GT4");
            stderr_log.warn("> The counter isn't part of the the \"" + this.metric.name + "\" metric set");
            stderr_log.warn("\nThese are the available \"" + this.metric.name + "\" counters:\n");
            this.list_metric_set_counters(this.metric);
            process.exit(1);
        }
    }

    if (this.reference_column >= 0) {
        var columns = "";
        var all_col_units = "";
        var title_lines = [];
        var current_titles_width = 0;

        for (var i = 0; i < this.counters_.length; i++) {
            var counter = this.counters_[i];
            var col_width = 0;

            if (this.pretty_print_csv_) {
                if (counter.symbol_name === "Timestamp") {
                    var units = "(ns)";
                    var camel_name = "TimeStamp";
                    var min_width = 15;
                } else {
                    var units = counter.units;

                    if (units === "percent") {
                        var min_width = 6;
                        units = "%";
                    } else {
                        var min_width = 8;
                    }

                    if (counter.duration_dependent)
                        units += '/s';
                    units = "(" + units + ")"

                    var camel_name = this.counters_[i].symbol_name;
                }

                col_width = min_width;

                var col_title_lines = camel_name.split(/(?=[A-Z])/).map((sym) => {
                    return sym.toUpperCase();
                });

                for (var l = title_lines.length; l < col_title_lines.length; l++)
                    title_lines[l] = sp.sprintf("%" + current_titles_width + "s", "");

                for (var l = 0; l < col_title_lines.length; l++) {
                    if (col_title_lines[l].length >= col_width)
                        col_width = col_title_lines[l].length + 1;
                }

                if (units.length >= col_width)
                    col_width = units.length + 1;

                for (var l = 0; l < col_title_lines.length; l++)
                    title_lines[l] = sp.sprintf("%-" + current_titles_width + "s%s", title_lines[l], col_title_lines[l]);

                counter.col_width_ = col_width;
                var col_units = sp.sprintf("%-" + col_width + "s", units);
                all_col_units += col_units

                current_titles_width += col_width;
            } else
                columns += this.counters_[i].symbol_name + ",";
        }

        if (this.pretty_print_csv_) {
            this.column_titles_ = title_lines;
            this.column_units_ = all_col_units.trim();
        } else
            this.column_titles_ = [ columns.trim().slice(0, -1) ]; // drop trailing comma

        stderr_log.warn("\n\nCSV: Capture Settings:");
        stderr_log.warn("CSV:   Server: " + args.address);
        stderr_log.warn("CSV:   File: " + (args.file ? args.file : "STDOUT"));
        stderr_log.warn("CSV:   Metric Set: " + this.metric.name);
        stderr_log.warn("CSV:   Columns: " + args.columns);
        stderr_log.warn("CSV:   OA Hardware Sampling Exponent: " + oa_sampling_state.oa_exponent);
        stderr_log.warn("CSV:   OA Hardware Period: " + oa_sampling_state.period + "ns");
        stderr_log.warn("CSV:   Accumulation period (requested): " + args.period + "ns");
        var real_accumulation_period = oa_sampling_state.factor * oa_sampling_state.period;
        stderr_log.warn("CSV:   Accumulation period (actual): " + real_accumulation_period + "ns (" + oa_sampling_state.period + "ns * " + oa_sampling_state.factor + ")");

        stderr_log.warn("\nCSV: OS Info:");
        stderr_log.warn("CSV:   Kernel Build: " + features.get_kernel_build().trim());
        stderr_log.warn("CSV:   Kernel Release: " + features.get_kernel_release().trim());

        stderr_log.warn("\nCSV: CPU Info:");
        stderr_log.warn("CSV:   Model: " + features.get_cpu_model().trim());
        stderr_log.warn("CSV:   N Cores: " + features.get_n_cpus());

        stderr_log.warn("\nCSV: GPU Info:");
        stderr_log.warn("CSV:   Model: " + features.devinfo.get_prettyname());
        stderr_log.warn("CSV:   N EUs: " + features.devinfo.get_n_eus().toInt());
        stderr_log.warn("CSV:   N EU Slices: " + features.devinfo.get_n_eu_slices().toInt());
        stderr_log.warn("CSV:   N EU Sub Slices Per Slice: " + features.devinfo.get_n_eu_sub_slices().toInt());
        stderr_log.warn("CSV:   EU Threads Count (total): " + features.devinfo.get_eu_threads_count().toInt());
        stderr_log.warn("CSV:   Min Frequncy: " + features.devinfo.get_gt_min_freq().toInt() + "Hz");
        stderr_log.warn("CSV:   Max Frequncy: " + features.devinfo.get_gt_max_freq().toInt() + "Hz");
        stderr_log.warn("CSV:   Timestamp Frequency: " + features.devinfo.get_timestamp_frequency().toInt() + "Hz");

        if (features.notices.length >= 0) {
            stderr_log.warn("\n\nCSV: Capture Notices:");
            features.notices.forEach((notice, i) => {
                stderr_log.warn("CSV:   - " + notice);
            });
        }

        stderr_log.warn("\n\n");

        this.metric.open({ oa_exponent: oa_sampling_state.oa_exponent },
                        () => { //onopen

                            /* The accumulator will keep going until the total
                             * period from combining hw samples is >= the
                             * requested accumulation period, so we slightly
                             * reduce what we request to avoid overshooting.
                             */
                            metric.csv_row_accumulator = metric.create_oa_accumulator({ period_ns: args.period * 0.9999 });

                            this.column_titles_.map((line) => {
                                this.stream.write(line + this.endl);
                            });
                            if (this.pretty_print_csv_)
                                this.stream.write(this.column_units_ + this.endl);
                        },
                        () => { // onerror
                        },
                        () => { // onclose
                        });
    } else {
        stderr_log.error("Failed to find counters matching requested columns");
    }
}

function write_rows(metric, accumulator)
{
    /* Note: this ref[erence] counter is pre-dermined to be one with
     * counter.record_data === true which we know we have valid timestamp
     * information for that we can use for the whole row.
     */
    var ref_counter = this.counters_[this.reference_column];
    var ref_accumulated_counter =
        accumulator.accumulated_counters[ref_counter.cc_counter_id_];

    stderr_log.assert(ref_accumulated_counter.counter === ref_counter,
               "Spurious reference counter state");

    var n_rows = ref_accumulated_counter.updates.length;

    if (n_rows <= 1)
        return;

    /* Ignore the latest updates in case we haven't received an update
     * for all counter yet... */
    n_rows -= 1;

    for (var r = 0; r < n_rows; r++) {

        var row_start = ref_accumulated_counter.updates[r][0];
        var row_end = ref_accumulated_counter.updates[r][1];
        var row_timestamp = row_start + (row_end - row_start) / 2;

        var row = "";

        for (var c = 0; c < this.counters_.length; c++) {
            var counter = this.counters_[c];
            var val = 0;

            if (counter === this.dummy_timestamp_counter) {
                if (this.pretty_print_csv_)
                    row += sp.sprintf("%-" + counter.col_width_ + "s", Math.round(row_timestamp) + ",");
                else
                    row += row_timestamp + ",";
            } else if (counter.record_data === true) {
                var accumulated_counter = accumulator.accumulated_counters[counter.cc_counter_id_];

                var start = accumulated_counter.updates[r][0];
                var end = accumulated_counter.updates[r][1];
                //var max = accumulated_counter.updates[r][3];
                var timestamp = start + (end - start) / 2;

                stderr_log.assert(accumulated_counter.counter === counter, "Accumulated counter doesn't match column counter");
                stderr_log.assert(timestamp === row_timestamp,
                                  "Inconsistent timestamp: row ts: " + row_timestamp + "(" + row_start + ", " + row_end + ") != " +
                                  "counter ts: " + timestamp + "(" + start + "," + end + ")");

                val = accumulated_counter.updates[r][2];

                if (this.pretty_print_csv_) {
                    var formatted_value = this.format_counter_value(accumulated_counter, true);
                    row += sp.sprintf("%-" + counter.col_width_ + "s", formatted_value + ",");
                } else
                    row += val + ","
            } else {
                /* NB: some columns may have placeholder counter objects (with
                 * .record_data == false) if they aren't available on this
                 * system
                 */
                if (this.pretty_print_csv_)
                    row += sp.sprintf("%-" + counter.col_width_ + "s", "N/A,");
                else
                    row += "0,";
            }
        }

        this.stream.write(row.trim().slice(0, -1) + this.endl);
        this.term_row_++;
        if (this.pretty_print_csv_ && this.term_row_ >= (process.stdout.rows - 1)) {
            this.column_titles_.map((line) => {
                this.stream.write(line + this.endl);
            });
            this.stream.write(this.column_units_ + this.endl);
            this.term_row_ = 0;
        }
    }

    for (var c = 0; c < this.counters_.length; c++) {
        var counter = this.counters_[c];
        if (counter.record_data === true) {
            var accumulated_counter =
                accumulator.accumulated_counters[counter.cc_counter_id_];

            accumulated_counter.updates.splice(0, n_rows);
        }
    }
}

GputopCSV.prototype.notify_accumulator_events = function(metric, accumulator, events_mask) {
    if (events_mask & 1) //period elapsed
        this.accumulator_clear(accumulator);

    if (this.write_queued_)
        return;

    setTimeout(() => {
        this.write_queued_ = false;
        write_rows.call(this, metric, accumulator);
    }, 0.2);

    this.write_queued_ = true;
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

parser.addArgument(
    [ '-m', '--metrics' ],
    {
        help: "Metric set to capture (default 'list')",
        defaultValue: 'list',
        constant: 'list',
        nargs: '?'
    }
);

parser.addArgument(
    [ '-c', '--columns' ],
    {
        help: "Comma separated counter symbol names for columns (default = 'list')",
        defaultValue: 'list',
        constant: 'list',
        nargs: '?'
    }
);

parser.addArgument(
    [ '-p', '--period' ],
    {
        help: 'Accumulate HW samples over this period (in nanoseconds) before writting a CSV row. Actual accumulation period may overrun by up to 10%%. (default = one second)',
        type: 'int',
        defaultValue: 1000000000
    }
);

parser.addArgument(
    [ '--oa-sample-exponent' ],
    {
        help: 'Not recommended to override. Determines the OA unit sampling period. By default (-1) automatically finds a period that factors neatly into the accumulation period (within 10%%) but is low enough to account for 32bit counter overflow',
        type: 'int',
        defaultValue: -1
    }
);

parser.addArgument(
    [ '-f', '--file' ],
    {
        help: "CSV file to write",
    }
);

parser.addArgument(
    [ '-u', '--allow-unknown-columns' ],
    {
        help: "For automated profiling compatibility: report unsupported counter columns as zero",
        action: 'storeTrue',
        defaultValue: false
    }
);

parser.addArgument(
    [ '-d', '--debug' ],
    {
        help: "Verbose debug output",
        action: 'storeTrue',
        defaultValue: false
    }
);

var args = parser.parseArgs();

var gputop;
var stream = null;

function init(pretty_print) {
    gputop = new GputopCSV(pretty_print);

    gputop.stream = stream;
    gputop.requested_columns_ = args.columns.split(",");

    gputop.connect(args.address, () => { // onopen
        stderr_log.log("Connected");
    }, () => { // onerror
        stderr_log.log("Failed to connect to address = \"" + args.address + "\"");
    });

}

if (args.file) {
    stream = fs.createWriteStream(args.file);
    stream.once('open', (fd) => {
        init(false);
    });
} else {
    stream = process.stdout;
    init(true);
}

function close_and_exit(signo) {
    if (args.file) {
        stderr_log.log("Closing CSV file...");
        stream.end(() => {
            process.exit(128 + signo);
        });
    } else
        process.exit(128 + signo);
}

process.on('SIGINT', () => { close_and_exit(2); });
process.on('SIGTERM', () => { close_and_exit(15); });
