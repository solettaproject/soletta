/* This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

(function(window){
    /* constructor */
    function Category(_data){
        /* creating the label */
        var label = $('<div></div>');
        label.attr('id',"label_" + _data['id']);
        label.addClass('category-label');
        label.text(String(_data['categoryLabel']).toUpperCase());
        $('#contents').append(label);

        /* creating the menu item */
        if(_data['menuLabel'] != "")
        {
            var li = $("<li></li>");
            var a = $("<a></a>");

            a.attr("href","#"+_data["id"]);
            a.attr("id","menu_" + _data["id"]);
            a.text(_data["menuLabel"]);

            $(li).append(a);
            $("#navigation").append(li);
        }

        /* returning the created JQuery object */
        this.el = $('<div></div>');
        this.el.attr("id",_data["id"]);
        this.el.addClass('category');
        $('#contents').append(this.el);
    }

    /* returns the created JQuery element */
    Category.prototype.getContents = function(){
        return this.el;
    }

    /* making the class visible for the window */
    window.Category = Category; /* making it public */

}(window));
