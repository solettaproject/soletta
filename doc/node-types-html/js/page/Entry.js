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

        /* adding the inputs */
        var i = 0;
        var list = $('<ul></ul>');
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
