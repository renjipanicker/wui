#include <iostream>
#include "wui.hpp"
#include "html.hpp"

int main(int argc, const char* argv[]){
    s::wui::application app(argc, argv, "WUI Demo");
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

    // set JS objects when page loads
    w.onLoad = [&w](const std::string& url) {
        std::cout << "w::onLoad:" << url << std::endl;

        // add console object with console.log function
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
