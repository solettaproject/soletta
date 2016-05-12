(function(window) {

	'use strict'

	function Widget(payload, key) {

		//caching a secure reference to the instance
		var instance = this;
		//var selectedPort;

		Widget.prototype.portsCount = 0;

		//unique id for the widget and other unique properties
		this.raw = JSON.stringify(payload, null, 4);
		this.key = key;
		this.widgetName = payload.id; //change to 'name ?'
		this.uid = payload.path[payload.path.length-1];
		this.type = payload.type;
		this.category = payload.category;
		this.description = payload.description;
		this.url = payload.url;
		this.portsIn = [];
		this.portsOut = [];
		this.autoSelected = false;
		this.hidden = true;
		this.selectedPort = null;
		this.totalPorts = 0;

		//TODO: we should change the id key to name
		this.name = "widget" + payload.id;

		//title and subtitle of the widget
		this.title = $("<span/>").addClass("widget-title").append(this.widgetName);
		this.subtitle = $("<span/>").addClass("widget-subtitle").append(this.type);

		//info button
		var info = $("<img/>").attr("src","images/ico_info.png").addClass("info");
		info.click(function(event) {
			window.modal.show(instance.raw, instance.type);
		});

		//creating DOM elements
		this.displayValue = $("<span/>").addClass("display-value");
		this.el = $("<div/>").addClass("widget").attr("id",this.uid).appendTo("#container");
		$("<div/>").addClass("widget-header").appendTo(this.el).append(this.title).append(this.subtitle).append(info);
		$("<div/>").addClass("widget-display").appendTo(this.el).addClass("widget-data").append(this.displayValue);
		this.widgetPorts = $("<div/>").addClass("widget-ports").appendTo(this.el);

		//putting the ports in the correct container
		this.lports = $("<div/>").addClass("port-container").appendTo("#"+this.uid + " .widget-ports");
		this.rports = $("<div/>").addClass("port-container").appendTo("#"+this.uid + " .widget-ports");

		//reading all in ports
		var port;
		payload.ports_in.forEach(function(element, index) {
			port = new Port(
			{
				name:element["name"],
				value:undefined,
				type:"in",
				port_index:element["base_port_idx"],
				data_type:element["data_type"],
				required:element["required"],
				widget:instance,
				key:instance.key + "_" + Widget.prototype.portsCount
			});

			instance.portsIn.push(port);
			instance.totalPorts++;
			port.el.on("port-select", instance.onPortSelect);
			Widget.prototype.portsCount ++;
		});

		//reading all out ports
		payload.ports_out.forEach(function(element, index) {
			port = new Port(
			{
				name:element["name"],
				value:undefined,
				type:"out",
				port_index:element["base_port_idx"],
				data_type:element["data_type"],
				required:element["required"],
				widget:instance,
				key:instance.key + "_" + Widget.prototype.portsCount
			});
			instance.portsOut.push(port);
			instance.totalPorts++;
			port.el.on("port-select", instance.onPortSelect);
			Widget.prototype.portsCount ++;
		});

		//Auto Adjusting the size of the Widget Container
		var h = Math.max(this.portsIn.length, this.portsOut.length) * 26;
		this.widgetPorts.css("height",h);

	}

	Widget.prototype.onPortSelect = function(event) {
		event.widget.selectPort(event.port, event.value);
	};

	Widget.prototype.getUID = function() {
		return this.uid;
	};

	//UPDATES THE VALUES OF ALL PORTS
	Widget.prototype.update = function(type, data) {

		//getting the child ports of the widget
		var port = (type === "deliver") ? this.portsIn[data.port_idx] : this.portsOut[data.port_idx];

		//if the ports have no pair, skip
		if(port.connections.length < 0) return;

		//found paired ports, update its value
		var packet = data.packet;
		var packetType = packet.packet_type;

		if(packetType === "empty"){
			port.update("empty");
		}else if(packetType === "int" || packetType === "float"){
			port.update(packet.payload.value)
		}else if(packetType === "byte"){
			port.update(packet.payload);
		}else if(packetType === "boolean"){
			port.update(packet.payload.toString());
		}
	};

	Widget.prototype.fadeIn = function(delay) {
		var instance = this;
		setTimeout(function(){
			instance.el.fadeIn(300);
		},delay);
	};

	Widget.prototype.clearPorts = function() {

		this.portsIn.forEach(function(p, i){
			if(p.connections.length === 0){
				p.hidePort();
			}
		})

		this.portsOut.forEach(function(p, i){
			if(p.connections === 0){
				p.hidePort();
			}
		})
	};

	Widget.prototype.autoSelectPort = function(port) {
		if(this.selectedPort == null){
			this.selectPort(port,port.value);
		}
	};

	Widget.prototype.selectPort = function(port, value) {

		//dealing with repeated port click
		if(port === this.selectedPort) {
			port.unselect();
			this.unsubscribe(port);
			this.selectedPort = null;
			return;
		}

		//clearing last selected port
		if(this.selectedPort != null) {
			this.selectedPort.unselect();
			this.unsubscribe(port);
		}

		//selecting new port
		this.clearDisplay().append(value);
		this.selectedPort = port;
		this.selectedPort.select();
		this.subscribe(port);
	};

	Widget.prototype.onPortValueChange = function(event) {
		event.widget.clearDisplay().append(event.value);
	};

	Widget.prototype.subscribe = function(port) {
		port.el.on('port-update', this.onPortValueChange);
	};

	Widget.prototype.unsubscribe = function(port) {
		port.el.off('port-update', this.onPortValueChange);
		this.clearDisplay(port.widget);
	};

	Widget.prototype.clearDisplay = function() {
		var display = $("#" + this.uid + " .display-value").empty();
		return display;
	};

	Widget.prototype.getElement = function() {
		return this.el;
	};

	Widget.prototype.getLeft = function() {
		return this.lports;
	};

	Widget.prototype.getRight = function() {
		return this.rports;
	};

	Widget.prototype.getInByIndex = function(index) {
		return this.portsIn[index];
	};

	Widget.prototype.getOutByIndex = function(index) {
		return this.portsOut[index];
	};

	window.Widget = Widget;

})(window)
