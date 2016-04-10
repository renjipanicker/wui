#include <iostream>
#include "wui.hpp"
#include "html.hpp"

int main(int argc, const char* argv[]){
    s::application app(argc, argv, "WUI Demo");
    s::wui::window w;

    // open window
    app.onInit = [&w](){
        std::cout << "app::onInit" << std::endl;
        w.setContentSourceEmbedded(html);
        std::cout << "Opening window" << std::endl;
        if (!w.open()) {
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
        s::app().exit(0);
    };

    // set JS objects when page loads
    w.onLoad = [&w](const std::string& url) {
        std::cout << "w::onLoad:" << url << std::endl;

        // add console object with console.log function
        auto& console = w.addObject("console");
        console.fn("log") = [](const std::string& text) {
            std::cout << "w::log:" << text << std::endl;
            return 1;
        };

        // add napp object with napp.send function
        auto& napp = w.addObject("napp");
        napp.fn("send") = [](const std::string& text) {
            std::cout << "w::napp::send:" << text << std::endl;
        };
    };

    std::cout << "Starting loop" << std::endl;
    return app.loop();
}
