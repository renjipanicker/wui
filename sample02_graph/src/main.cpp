#include <iostream>
#include "wui.hpp"
#include "html.hpp"

struct node {
    int x;
    int y;
    bool fixed;
    void testf(const int& txt, std::vector<std::string> il){
        std::cout << "calling testf:" << txt << ", len(il):" << il.size() << std::endl;
        for(auto& x : il){
            std::cout << "-:" << x << std::endl;
        }
    }
};

//void refresh(const std::string& txt);

auto jnode = s::js::klass<node>("NodeT")
            .property("x", &node::x)
            .property("y", &node::y)
            .property("fixed", &node::fixed)
            .method("testf", &node::testf)
            .end()
            ;

////
int main(int argc, const char* argv[]){
    s::wui::application app(argc, argv, "Graph");
    s::wui::window w;

    // open window
    app.onInit = [&w](){
        std::cout << "app::onInit" << std::endl;
        w.setContentSourceEmbedded(html);
        std::cout << "Opening window" << std::endl;
        if (!w.open(-500, 0, -1, -1)) {
            std::cout << "unable to open window" << std::endl;
            return;
        }
        w.setDefaultMenu();
    };

    // load index page onOpen
    w.onOpen = [&w]() {
        std::cout << "w::onOpen" << std::endl;
        w.go("html/index.html");
    };

    // exit app on close
    w.onClose = []() {
        std::cout << "w::onClose" << std::endl;
        s::wui::app().exit(0);
    };

    node tnode1;

    // set JS objects when page loads
    w.onLoad = [&w, &tnode1](const std::string& url) {
        std::cout << "w::onLoad:" << url << std::endl;

        // add node class
        w.addClass(jnode);
        //w.setObject(jnode, "tnode1", tnode1);

        // add console object with console.log() function
        auto& console = w.newObject("console");
        console.fn("log") = [](const std::string& text) {
            std::cout << "w::log:" << text << std::endl;
            return 1;
        };
        w.addObject(console);
    };

    std::cout << "Starting loop" << std::endl;
    return app.loop();
}

/*TODO
outgoing functions
implement containers
*/
