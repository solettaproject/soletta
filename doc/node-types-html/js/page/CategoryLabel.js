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
    //function CategoryLabel(_id, _label, _anchor)
    function CategoryLabel(_data) {
        //creating the category object
        var el = $("<div></div>");
        el.attr("class","category-label");
        el.attr("id",_data["id"]);
        el.text(_data["label"]);

        //appending to the content div
        _data["parentCategory"].append(el);
    }
    window.CategoryLabel = CategoryLabel;
}(window));
