/*
 * This file is part of the Soletta Project
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
    function ExamplesView(){
        /* getting the DOM element */
        var el = $('#example');

        /* setting up the back button */
        $('.button-back').on('click',onBackClick);
        function onBackClick(event){
            close();
        }

        /* handling the window resize */
        function onWindowResize(event){
            adjustExampleView();
        }

        /* adjusting the view when the window is resized */
        function adjustExampleView(){
            var exampleWidth = $('#example').width() - 291-25;
            var exampleHeight = $('#example').height()-91-32;

            $('#example-image-container').css('width',exampleWidth);
            $('#example-image-container').css('height',exampleHeight);
        }

        /* load the contents of the selected entry */
        function loadExampleContents(_data){

            /* loading title and description */
            var info = $('#example-info');
            var title = $('<h1></h1>');
            var description = $('<p></p>');

            title.text(_data['name']);
            description.text(_data['description']);

            info.append(title);
            info.append(description);

            /* loading all thumbnails */
            for(var i=0; i<_data['exampleList'].length; i++){

                var thumb = $('<img></img>');
                thumb.addClass('example-thumb');

                /* thumbnail main attributes */
                thumb.attr('src',_data['exampleList'][i]['thumb']);
                thumb.attr('id','thumb_' + i);

                /* thumbnail data */
                thumb.data('large',_data['exampleList'][i]['large']);
                thumb.data('code',_data['exampleList'][i]['code']);

                /* thumbnail events */
                thumb.on('click',onThumbExampleClick);
                info.append(thumb);

                if(i==0){
                    /* load the first example */
                    loadExampleImage(thumb);
                }
            }

            /* adding the get-code button */
            var getCodeButton = $('<img></img>');
            getCodeButton.attr('src','images/button_get_code.png');
            getCodeButton.attr('id','example-get-code');
            getCodeButton.on('click',copyExampleCode);

            info.append(getCodeButton);
        }

        /* handles the click on a thumbnail */
        function onThumbExampleClick(event){
            loadExampleImage($(this));
        }

        /* loads the correspondent image from the thumbnail */
        function loadExampleImage(_thumb)
        {
            /* removing old image reference */
            $('#example-image-container').empty();
            $("img[id^=thumb_]").each(function(){
                $(this).removeClass('example-thumb-active');
            });

            /* loading selected image */
            var imageURL = $(_thumb).data('large');

           /* SVG Loading happens here */
            var image = $("<img></img");
            image.attr("src",imageURL);
            image.attr('id','example-image');
            image.data('code',$(_thumb).data('code'));

            /* adding the loaded image into the drag view */
            $("#example-image-container").append(image);

            /* marking the active thumb */
            $(_thumb).addClass('example-thumb-active');

            /* making the image draggable */
            image.draggable();

            /* removing existing copy feedback and restoring the copy button */
            if($('#example-info').has('#example-code-copied')){
                $('#example-code-copied').remove();
                $('#example-get-code').show();
            }
        }

        /* --------------------------------------------------------
         * COPY EXAMPLE CODE
         * Implement the algorith to read the example
         * code here. The var code already stores any key, url or
         * string you sent when creating the Entry in the main loop
         * --------------------------------------------------------
         */

        function copyExampleCode(event){

            var code = $('#example-image').data('code');
            $(this).fadeOut(100,showCopiedFeedback);
        }
        function showCopiedFeedback(){

            var feedback = $('<img></img>');
            feedback.attr('src','images/button_code_copied.png');
            feedback.attr('id','example-code-copied');
            $('#example-info').append(feedback);

            feedback.hide();
            feedback.fadeIn(100);
        }

        /* clears all data and loaded images
         * closes the view and restore the main content */
        function close(){
            el.fadeOut(250,onCloseComplete);
        }
        function onCloseComplete(){
            /* clearing all created objects */
            $(window).off('resize',onWindowResize);
            $("img[id^=thumb_]").each(function(){
                $(this).remove();
            });

            $('#example-image-container').empty();
            $('#example-get-code').remove();
            $('#example h1, p').remove();
            $('#example-code-copied').remove();

            $('#example').hide();
            el.trigger('examples:closed');
        }
        ExamplesView.prototype.show = function(_data){
            /* showing the example view */
            $("#example").fadeIn();

            adjustExampleView();
            loadExampleContents(_data);
            $(window).on('resize',onWindowResize);
        }
    }

    window.ExamplesView = ExamplesView;

}(window));
