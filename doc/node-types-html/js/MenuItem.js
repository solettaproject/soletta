(function(window){
    //label   = the label of the menu item
    //anchor  = the anchor to slide the page to
    function MenuItem(label, anchor)
    {
        //receiving default values
        this.label = label;

        //this attribute will not be the id of the object,
        //instead it will be used to compose the anchor link
        //which is pointing to the Category object created
        //above it on the main loop
        this.anchor = anchor;

        //creating the menu item
        var item = document.createElement("div");
        item.setAttribute("class","MenuItem");
        item.setAttribute("id","menuItem" + MenuItem.COUNT);
        item.innerHTML = "<a href='#" + this.anchor  + "'><p class='MenuItemLabel'>" + label + "</p>" + "</a>";
        document.getElementById("sideBar").appendChild(item);

        MenuItem.COUNT ++; //incrementing to create unique ids
    }

    window.MenuItem = MenuItem; //making it public

    //STATICS
    MenuItem.COUNT = 0;

}(window));
