(function(window) {

	'use strict'

	function Port (params) {

		//data properties
		var instance = this; //returns the __proto__ of the object

		this.name = params["name"];
		this.value = params["value"];
		this.type = params["type"] || "in";
		this.data_type = params["data_type"];
		this.widget = params["widget"];
		this.port_index = params["port_index"];
		this.required = params["required"] || true;
		this.key = "port_" + this.type + "_" + params["key"];
		this.connections = []; //the pair port which this port is connected
		this.emitt = false;

		//DOM properties
		this.el = $("<div/>")
		.attr("id",this.key)
		.addClass("port "+this.getPortType())
		.append(this.name);

		//adding the port to the correct container
		if(this.type == "in") this.widget.getLeft().append(this.el);
		if(this.type == "out") this.widget.getRight().append(this.el);

		//MOUSE AND CUSTOM EVENTS
		this.el.click(function (event) {
			$(event.target).trigger({
				type:"port-select",
				port:instance,
				widget:instance.widget,
				value:instance.value
			});

		});

		this.el.mouseover(function (event) {
			$(event.target).trigger({
				type:"port-over",
				port:instance,
				widget:instance.widget,
				connections:instance.connections
			});
		});

		this.el.mouseout(function (event) {
			$(event.target).trigger({
				type:"port-out",
				port:instance,
				widget:instance.widget,
				connections:instance.connections
			});
		});
	}

	Port.prototype.update = function (value) {

		this.value = value; //retrieves only the valu

		var widget = this.widget;
		var instance = this.instance;

		if(this.emitt) this.el.trigger(
		{
			type:"port-update",
			widget:widget,
			port:instance,
			value:value,
		});
	}

	Port.prototype.getElement = function() {
		return this.el;
	}

	Port.prototype.getParentWidget = function() {
		return this.widget;
	}

	Port.prototype.getPortType = function() {
		return (this.type === "in") ? "in" : "out";
	}

	Port.prototype.select = function() {
		this.emitt = true;
		this.el.addClass("selected");
	}

	Port.prototype.unselect = function() {
		this.emitt = false;
		this.el.removeClass("selected");
	}

	Port.prototype.connect = function (port) {

		//ERROR TRYING TO CONNECT PORTs!
		if(this.connections.indexOf(port.key) == -1)
		{
			this.connections.push(port.key);
			this.widget.autoSelectPort(this);
		}

	}

	Port.prototype.hidePort = function() {
		this.el.addClass("hidden");
		this.el.addClass("non-selectable");
		this.el.unbind();
	}

	Port.prototype.showPort = function() {
		//TODO
	}

	window.Port = Port;

})(window)
