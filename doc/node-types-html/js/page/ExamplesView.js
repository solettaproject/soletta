// This file is part of the Soletta Project
//
// Copyright (C) 2015 Intel Corporation. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in
//     the documentation and/or other materials provided with the
//     distribution.
//   * Neither the name of Intel Corporation nor the names of its
//     contributors may be used to endorse or promote products derived
//     from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

(function(window){
    function ExamplesView(){
        //getting the DOM element
        var el = $('#example');

        //setting up the back button
        $('.button-back').on('click',onBackClick);
        function onBackClick(event){
            close();
        }

        //handling the window resize
        //--------------------------
        function onWindowResize(event){
            adjustExampleView();
        }

        //adjusting the view when the window is resized
        //---------------------------------------------
        function adjustExampleView(){
            var exampleWidth = $('#example').width() - 291-25;
            var exampleHeight = $('#example').height()-91-32;

            $('#example-image-container').css('width',exampleWidth);
            $('#example-image-container').css('height',exampleHeight);
        }

        //load the contents of the selected entry
        //---------------------------------------
        function loadExampleContents(_data){

            //loading title and description
            var info = $('#example-info');
            var title = $('<h1></h1>');
            var description = $('<p></p>');

            title.text(_data['name']);
            description.text(_data['description']);

            info.append(title);
            info.append(description);

            //loading all thumbnails
            for(var i=0; i<_data['exampleList'].length; i++){

                var thumb = $('<img></img>');
                thumb.addClass('example-thumb');

                //thumbnail main attributes
                thumb.attr('src',_data['exampleList'][i]['thumb']);
                thumb.attr('id','thumb_' + i);

                //thumbnail data
                thumb.data('large',_data['exampleList'][i]['large']);
                thumb.data('code',_data['exampleList'][i]['code']);

                //thumbnail events
                thumb.on('click',onThumbExampleClick);
                info.append(thumb);

                if(i==0){
                    //load the first example
                    loadExampleImage(thumb);
                }
            }

            //adding the get-code button
            var getCodeButton = $('<img></img>');
            getCodeButton.attr('src','images/button_get_code.png');
            getCodeButton.attr('id','example-get-code');
            getCodeButton.on('click',copyExampleCode);

            info.append(getCodeButton);
        }

        //handles the click on a thumbnail
        //--------------------------------
        function onThumbExampleClick(event){
            loadExampleImage($(this));
        }

        //loads the correspondent image from the thumbnail
        //------------------------------------------------
        function loadExampleImage(_thumb)
        {
            //removing old image reference
            $('#example-image-container').empty();
            $("img[id^=thumb_]").each(function(){
                $(this).removeClass('example-thumb-active');
            });

            //loading selected image
            var imageURL = $(_thumb).data('large'); //'http://ab01.bz.intel.com/~lpereir/fbp-to-svg/foosball.svg'

           //SVG Loading happens here
            var image = $("<img></img");
            image.attr("src",imageURL);
            image.attr('id','example-image');
            image.data('code',$(_thumb).data('code'));

            //adding the loaded image into the drag view
            $("#example-image-container").append(image);

            //marking the active thumb
            $(_thumb).addClass('example-thumb-active');

            //making the image draggable
            image.draggable();

            //removing existing copy feedback and restoring the copy button
            if($('#example-info').has('#example-code-copied')){
                $('#example-code-copied').remove();
                $('#example-get-code').show();
            }
        }

        //--------------------------------------------------------
        //COPY EXAMPLE CODE
        //Implement the algorith to read the example
        //code here. The var code already stores any key, url or
        //string you sent when creating the Entry in the main loop
        //--------------------------------------------------------

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

        //clears all data and loaded images
        //closes the view and restore the main content
        //--------------------------------------------
        function close(){
            el.fadeOut(250,onCloseComplete);
        }
        function onCloseComplete(){
            //clearing all created objects
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

            //showing the example view
            //$("#contents").hide();
            $("#example").fadeIn();

            adjustExampleView();
            loadExampleContents(_data);
            $(window).on('resize',onWindowResize);
        }
    }

    window.ExamplesView = ExamplesView;

}(window));
