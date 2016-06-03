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

function GputopCSV()
{
    Gputop.Gputop.call(this);

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
}

GputopCSV.prototype = Object.create(Gputop.Gputop.prototype);

GputopCSV.prototype.update_features = function(features)
{
    if (features.supported_oa_query_guids.length === 0) {
        log.error("No OA metrics supported");
        process.exit(1);
        return;
    }

    if (args.metrics === 'list') {
        log.log("\nList of metric sets selectable with --metrics=...");
        for (var i = 0; i < features.supported_oa_query_guids.length; i++) {
            var guid = features.supported_oa_query_guids[i];
            var metric = this.lookup_metric_for_guid(guid);
            log.log("" + metric.symbol_name + ": " + metric.name + ", hw-config-guid=" + guid);
        }
        process.exit(1);
    }

    for (var i = 0; i < features.supported_oa_query_guids.length; i++) {
        var guid = features.supported_oa_query_guids[i];
        var metric = this.lookup_metric_for_guid(guid);

        if (metric.symbol_name === args.metrics) {
            this.metric = metric;
            break;
        }
    }

    if (this.metric === undefined) {
        log.error('Failed to look up metric set "' + args.metrics + '"');
        process.exit(1);
        return;
    }

    if (args.period < 0 || args.period > 1000000000) {
        log.error('Sampling period out of range [0, 1000000000]');
        process.exit(1);
        return;
    }
    var closest_oa_exponent = gputop.calculate_max_exponent_for_period(args.period);

    if (args.period > 40000000)
        log.warn("WARNING: EU counters may overflow 32 bits with a long sampling period (recommend < 40 millisecond period)");

    if (args.accumulation_period === 0 || args.accumulation_period < args.period) {
        log.error("Counter aggregation period (" + args.aggregation_period + ") should be >= requested hardware sampling period (" + args.period + ")");
        process.exit(1);
    }
    this.metric.set_aggregation_period(args.accumulation_period);

    if (args.columns === 'list') {
        var all = "Timestamp"
        log.log("\nList of counters selectable with --columns=... (comma separated)");
        log.log("Timestamp: Sample timestamp");
        this.metric.webc_counters.forEach((counter, idx, arr) => {
            log.log("" + counter.symbol_name + ": " + counter.name + " - " + counter.description);
            all += "," + counter.symbol_name;
        });
        log.log("\nALL: " + all);
        process.exit(1);
    }

    var counter_index = {};
    this.metric.webc_counters.forEach((counter, idx, arr) => {
        counter_index[counter.symbol_name] = counter;
    });

    for (var i = 0; i < this.requested_columns_.length; i++) {
        var name = this.requested_columns_[i];

        if (name === "Timestamp") {
            this.counters_.push(this.dummy_timestamp_counter);
        } else if (name in counter_index) {
            var counter = counter_index[name]
                counter.record_data = true;
            this.reference_column = this.counters_.length;
            this.counters_.push(counter);
        } else {
            var skip = { symbol_name: name, record_data: false };
            this.counters_.push(skip);
        }
    }

    if (this.reference_column > 0) {
        var columns = this.counters_[0].symbol_name;

        for (var i = 1; i < this.counters_.length; i++)
            columns += ",\"" + this.counters_[i].symbol_name + "\"";

        this.stream.write(columns + this.endl);

        log.warn("\n\nCSV: Capture Settings:");
        log.warn("CSV:   Server: " + args.address);
        log.warn("CSV:   File: " + (args.file ? args.file : "STDOUT"));
        log.warn("CSV:   Metric Set: " + this.metric.name);
        log.warn("CSV:   Columns: " + columns);
        log.warn("CSV:   OA Hardware Period requested: " + args.period);
        log.warn("CSV:   OA Hardware Sampling Exponent: " + closest_oa_exponent);
        log.warn("CSV:   Accumulation period: " + args.accumulation_period);
        log.warn("\n\n");

        this.open_oa_metric_set({guid: this.metric.guid_,
                                 oa_exponent: closest_oa_exponent });
    } else {
        log.error("Failed to find counters matching requested columns");
    }
}

function write_rows()
{
    var metric = this.metric;

    if (metric === undefined)
        return;

    var ref_counter = this.counters_[this.reference_column];
    var n_rows = ref_counter.updates.length;

    if (n_rows <= 1)
        return;

    /* Ignore the latest updates in case we haven't received an update
     * for all counter yet... */
    n_rows -= 1;

    for (var r = 0; r < n_rows; r++) {

        var start = ref_counter.updates[r][0];
        var end = ref_counter.updates[r][1];
        var row_timestamp = start + (end - start) / 2;

        var row = "";

        for (var c = 0; c < this.counters_.length; c++) {
            var counter = this.counters_[c];
            var val = 0;

            if (counter === this.dummy_timestamp_counter) {
                val = row_timestamp;
            } else if (counter.record_data === true) {
                start = counter.updates[r][0];
                end = counter.updates[r][1];
                var max = counter.updates[r][3];
                var timestamp = start + (end - start) / 2;

                log.assert(timestamp === row_timestamp, "Inconsistent row timestamp");

                val = counter.updates[r][2];
            }
            /* NB: some columns may have placeholder counter objects (with
             * .record_data == false) if they aren't available on this
             * system */

            row += val + ",";
        }

        this.stream.write(row.slice(0, -1) + this.endl);
    }

    for (var c = 0; c < this.counters_.length; c++) {
        var counter = this.counters_[c];
        if (counter.record_data === true)
            counter.updates.splice(0, n_rows);
    }
}

GputopCSV.prototype.notify_metric_updated = function(metric) {
    if (this.write_queued_)
        return;

    setTimeout(() => {
        this.write_queued_ = false;
        write_rows.call(this);
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
        defaultValue: 'list'
    }
);

parser.addArgument(
    [ '-c', '--columns' ],
    {
        help: "Comma separated counter symbol names for columns (default = 'list')",
        defaultValue: 'list'
    }
);

parser.addArgument(
    [ '-p', '--period' ],
    {
        help: 'Maximum hardware sampling period, in nanoseconds - actual period may be shorter (default 40 milliseconds)',
        type: 'int',
        defaultValue: 40000000
    }
);

parser.addArgument(
    [ '-g', '--accumulation-period' ],
    {
        help: 'Accumulate HW samples over this period before calculating a CSV row sample (real period will be >= closest multiple of the hardware sampling period)',
        type: 'int',
        defaultValue: 1000000000
    }
);

parser.addArgument(
    [ '-f', '--file' ],
    {
        help: "CSV file to write",
    }
);

var args = parser.parseArgs();

var gputop;
var stream = null;

/* Don't want to polute CSV output to stdout with log messages... */
var log = new console.Console(process.stderr, process.stderr);

function init() {
    gputop = new GputopCSV();

    gputop.stream = stream;
    gputop.requested_columns_ = args.columns.split(",");

    gputop.connect(args.address, () => {
        log.warn("connected"); //use warn to write to stderr
    });

}

if (args.file) {
    stream = fs.createWriteStream(args.file);
    stream.once('open', (fd) => {
        init();
    });
} else{
    stream = process.stdout;
    init();
}

function close_and_exit(signo) {
    if (args.file) {
        log.log("Closing CSV file...");
        stream.end(() => {
            process.exit(128 + signo);
        });
    } else
        process.exit(128 + signo);
}

process.on('SIGINT', () => { close_and_exit(2); });
process.on('SIGTERM', () => { close_and_exit(15); });

