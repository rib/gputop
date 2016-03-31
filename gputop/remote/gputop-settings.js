"use strict";

//# sourceURL=gputop-settings.js
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

var gputop_settings;

//------------------------------ Settings --------------------------------------
function Settings() {
}

Settings.prototype.set = function (key, value) {
    $.cookie(key, value);
}

Settings.prototype.get = function (key, default_value) {
    var value = $.cookie(key);
    if (value == null)
        return default_value;

    return value;
}

Settings.prototype.get_bool = function (key, default_value) {
    var ret = this.get(key, default_value);
    if (ret == null)
        return false;
        
    return ret == "true";
}

Settings.prototype.set_include_pids = function (value) {
    $.cookie('gputop.include_pids', value);
}

Settings.prototype.set_include_ctx_ids = function (value) {
    $.cookie('gputop.include_ctx_ids', value);
}

Settings.prototype.load = function () {
    gputop.include_pids_ = this.get_bool('gputop.include_pids', gputop.include_pids_);
    gputop.include_ctx_ids_ = this.get_bool('gputop.include_ctx_ids', gputop.include_ctx_ids_);
}

gputop_settings = new Settings();