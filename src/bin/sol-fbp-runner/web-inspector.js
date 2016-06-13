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

var FBPInspector = function () {
    this.html = {
        nodes: this.el("fbp-inspector-nodes"),
        logSent: this.el("fbp-log-sent"),
        logDelivered: this.el("fbp-log-delivered"),
        start: this.el("fbp-start"),
        finish: this.el("fbp-finish"),
    };

    if (!window.EventSource) {
        alert("Your browser does not support server-side-events (SSE): no EventSource().");
        this.html.start.disabled = true;
        this.html.finish.disabled = true;
        return;
    }

    this.clearTableRows(this.html.nodes);
    this.html.start.disabled = false;
    this.html.finish.disabled = true;
};

FBPInspector.prototype.el = function (id) {
    return document.getElementById(id);
};

FBPInspector.prototype.clearTableRows = function (domElem) {
    while (domElem.rows.length)
        domElem.deleteRow(0);
};

FBPInspector.prototype.addTableRow = function (domElem, idx, rowSpec) {
    var r = domElem.insertRow(idx);
    for (var i = 0; i < rowSpec.length; i++) {
        var c = r.insertCell(i);
        var rs = rowSpec[i];
        if (rs.content)
            c.textContent = rs.content;
        if (rs.html)
            c.innerHTML = rs.html;
        c.className = rs.className;
    };
};

FBPInspector.prototype.start = function () {
    this.nodes = {};
    this.events = [];

    this.source = new EventSource(window.location.origin + '/events');
    this.source.fbpInspector = this;
    this.source.onopen = function (e) {
        this.fbpInspector.clearTableRows(this.fbpInspector.html.nodes);
        this.fbpInspector.logDeliveredClear();
        this.fbpInspector.logSentClear();
        this.fbpInspector.html.start.disabled = true;
        this.fbpInspector.html.finish.disabled = false;
    };

    this.source.onerror = function (e) {
        this.fbpInspector.finish();
    };

    this.source.onmessage = function (e) {
        var ev = JSON.parse(e.data);
        this.fbpInspector.feedEvent(ev);
    };
};

FBPInspector.prototype.logSentClear = function () {
    this.clearTableRows(this.html.logSent);
};

FBPInspector.prototype.logDeliveredClear = function () {
    this.clearTableRows(this.html.logDelivered);
};

FBPInspector.prototype.getNodeName = function (node) {
    if (this.nodes[node])
        return this.nodes[node].creation.payload.id || node;
    return node;
};

FBPInspector.prototype.renderPacket = function (packet) {
    var packet_type = packet.packet_type;
    var payload = packet.payload;
    var extraClass = "";
    var body = "";

    if (packet_type == "empty" ||
        packet_type == "any") {
    } else if (packet_type == "boolean" ||
               packet_type == "byte" ||
               packet_type == "timestamp") {
        body = payload;
    } else if (packet_type == "string") {
        body = payload; // TODO: escape
    } else if (payload instanceof Array) {
        extraClass += " fbp-packet-composed";
        for (var i = 0; i < payload.length; i++) {
            if (i > 0)
                body += '<span class="fbp-packet-composed-separator">, </span>';
            body += this.renderPacket(payload[i]);
        }
    } else {
        var keys = [];
        for (var key in payload) {
            if (!payload.hasOwnProperty(key))
                continue;
            keys.push(key);
        }

        keys.sort();

        for (var i = 0; i < keys.length; i++) {
            var key = keys[i];
            var value = payload[key];
            if (i > 0)
                body += '<span class="fbp-packet-field-separator">, </span>';
            body += '<span class="fbp-packet-field">' +
                '<span class="fbp-packet-field-key">' + key + ': </span>' + // TODO: escape
                '<span class="fbp-packet-field-value">' + value + '</span>' + // TODO: escape
                '</span>';
        }

        if (packet_type == "rgb" && payload) {
            var r = Math.round(255 * (payload.red / payload.red_max));
            var g = Math.round(255 * (payload.green / payload.green_max));
            var b = Math.round(255 * (payload.blue / payload.blue_max));

            body += '<span class="fbp-packet-type-rgb-preview" style="background-color: rgb(' + r + ',' + g + ',' + b + ');"></span>';
        }
    }

    return '<span class="fbp-packet fbp-packet-type-' + packet_type + ' ' + extraClass + '">' + body + '</span>';
};

FBPInspector.prototype.renderOptionValue = function (data_type, value) {
    var extraClass = "";
    var body = "";

    if (data_type == "boolean" ||
               data_type == "byte" ||
               data_type == "timestamp") {
        body = value;
    } else if (data_type == "string") {
        body = value; // TODO: escape
    } else {
        var keys = [];
        for (var key in value) {
            if (!value.hasOwnProperty(key))
                continue;
            keys.push(key);
        }

        keys.sort();

        for (var i = 0; i < keys.length; i++) {
            var key = keys[i];
            var val = value[key];
            if (i > 0)
                body += '<span class="fbp-node-option-field-separator">, </span>';
            body += '<span class="fbp-node-option-field">' +
                '<span class="fbp-node-option-field-key">' + key + ': </span>' + // TODO: escape
                '<span class="fbp-node-option-field-value">' + val + '</span>' + // TODO: escape
                '</span>';
        }

        if (data_type == "rgb" && value) {
            var r = Math.round(255 * (value.red / value.red_max));
            var g = Math.round(255 * (value.green / value.green_max));
            var b = Math.round(255 * (value.blue / value.blue_max));

            body += '<span class="fbp-node-option-type-rgb-preview" style="background-color: rgb(' + r + ',' + g + ',' + b + ');"></span>';
        }
    }

    return '<span class="fbp-node-option fbp-node-option-type-' + data_type + ' ' + extraClass + '">' + body + '</span>';
}

FBPInspector.prototype.renderNodeInfo = function (node) {
    var n = this.nodes[node];
    var payload = n.creation.payload;
    var creationTimestamp = n.creation.timestamp;
    var destructionTimestamp = n.destruction;

    // TODO: hadle escaping of html

    var html = '<h3>' + this.getNodeName(node) + '</h3>' +
        '<dl id="fbp-node-info-' + node + '">';

    if (payload.type) {
        html += '<dt class="fbp-node-type">type</dt>' +
            '<dd class="fbp-node-type">' + payload.type + '</dd>';
    }

    if (payload.category) {
        html += '<dt class="fbp-node-category">category</dt>' +
            '<dd class="fbp-node-category">' + payload.category + '</dd>';
    }

    if (payload.description) {
        html += '<dt class="fbp-node-description">description</dt>' +
            '<dd class="fbp-node-description">' + payload.description + '</dd>';
    }

    if (payload.author) {
        html += '<dt class="fbp-node-author">author</dt>' +
            '<dd class="fbp-node-author">' + payload.author + '</dd>';
    }

    if (payload.url) {
        html += '<dt class="fbp-node-url">url</dt>' +
            '<dd class="fbp-node-url"><a href="' + payload.url + '">' + payload.url + '</a></dd>';
    }

    if (payload.license) {
        html += '<dt class="fbp-node-license">license</dt>' +
            '<dd class="fbp-node-license">' + payload.license + '</dd>';
    }

    if (payload.version) {
        html += '<dt class="fbp-node-version">version</dt>' +
            '<dd class="fbp-node-version">' + payload.version + '</dd>';
    }

    if (payload.options) {
        html += '<dt class="fbp-node-options">options</dt>' +
            '<dd class="fbp-node-options"><table>' +
            '<thead>' +
            '<th class="fbp-node-options-name">Name</th>' +
            '<th class="fbp-node-options-value">Value</th>' +
            '<th class="fbp-node-options-info">Info</th>' +
            '</thead><tbody>';

        for (var i = 0; i < payload.options.length; i++) {
            var o = payload.options[i];
            // TODO: hide fbp-node-options-info by default, dettach it and make a floating window on mouse-over row
            html += '<tr class="fbp-node-options-required-' + o.required + '">' +
                '<td class="fbp-node-options-name">' + o.name + '</td>' +
                '<td class="fbp-node-options-value fbp-node-options-data_type-' + o.data_type + '">' + this.renderOptionValue(o.data_type, o.value) + '</td>' +
                '<td class="fbp-node-options-info">' +
                '<div id="fbp-node-options-info-' + node + '-' + i + '" class="fbp-node-options-info"><dl>' +
                '<dt>name</dt><dd>'+ o.name + '</dd>' +
                '<dt>required</dt><dd>'+ o.required + '</dd>' +
                '<dt>type</dt><dd>' + o.data_type + '</dd>' +
                '<dt>default value</dt><dd>' + this.renderOptionValue(o.data_type, o.defvalue) + '</dd>' +
                '<dt>description</dt><dd>' + o.description + '</dd>' +
                '</dl></div></td>' +
                '</tr>';
        }

        html += '<tbody></table></dd>';
    }

    html += '<dt class="fbp-node-timestamp fbp-node-creation">creation</dt>' +
        '<dd class="fbp-node-timestamp fbp-node-creation">' + creationTimestamp + '</dd>';

    if (destructionTimestamp) {
        html += '<dt class="fbp-node-timestamp fbp-node-destruction">destruction</dt>' +
            '<dd class="fbp-node-timestamp fbp-node-destruction">' + destructionTimestamp + '</dd>';
    }

    return html;
};

FBPInspector.prototype.renderNodePorts = function (node, direction, descriptions) {
    var n = this.nodes[node];
    var html = "";

    // TODO: hadle escaping of html

    for (var i = 0; i < descriptions.length; i++) {
        var pdesc = descriptions[i];
        var uid_prefix = direction + '-' + node + '-';
        var uid = uid_prefix + pdesc.base_port_idx;

        html += '<div id="fbp-node-port-container-' + uid  + '" class="fbp-node-port-container-' + direction + ' fbp-node-port-data_type-' + direction + '-' + pdesc.data_type + '">';

        html += '<div id="fbp-node-port-names-' + uid  + '" class="fbp-node-port-names-' + direction + '">';

        if (pdesc.array_size == 0) {
            var port_uid = uid;
            html += '<div id="fbp-node-port-entry-' + port_uid +
                '" class="fbp-node-port-entry-' + direction +
                '"><div id="fbp-node-port-name-' + port_uid +
                '" class="fbp-node-port-name-' + direction +
                '">' + pdesc.name + '</div>' +
                '<div id="fbp-node-port-value-' + port_uid +
                '" class="fbp-node-port-value-' + direction +
                '"></div></div>';
        } else {
            for (var idx = 0; idx < pdesc.array_size; idx++) {
                var port_uid = uid_prefix + pdesc.base_port_idx + idx;
                html += '<div id="fbp-node-port-entry-' + port_uid +
                    '" class="fbp-node-port-entry-' + direction + ' fbp-node-port-entry-' + direction + '-array' +
                    '"><div id="fbp-node-port-name-' + port_uid +
                    '" class="fbp-node-port-name-' + direction + ' fbp-node-port-name-' + direction + '-array' +
                    '">' + pdesc.name + '[' + idx + ']</div>' +
                    '<div id="fbp-node-port-value-' + port_uid +
                    '" class="fbp-node-port-value-' + direction + ' fbp-node-port-value-' + direction + '-array' +
                    '"></div></div>';
            }
        }

        // TODO: hide fbp-node-port-info by default, dettach it and make a floating window on mouse-over row
        html += '</div><div id="fbp-node-port-info-' + uid +
            '" class="fbp-node-port-info-' + direction +
            '"><dl>';

        var infoName = pdesc.name;
        if (pdesc.array_size) {
            infoName += '[' + pdesc.array_size + ']';
        }

        html += '<dt>name</dt><dd>' + infoName + '</dd>';
        html += '<dt>required</dt><dd>' + pdesc.required + '</dd>';
        html += '<dt>type</dt><dd>' + pdesc.data_type + '</dd>';
        html += '<dt>description</dt><dd>' + pdesc.description + '</dd>';

        html += '</div></dl></div>';
    }

    return html;
};

FBPInspector.prototype.feedEvent = function (ev) {
    this.events.push(ev);

    var payload = ev.payload;

    if (ev.event == "open") {
        var path = payload.path;
        var node = path[path.length - 1];
        this.nodes[node] = {
            creation: ev,
            destruction: null,
            inputConnections: [],
            outputConnections: [],
        };

        var row = this.html.nodes.insertRow(this.html.nodes.rows.length - 1);
        row.id = "fbp-node-" + node;
        row.className = "fbp-node";

        var cell;

        cell = row.insertCell(0);
        cell.id = "fbp-node-ports-in-" + node;
        cell.className = "fbp-node-ports-in";
        cell.innerHTML = this.renderNodePorts(node, 'in', ev.payload.ports_in);

        cell = row.insertCell(1);
        cell.id = "fbp-node-info-" + node;
        cell.className = "fbp-node-info";
        cell.innerHTML = this.renderNodeInfo(node);

        cell = row.insertCell(2);
        cell.id = "fbp-node-ports-out-" + node;
        cell.className = "fbp-node-ports-out";
        cell.innerHTML = this.renderNodePorts(node, 'out', ev.payload.ports_out);

    } else if (ev.event == "close") {
        var node = payload;

        this.nodes[node].destruction = ev.timestamp;

        this.el("fbp-node-info-" + node).innerHTML += '<dt class="fbp-node-timestamp fbp-node-creation">destruction</dt>' +
            '<dd class="fbp-node-timestamp fbp-node-destruction">' + ev.timestamp + '</dd>';
        this.el("fbp-node-" + node).className += "fbp-node-closed";

    } else if (ev.event == "connect") {
        var srcConn = payload.src;
        var dstConn = payload.dst;

        var srcNode = this.nodes[srcConn.node];
        var dstNode = this.nodes[dstConn.node];

        srcNode.outputConnections.push(ev);
        dstNode.inputConnections.push(ev);

        // TODO: show

    } else if (ev.event == "disconnect") {
        var srcConn = payload.src;
        var dstConn = payload.dst;

        var srcNode = this.nodes[srcConn.node];
        var dstNode = this.nodes[dstConn.node];

        var i;

        for (i = 0; i < srcNode.outputConnections.length; i++) {
            var c = srcNode.outputConnections[i];
            if (c.payload.src.conn_id == srcConn.conn_id)
                srcNode.outputConnections.splice(i, 1);
        }
        for (i = 0; i < dstNode.inputConnections.length; i++) {
            var c = dstNode.inputConnections[i];
            if (c.payload.dst.conn_id == dstConn.conn_id)
                dstNode.inputConnections.splice(i, 1);
        }

        // TODO: show

    } else if (ev.event == "send") {
        var packetHtml = this.renderPacket(payload.packet);

        this.addTableRow(this.html.logSent, -1,
            [{content: ev.timestamp, className: "fbp-log-timestamp"},
             {content: this.getNodeName(payload.node), className: "fbp-log-node"},
             {content: payload.port_name || payload.port_idx, className: "fbp-log-port"},
             {html: packetHtml, className: "fbp-log-packet"}]);

        var valEl = this.el("fbp-node-port-value-out-" + payload.node + "-" + payload.port_idx);
        if (valEl) {
            valEl.innerHTML = packetHtml;
            // TODO: plot graph over timestamp
        }

    } else if (ev.event == "deliver") {
        var packetHtml = this.renderPacket(payload.packet);

        this.addTableRow(this.html.logDelivered, -1,
            [{content: ev.timestamp, className: "fbp-log-timestamp"},
             {content: this.getNodeName(payload.node), className: "fbp-log-node"},
             {content: payload.port_name || payload.port_idx, className: "fbp-log-port"},
             {html: packetHtml, className: "fbp-log-packet"}]);

        var valEl = this.el("fbp-node-port-value-in-" + payload.node + "-" + payload.port_idx);
        if (valEl) {
            valEl.innerHTML = packetHtml;
            // TODO: plot graph over timestamp
        }
    }
};

FBPInspector.prototype.finish = function () {
    if (this.source) {
        this.source.close();
        this.source = null;
    }

    this.html.start.disabled = false;
    this.html.finish.disabled = true;
};

window.onload = function () {
    window.fbpInspector = new FBPInspector();
};
