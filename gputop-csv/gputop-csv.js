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
    console.log(features);

    if (features.supported_oa_query_guids.length === 0) {
        console.error("No OA metrics supported");
        process.exit(1);
        return;
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
        console.error('Failed to look up metric set "' + args.metrics + '"');
        process.exit(1);
        return;
    }

    var counter_index = {};
    this.metric.emc_counters_.forEach((counter, idx, arr) => {
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
            columns += ",\"" + counter.symbol_name + "\"";

        this.stream.write(columns + this.endl);

        this.open_oa_metric_set({guid: this.metric.guid_});
    } else {
        console.error("Failed to find counters matching requested columns");
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

                console.assert(timestamp === row_timestamp, "Inconsistent row timestamp");

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

GputopCSV.prototype.log = function(level, message)
{
    console.log(message.trim());
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
        help: 'Metric set to capture',
        required: true
    }
);

parser.addArgument(
    [ '-c', '--columns' ],
    {
        help: 'Comma separated counter symbol names for columns',
        required: true
    }
);

parser.addArgument(
    [ '-f', '--file' ],
    {
        help: "CSV file to write",
        required: true
    }
);

var args = parser.parseArgs();

var gputop;

var stream = fs.createWriteStream(args.file);

stream.once('open', (fd) => {
    console.log("opened file");

    gputop = new GputopCSV();

    gputop.stream = stream;
    gputop.requested_columns_ = args.columns.split(",");

    gputop.connect(args.address, () => {
        console.log("connected");
    });
});

function close_and_exit(signo) {
    console.log("Closing...");
    stream.end();
    process.exit(128 + signo);
}

process.on('SIGINT', () => { close_and_exit(2); });
process.on('SIGTERM', () => { close_and_exit(15); });

