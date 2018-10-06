/*
 * This file is part of the Soletta™ Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

var widgets = [];
var nodeId = -1;
var isCleared = false;
var max = 2;
var count = 0;


String.prototype.replaceAll = function(search, replacement) {
    var target = this;
    return target.replace(new RegExp(search, 'g'), replacement);
};


var FBPInspector = function () {

    if (!window.EventSource) {
        alert("Your browser does not support server-side-events (SSE): no EventSource().");
        return;
    }
};

FBPInspector.prototype.el = function (id) {
    return document.getElementById(id);
};


FBPInspector.prototype.start = function () {

    this.source = new EventSource(window.location.origin + '/events');
    this.source.fbpInspector = this;
    this.source.onopen = function (e) {
        //TODO
    };

    this.source.onerror = function (e) {
        this.fbpInspector.finish();
    };

    var ev;
    this.source.onmessage = function (e) {
        try {
            feed(e.data);
        } catch (err){
            var fixed = e.data.toString().replaceAll(":inf,",':"inf",');
            feed(fixed);
        }
        function feed(data){
            ev = JSON.parse(data);
            this.fbpInspector.feedEvent(ev);
        }
    };
};

FBPInspector.prototype.finish = function () {
    if (this.source) {
        this.source.close();
        this.source = null;
    }
};

FBPInspector.prototype.logSentClear = function () {};

FBPInspector.prototype.logDeliveredClear = function () {};

FBPInspector.prototype.feedEvent = function (data) {

    if (data.event == "open") {

        if(data.payload.id.indexOf(".fbp") > -1) return;

        var widget = new Widget(data.payload, widgets.length);
        widgets.push(widget);

    } else if (data.event == "close") {
        //TODO

    } else if (data.event == "connect") {
        connectPorts(data.payload);

    } else if (data.event == "disconnect") {
        //TODO

    } else if (data.event == "send") {

        if (!isCleared) {
            widgets.forEach(function(item, index) {
                item.clearPorts();
            })
            isCleared = true;
        }

        nodeId = data.payload.node;
        widgets.forEach(function(widget, index) {
            if (widget.getUID() === nodeId) {

                widget.update("send",data.payload);
            }
        });

    } else if (data.event == "deliver") { //RECEIVED

        nodeId = data.payload.node;
        widgets.forEach(function(widget, index){
            if (widget.getUID() === nodeId) {
                widget.update("deliver",data.payload);
            }
        });
    }


    function compareNames(a,b) {
        return a === b;
    }

    var dest;
    var src;
    function connectPorts(data) {

        dest = {
            node: data.dst.node,
            idx: data.dst.port_idx,
            port: null
        }
        src = {
            node: data.src.node,
            idx: data.src.port_idx,
            port: null
        }
        //getting the source port
        widgets.forEach(function(widget, index) {

            if (widget.getUID() === src.node) {
                src.port = widget.getOutByIndex(src.idx);
            } else if (widget.getUID() === dest.node) {
                dest.port = widget.getInByIndex(dest.idx)
            };

        });

        if (dest.port !== null) {
            dest.port.connect(src.port);
            dest.port.getElement().on("port-over", onPortMouseEvent);
            dest.port.getElement().on("port-out", onPortMouseEvent);
        }


        if (src.port !== null) {
            src.port.connect(dest.port);
            src.port.getElement().on("port-over", onPortMouseEvent);
            src.port.getElement().on("port-out", onPortMouseEvent);
        }
    }
    function onPortMouseEvent(event){
        if (event.type == "port-over") {
            event["connections"].forEach(function(item, index) {
                $("#"+item).addClass("paired-slave");
            });
            $("#"+event["port"].key).addClass("paired-master");
        } else {
            event["connections"].forEach(function(item, index) {
                $("#"+item).removeClass("paired-slave");
            });
            $("#"+event["port"].key).removeClass("paired-master");
        }
    }
};

window.onload = function () {

    window.modal = new Modal();

    window.fbpInspector = new FBPInspector();
    fbpInspector.start();

};
