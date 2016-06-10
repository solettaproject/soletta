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
