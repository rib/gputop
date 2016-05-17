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
const ArgumentParser = require('argparse');

function GputopCSV()
{
    Gputop.Gputop.call(this);

    this.stream = undefined;
    this.metric = undefined;

    this.write_queued_ = false;
}

GputopCSV.prototype = Object.create(Gputop.Gputop.prototype);

GputopCSV.prototype.update_features = function(features)
{
    console.log(features);

    if (features.supported_oa_query_guids.length === 0) {
        console.error("No OA metrics supported");
    } else {
        var guid = features.supported_oa_query_guids[0];
        this.metric = this.get_map_metric(guid);

        if (this.metric === undefined) {
            console.error("Failed to look up metric " + guid);
            return;
        }
        var columns = "Timestamp";

        this.metric.emc_counters_.forEach((counter, i, arr) => {
            columns += ",\"" + counter.symbol_name + "\"";
            counter.record_data = true;
        });

        /* RFC 4180 says to use DOS style \r\n line endings and we
         * ignore that because... reasons... */
        this.stream.write(columns + "\n");

        this.open_oa_metric_set({guid: this.metric.guid_});
    }
}

function write_rows()
{
    var metric = this.metric;

    if (metric === undefined)
        return;

    var first_counter = metric.emc_counters_[0];
    var n_rows = first_counter.updates.length;

    if (n_rows <= 1)
        return;

    /* Ignore the latest updates in case we haven't received an update
     * for all counter yet... */
    n_rows -= 1;

    for (var r = 0; r < n_rows; r++) {

        //console.log("start row " + r);
        var start = first_counter.updates[r][0];
        var end = first_counter.updates[r][1];
        var row_timestamp = start + (end - start) / 2;

        var row = "" + row_timestamp;

        for (var c = 0; c < metric.emc_counters_.length; c++) {
            //console.log("c = " + c);
            var counter = metric.emc_counters_[c];

            start = counter.updates[r][0];
            end = counter.updates[r][1];
            var val = counter.updates[r][2];
            var max = counter.updates[r][3];
            var mid = start + (end - start) / 2;

            console.assert(mid === row_timestamp, "Inconsistent row timestamp");

            row += "," + val;
        }

        /* RFC 4180 says to use DOS style \r\n line endings and we
         * ignore that because... reasons... */
        this.stream.write(row + "\n");
        console.log("wrote row " + r + ": " + row);
    }

    for (var c = 0; c < metric.emc_counters_.length; c++) {
        var counter = metric.emc_counters_[c];
        counter.updates.splice(0, n_rows);
    }
}

GputopCSV.prototype.queue_redraw = function() {
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
    console.log(level);
    console.log(message);
}

var gputop;

var stream = fs.createWriteStream("my_file.csv");

stream.once('open', (fd) => {
    console.log("opened file");

    gputop = new GputopCSV();

    gputop.stream = stream;

    gputop.connect(() => {
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

