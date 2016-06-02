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


    function parse_field(str) {
        var field = {};

        var subfields = str.split(';');

        for (var i = 0; i < subfields.length; i++) {
            var subfield = subfields[i].trim();

            if (subfield.match('^field:')) {
                field.field = subfield.split(':')[1];
            } else if (subfield.match('^offset:')) {
                field.offset = Number(subfield.split(':')[1]);
            } else if (subfield.match('^size:')) {
                field.size = Number(subfield.split(':')[1]);
            } else if (subfield.match('^signed:')) {
                field.signed = Boolean(Number(subfield.split(':')[1]));
            }
        }

        return field;
    }

    function parse_tracepoint_format(str) {
        var tracepoint = {};

        var lines = str.split('\n');
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];

            if (line.match('^name:'))
                tracepoint.name = line.slice(6);
            else if (line.match('^ID:'))
                tracepoint.id = Number(line.slice(4));
            else if (line.match('^print fmt:'))
                tracepoint.print_format = line.slice(11);
            else if (line.match('^format:')) {
                tracepoint.common_fields = [];
                tracepoint.event_fields = [];

                for (i++; lines[i].match('^\tfield:'); i++) {
                    line = lines[i];
                    var field = parse_field(line.slice(1));
                    tracepoint.common_fields.push(field);
                }
                if (lines[i + 1].match('\tfield:')) {
                    for (i++; lines[i].match('^\tfield:'); i++) {
                        line = lines[i];
                        var field = parse_field(line.slice(1));
                        tracepoint.event_fields.push(field);
                    }
                }
                break;
            }
        }

        return tracepoint;
    }

    this.rpc_request('get_tracepoint_info', "i915/i915_flip_complete", (msg) => {
        var tracepoint = {};

        tracepoint.id = msg.tracepoint_info.id;

        var format = msg.tracepoint_info.sample_format;
        console.log("Full format description = " + format);

        var tracepoint = parse_tracepoint_format(format);
        console.log("Structured format = " + JSON.stringify(tracepoint));
    });
}


GputopTool.prototype.log = function(level, message)
{
    console.log(message.trim());
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


