//using a self executing anonymous function
//to enable the fake construction method
(function(window){
    //title         = the name of the node
    //description   = a small description of the node
    //arrInput      = an array containing the name, data type of the in port and its description, format is: ["port","port desription"],...
    //arrOutput     = an array containing the name, data type of the out port and it description,
    //arrOption     = an array containing the name, data type of the options and it description,
    function Entry(title, description, arrInput, arrOutput, arrOption)
    {
        //receiving default values
        this.title = title;
        this.description = description;
        this.arrInput = arrInput;
        this.arrOutput = arrOutput;
        this.arrOption = arrOption;
    }
    Entry.prototype.add = function(group)
    {
        var i;

        //BUILDING THE VISUAL SHELL OF THE ELEMENT
        //BUILDING THE VISUAL SHELL OF THE ELEMENT
        var el = document.createElement("div");
        el.setAttribute("class","Entry");
        el.setAttribute("id" ,"entry" + Entry.COUNT);
        group.appendChild(el);

        //THE HEADER ELEMENT
        var elHeader = document.createElement("div");
        elHeader.setAttribute("class","EntryHeader");
        el.appendChild(elHeader);

        //ADDING THE TITLE TO THE HEADER
        var elTitle = document.createElement("h2");
        elTitle.innerHTML = this.title;
        elHeader.appendChild(elTitle);

        //ADDING THE DESCRIPTION TO THE TITLE
        var elDescription = document.createElement("p");
        elDescription.setAttribute("class","NodeDescription");
        elDescription.innerHTML = "Description: " + "<b>"+this.description + "</b>";
        elHeader.appendChild(elDescription);

        if (this.arrInput.length > 0)
        {
            //INPUT PORTS
            var elPortLabel = document.createElement("h3");
            elPortLabel.innerHTML = "INPUT PORTS";
            el.appendChild(elPortLabel);

            //ADDING THE UL LIST FOR THE INPUT PORTS
            var ul = document.createElement("ul"); el.appendChild(ul);
            ul.setAttribute("class","ULInput");
            var li;

            for(i = 0; i<this.arrInput.length; i++)
            {
                li = document.createElement("li");
                li.innerHTML = "<b>" + this.arrInput[i][0] + "</b> " + this.arrInput[i][1] + "<br>" + this.arrInput[i][2];
                ul.appendChild(li);
            }

            //ADDING A HORIZONTAL LINE TO SEPARATE INPUTS AND OUTPUTS
            el.innerHTML += "<hr noshade color='#cfd0d1'>"; 
        }

        if (this.arrOutput.length > 0)
        {
            //CREATING THE OUTPUT PORTS
            elPortLabel = document.createElement("h3");
            elPortLabel.innerHTML = "OUTPUT PORTS";
            el.appendChild(elPortLabel);

            ul = document.createElement("ul"); el.appendChild(ul);
            ul.setAttribute("class","ULOutput");

            //reading the data attribute
            for(i = 0; i<this.arrOutput.length; i++)
            {
                li = document.createElement("li");
                li.innerHTML = "<b>" + this.arrOutput[i][0] + "</b> " + this.arrOutput[i][1] + "<br>" + this.arrOutput[i][2];
                ul.appendChild(li);
            }

            //ADDING A HORIZONTAL LINE TO SEPARATE OUTPUTS AND OPTIONS
            el.innerHTML += "<hr noshade color='#cfd0d1'>";
        }

        if (this.arrOption.length > 0)
        {
            //CREATING THE OPTIONS
            elPortLabel = document.createElement("h3");
            elPortLabel.innerHTML = "OPTIONS";
            el.appendChild(elPortLabel);

            ul = document.createElement("ul"); el.appendChild(ul);
            ul.setAttribute("class","ULOption");

            //reading the data attribute
            for(i = 0; i<this.arrOption.length; i++)
            {
                li = document.createElement("li");
                li.innerHTML = "<b>" + this.arrOption[i][0] + "</b> " + this.arrOption[i][1] + "<br>" + this.arrOption[i][2];
                if (this.arrOption[i][3] != "")
                    li.innerHTML = li.innerHTML + "<br>Default: " + this.arrOption[i][3];
                ul.appendChild(li);
            }
        }

        Entry.COUNT++;
    };

    window.Entry = Entry; //making it public

    //STATICS
    Entry.COUNT = 0;

}(window));
