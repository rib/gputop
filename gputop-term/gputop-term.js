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
const blessed = require('blessed');
const fs = require('fs');
const ArgumentParser = require('argparse').ArgumentParser;


function GputopTerm()
{
    Gputop.Gputop.call(this);

    var screen = blessed.screen({
        smartCSR: true,
        debug: true,
        log: "flibble.txt"
    });

    screen.title = "GPUTOP";
    this.screen = screen;

    //screen.append(this.topnav);

    screen.log(screen.height);
    screen.debug("foo");
    //process.exit(0);
    this.topnav = blessed.listbar({
        parent: screen,
        autoCommandKeys: true,
        mouse: true,
        keys: true,
        commands: {
            "Overview": {
                callback: () => {
                }
            },
            "Log": {
                callback: () => {
                    screen.debug("log button");
                    this.log_widget.hidden = false;
                }
            }
        }
    });

    this.sidenav = blessed.list({
        parent: screen,
        top: 1,
        left: 0,
        width: '20%',
        height: "100%-1",
        items: [ "Overview", "OA Metrics", "Log" ],
        tags: true,
        keys: true,
        mouse: true,
        border: {
            type: 'line'
        },
        style: {
            fg: 'white',
            bg: 'magenta',
            border: {
                fg: '#f0f0f0'
            },
            hover: {
                bg: 'green'
            }
        }
    });
    //screen.append(this.sidenav);

    this.log_widget = blessed.log({
        parent: screen,
        top: 1,
        left: '20%',
        width: '80%',
        height: '100%-1',
        tags: true,
        border: {
            type: 'line'
        },
        scrollable: true,
        scrollbar: true,
        input: true,
        hidden: true,
        style: {
            fg: 'white',
            bg: 'magenta',
            border: {
                fg: '#f0f0f0'
            },
            hover: {
                bg: 'green'
            },
            scrollbar: {
                bg: 'blue'
            },
        }
    });

    this.console = {
        log: (msg) => {
            //this.log_widget.log(msg);
        },
        warn: (msg) => {
            //this.log_widget.log("WARN: " + msg);
        },
        error: (msg) => {
            //this.log_widget.log("ERROR: " + msg);
        },
    };
    //screen.append(this.log_widget);
    this.log_widget.focus();

    this.messagebar = blessed.message({
        parent: screen,
        top: '100%-5',
        left: 'center',
        height: 3,
        width: '80%',
        border: {
            type: 'line'
        },
    });


    // If our box is clicked, change the content.
    //box.on('click', function(data) {
    //  box.setContent('{center}Some different {red-fg}content{/red-fg}.{/center}');
    //  screen.render();
    //});

    // If box is focused, handle `enter`/`return` and give us some more content.
    /*
       box.key('enter', function(ch, key) {
       box.setContent('{right}Even different {black-fg}content{/black-fg}.{/right}\n');
       box.setLine(1, 'bar');
       box.insertLine(1, 'foo');
       screen.render();
       });
       */
    // Quit on Escape, q, or Control-C.
    screen.key(['escape', 'q', 'C-c'], function(ch, key) {
        return process.exit(0);
    });

}

GputopTerm.prototype = Object.create(Gputop.Gputop.prototype);

GputopTerm.prototype.user_msg = function(message, level){
    if (level === undefined)
        level = this.LOG;

    // types of bootstrap alerts: alert-success alert-info alert-warning alert-danger
    switch (level) {
    case this.LOG:
        this.messagebar.log(message);
        break;
    case this.WARN:
        this.messagebar.log("WARNING: " + message);
        break;
    case this.ERROR:
        this.messagebar.log("ERROR: " + message);
        break;
    default:
        this.console.error("User message given with unknown level " + level + ": " + message);
        return;
    }
}


GputopTerm.prototype.update_features = function(features)
{
    for (var i = 0; i < features.supported_oa_query_guids.length; i++) {
        var guid = features.supported_oa_query_guids[i];
        var metric = this.lookup_metric_for_guid(guid);

        this.sidenav.add(metric.name);
    }

    this.screen.render();
}

//GputopTerm.prototype.log = function(message, level)
//{
//}

GputopTerm.prototype.notify_metric_updated = function(metric) {
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

var args = parser.parseArgs();

var gputop;

function init() {
    gputop = new GputopTerm();

    gputop.screen.render();

    gputop.connect(args.address, () => {
        gputop.log("connected");
    });

}

init();
