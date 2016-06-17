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
const ArgumentParser = require('argparse').ArgumentParser;

function GputopTool()
{
    Gputop.Gputop.call(this);

    this.metric = undefined;

    this.counters_ = [];

    this.write_queued_ = false;
}

GputopTool.prototype = Object.create(Gputop.Gputop.prototype);

GputopTool.prototype.update_features = function(features)
{
    if (features.tracepoints.length === 0) {
        console.error("No tracepoints supported");
        process.exit(1);
        return;
    }

    //for (var i = 0; i < features.tracepoints.length; i++) {
    //    console.log("Tracepoint: " + features.tracepoints[i]);
    //}

    //this.get_tracepoint_info("i915/i915_flip_complete", (info) => {
    this.get_tracepoint_info("i915/i915_gem_request_add", (info) => {
        console.log("i915 tracepoint info = " + JSON.stringify(info));

        this.open_tracepoint(info, {}, () => {
            console.log("Tracepoint opened");
        });
    });
}


var parser = new ArgumentParser({
    version: '0.0.1',
    addHelp: true,
    description: "GPU Top GputopTool"
});

parser.addArgument(
    [ '-a', '--address' ],
    {
        help: 'host:port to connect to (default localhost:7890)',
        defaultValue: 'localhost:7890'
    }
);

var args = parser.parseArgs();

var gputop = new GputopTool();

gputop.connect(args.address, () => {
    //console.log("Connected");
});


