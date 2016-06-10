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
    function Entry(_data)
    {
        /* getting values from arguments */
        this.id = _data["id"];

        var name        = _data['name'];
        var description = _data['description'];
        var exampleList = _data['exampleList'];
        var exampleId   = _data['exampleId'];

        /* creating the layout objects */
        var el = $("<div></div>");
        el.addClass('entry');
        el.attr('id',_data['id']);
        $("#"+_data['categoryId']).append(el);

        /* creating inner elements */
        var header = $("<div></div>");
        header.addClass("entry-header");
        header.append('<span class="entry-name">'+name+'</span><br>');
        header.append('<span class="entry-description">'+description+'</span>');

        /* appending elements */
        el.append(header);
        el.on('click',onEntryMouseEvent);
        el.on('mouseover',onEntryMouseEvent);

        /* adding the aliases */
        var i = 0;
        var list = $('<ul></ul>');
        el.append('<div class="entry-separator">ALIASES</div>');
        el.append(list);
        for(i=0; i<_data['aliases'].length; i++){
            list.append(
                '<li>'+_data['aliases'][i]['name']+'</li>'
            );
        }

        /* adding the inputs */
        list = $('<ul></ul>');
        el.append('<div class="entry-separator">INPUT PORTS</div>');
        el.append(list);
        for(i=0; i<_data['inputs'].length; i++){
            list.append(
                '<li>'+
                    '<b>'+_data['inputs'][i]['name']+' | '+
                    _data['inputs'][i]['dataType']+'<br>'+'</b>'+
                    '<span>'+
                        _data['inputs'][i]['description']+
                    '</span>'+
                '</li>'
            );
        }

        /* adding the ouputs */
        list = $('<ul></ul>');
        el.append('<div class="entry-separator">OUTPUT PORTS</div>');
        el.append(list);
        for(i=0; i<_data['outputs'].length; i++){
            list.append(
                '<li>'+
                    '<b>'+_data['outputs'][i]['name']+' | '+
                    _data['outputs'][i]['dataType']+'<br>'+'</b>'+
                    '<span>'+
                        _data['outputs'][i]['description']+
                    '</span>'+
                '</li>'
            );
        }

        /* adding the options */
        list = $('<ul></ul>');
        el.append('<div class="entry-separator">OPTIONS</div>');
        el.append(list);

        for(i=0; i<_data['options'].length; i++){
            list.append(
                '<li>'+
                    '<b>'+_data['options'][i]['name']+' | '+
                    _data['options'][i]['dataType']+'<br>'+'</b>'+
                    '<span>'+
                        _data['options'][i]['description']+
                    '</span>'+
                '</li>'
            );
        }

        /* internal event handlers */
        function onEntryMouseEvent(event){
            event.preventDefault;
            if(event.type == 'mouseover'){

                /* sends the basic node information to the root
                   to be diplayed at the hover card */
                el.trigger('entry:hover',{name:name, description:description});
            } else if(event.type=='click'){

                /* select this card and send a signal to the root
                   to display the samples view with the correct information */
                el.trigger('entry:select',{name:name, description:description, exampleList:exampleList, exampleId:exampleId});
            }
        }
        Entry.prototype.getElement = function(){
            return el;
        }
    }

    window.Entry = Entry;

}(window));
