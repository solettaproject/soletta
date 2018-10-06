(function(window) {

	'use strict'

	function Modal(){

		var instance = this;

		this.el = $("<div/>").addClass("modal").appendTo("#modal-container");
		this.header = $("<div/>").addClass("modal-header").appendTo(this.el);
		this.title = $("<span/>").appendTo(this.header).addClass("modal-title");
		this.content = $("<pre/>").appendTo(this.el);

		this.el.hide();
		this.shield = $("<div/>").attr("id","shield");

		this.closeButton = $("<img/>").attr("src","images/ico_close.png").addClass("close").appendTo(this.header)
		.click(function(){
			instance.hide();
		});


	}

	Modal.prototype.show = function(data, title) {

		var instance = this;

		this.title.empty().append(title);
		this.content.empty().append(data);

		//adding click to the shield div
		this.shield.insertBefore(this.el)
		.click(function() {
			instance.hide();
		});

		this.el.show(400);
	};

	Modal.prototype.hide = function() {
		this.title.empty();
		this.content.empty();
		this.el.hide();
		this.shield.remove();
	};

	window.Modal = Modal;

})(window)
