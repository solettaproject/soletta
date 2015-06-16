(function(window){

    //name          = The name of the category
    //description   = The description of the category
    //id            = The id of the category, used to function as an anchor to the created category
    function Category(name, description, id)
    {
        //receiving default values
        this.name = name;
        this.id = id;
        this.description = description;

        //creating the visual element
        var el = document.createElement("div");
        el.setAttribute("class","Category");
        el.setAttribute("id",id);
        el.innerHTML = "<h1>" + this.name.toUpperCase() + "</h1>";
        el.innerHTML += "<p class='CategoryDescription'>"+ this.description + "</p>";

        document.getElementById("content").appendChild(el);
    }

    window.Category = Category; //making it public

}(window));
