#include "wui.hpp"

// detect OS
#ifdef _WIN32
#define WUI_WIN
#elif __APPLE__
#include "TargetConditionals.h"
#if TARGET_IPHONE_SIMULATOR
#define WUI_IOS_SIM
#elif TARGET_OS_IPHONE
#define WUI_IOS
#elif TARGET_OS_MAC
#define WUI_OSX
#else
#define WUI_UNKNOWN
#endif
#elif __ANDROID__
#define WUI_NDK
#elif __linux
#define WUI_LINUX
#elif __unix // all unices not caught above
#define WUI_UNIX
#elif __posix
#define WUI_POSIX
#endif

// common includes
#include <iostream>
#include <condition_variable>
#include <queue>
#include <thread>

// NDK-specific includes
#ifdef WUI_NDK
#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#endif

// WIN-specific includes
#ifdef WUI_WIN
#include <windows.h>
#include <shlobj.h>
#include <atlbase.h>
#include <comutil.h>
#include <mshtmhst.h>
#include <mshtmdid.h>
#include <MsHTML.h>
#include <ExDispId.h>

#include <locale>
#include <codecvt>
#endif

// OSX-specific includes
#ifdef WUI_OSX
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>
#endif

namespace {
#ifdef WUI_OSX
    //////////////////////////
    inline std::string getCString(NSString* str) {
        std::string cstr([str UTF8String]);
        return cstr;
    }

    inline NSString* getNSString(const std::string& str) {
        NSString *ustr = [NSString stringWithCString : str.c_str()
                                            encoding : [NSString defaultCStringEncoding]];
        return ustr;
    }
#endif

    const std::string jspfx = "javascript:";
    const std::string empfx = "embedded";
    const std::wstring empfxw = L"embedded";

    struct ContentSourceData {
        s::wui::ContentSourceType type;
        std::string path;
        std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>> const* lst;
        inline ContentSourceData() : type(s::wui::ContentSourceType::Standard), lst(nullptr) {}

        /// \brief set embedded source
        inline void setEmbeddedSource(std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>> const& l) {
            type = s::wui::ContentSourceType::Embedded;
            lst = &l;
        }

        /// \brief set resource source
        inline void setResourceSource(const std::string& p) {
            type = s::wui::ContentSourceType::Resource;
            path = p;
        }

        inline auto isHTTP(const std::string& url) const {
            if (url.substr(0, 4) == "http") {
                return true;
            }
            if (url.substr(0, 5) == "https") {
                return true;
            }
            return false;
        }
        inline auto normaliseUrl(std::string url) {
            if (isHTTP(url)) {
                return url;
            }
            if(type == s::wui::ContentSourceType::Embedded){
                if ((url.length() < empfx.length()) || (url.substr(0, empfx.length()) != empfx)) {
                    url = empfx + "://app.res/" + url;
                }
            }else if(type == s::wui::ContentSourceType::Resource){
#ifdef WUI_WIN
                url = "res://" + s::app().name + "/" + url;
#endif
#ifdef WUI_OSX
                auto rpath = [[NSBundle mainBundle] resourcePath];
                auto upath = getCString(rpath);
                upath += path;
                upath += url;
                url = upath;
#endif
#ifdef WUI_NDK
                auto rpath = path;
                if(rpath.at(0) == '/') {
                    rpath = rpath.substr(1);
                }
                if(rpath.at(rpath.length()-1) == '/') {
                    rpath = rpath.substr(0, rpath.length()-1);
                }
                url = "file:///android_asset/" + rpath + "/" + url;
#endif
            }
            return url;
        }

        inline auto stripPrefixIf(const std::string& url, const std::string& pfx) const {
            if ((url.length() >= pfx.length()) && (url.substr(0, pfx.length()) == pfx)) {
                return url.substr(pfx.length());
            }
            return url;
        }

        inline auto getEmbeddedSourceURL(const std::string& surl) const {
            assert(type == s::wui::ContentSourceType::Embedded);
            auto url = surl;
            url = stripPrefixIf(url, empfx);
            url = stripPrefixIf(url, ":");
            url = stripPrefixIf(url, "//");
            url = stripPrefixIf(url, "app.res");
            url = stripPrefixIf(url, "/");
            if (url.at(url.length() - 1) == '/') {
                url = url.substr(0, url.length() - 1);
            }
            return url;
        }

        inline const auto& getEmbeddedSource(const std::string& surl) {
            auto url = getEmbeddedSourceURL(surl);
            assert(lst != nullptr);
            auto fit = lst->find(url);
            if(fit == lst->end()){
                throw s::wui::exception(std::string("unknown url:") + url);
            }
            return fit->second;
        }
    };

    inline void addCommonPage(s::wui::window& wb) {
        // NOTE: do not put console.log(), or any other native calls, in this code
        // as it will create recursion. Use alert() instead, but sparingly.
        static std::string initstr =
        "function _wui_convertToNative(val){\n"
        "  var nval = val;\n"
        "  nval = String(val);\n"
        "  return nval;\n"
        "}\n"
        "function _wui_convertFromNative(val){\n"
        "  var nval = val;\n"
        "  if(!val) {\n"
        "    return val;\n"
        "  }\n"
        "  try{\n"
        "    nval = eval(val);\n"
        "  }catch(ex){\n"
        "    return \"<err>\"\n"
        "  }\n"
        "  return nval;\n"
        "}\n"
        ;
        wb.eval(initstr);

        auto& wobj = wb.newObject("wui");
        wobj.fn("addMenu") = [&wb](const std::string& path, const std::string& name, const std::string& key, const std::string& cb) {
            wb.setMenu(path, name, key, [&wb, cb](){
                wb.eval("var x = new " + cb + "();");
            });
        };
        wobj.fn("setDefaultMenu") = [&wb]() {
            wb.setDefaultMenu();
        };
        wobj.fn("hasNativeMenu") = []() {
#ifdef WUI_WIN
            return true;
#endif
#ifdef WUI_OSX
            return true;
#endif
#ifdef WUI_NDK
            return false;
#endif
        };
        wb.addObject(wobj);
    }

    struct WindowRect {
        int left;
        int top;
        int width;
        int height;

        /// \brief default ctor
        inline WindowRect(const int& l, const int& t, const int& w, const int& h) : left(l), top(t), width(w), height(h) {}

        /// \brief adjust position according to screen size passed as parameters
        inline void adjust(const int& w, const int& h) {
            if (left < 0) {
                left = w + left;
            }

            if (top < 0) {
                top = h + top;
            }

            if (width < 0) {
                width = w + width - left + 1;
            }

            if (height < 0) {
                height = h + height - top + 1;
            }
        }
    };

} // namespace

inline s::wui::window::Impl& s::wui::window::impl() {
    return *impl_;
}

#ifdef WUI_OSX
////////////////////////////////////
@interface WuiObjDelegate : NSObject{
@public
    s::js::objectbase* jo_;
    s::wui::window* wb_;
}
@end

@implementation WuiObjDelegate

+(NSString*)webScriptNameForSelector:(SEL)sel {
    if(sel == @selector(invoke:pargs:))
        return @"invoke";
    if(sel == @selector(error:))
        return @"error";
    return nil;
}

+ (BOOL)isSelectorExcludedFromWebScript:(SEL)sel {
    if(sel == @selector(invoke:pargs:))
        return NO;
    if(sel == @selector(error:))
        return NO;
    return YES;
}

-(id)invoke:(NSString *)fn pargs:(WebScriptObject *)args {
    auto fnname = getCString(fn);
    std::vector<std::string> params;
    NSUInteger cnt = [[args valueForKey:@"length"] integerValue];
    for(unsigned int i = 0; i < cnt; ++i){
        NSString* item = [args webScriptValueAtIndex:i];
        if (![item isKindOfClass : [NSString class]]) {
            NSLog(@"%@", NSStringFromClass([item class]));
            throw std::invalid_argument("expected string value");
        }
        auto s = getCString(item);
        params.push_back(s);
    }
    auto rv = jo_->invoke(fnname, params);
    return getNSString(rv);
}

-(void)error:(NSString *)msg {
    auto m = getCString(msg);
    std::cout << "ERROR:" << m << std::endl;
}

@end

/////////////////////////////////
@interface WuiBrowserView : WebView {
}
@end

/////////////////////////////////
@implementation WuiBrowserView
- (BOOL) acceptsFirstResponder {
//    NSLog(@"acceptsFirstResponder called!");
    return YES;
}

- (BOOL)wantsScrollEventsForSwipeTrackingOnAxis:(NSEventGestureAxis)axis {
    return YES;
}

- (void)scrollWheel:(NSEvent *)event {
    CGFloat x = [event scrollingDeltaX];

    //NSLog(@"user delta-scrolled %f horizontally and %f vertically", [event scrollingDeltaX], [event scrollingDeltaY]);

    if (x != 0) {
        (x > 0) ? [self goBack:self] : [self goForward:self];
    }
}

@end

/////////////////////////////////
@interface WuiSecondaryView : WebView {
@public
    NSWindow* win_;
}
@end

/////////////////////////////////
@implementation WuiSecondaryView
- (void)keyDown: (NSEvent *) event {
//    NSLog(@"keyDown %d called!", [event keyCode]);
    if ([event keyCode] == 53){ //For escape key
        [win_ orderOut:win_];
    }
}

@end

/////////////////////////////////
@interface AppDelegate : NSObject<NSApplicationDelegate, WebFrameLoadDelegate, WebResourceLoadDelegate, WebUIDelegate, WebPolicyDelegate>{
@public
    s::wui::window* wb_;
    NSWindow* win_;
    WebView* wv_;
}
@end

#ifndef NO_NIB
@interface AppDelegate ()
@property (weak) IBOutlet NSWindow *window;
@end
#endif

/////////////////////////////////
@interface EmbeddedURLProtocol : NSURLProtocol {
}

+ (BOOL)canInitWithRequest:(NSURLRequest *)request;
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request;
+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b;
- (void)startLoading;
- (void)stopLoading;

@end

/////////////////////////////////
@interface NSMenuItem (Q)
- (void)setBlockAction:(void (^)(id sender))block;
- (void (^)(id))blockAction;
@end

static const char * const qBlockActionKey = "BlockActionKey";

@implementation NSMenuItem (Q)

- (void)setBlockAction:(void (^)(id))block {
    objc_setAssociatedObject(self, qBlockActionKey, nil, OBJC_ASSOCIATION_RETAIN);

    if (block == nil) {
        [self setTarget:nil];
        [self setAction:NULL];

        return;
    }

    objc_setAssociatedObject(self, qBlockActionKey, block, OBJC_ASSOCIATION_RETAIN);
    [self setTarget:self];
    [self setAction:@selector(blockActionWrapper:)];
}

- (void (^)(id))blockAction {
    return objc_getAssociatedObject(self, qBlockActionKey);
}

- (void)blockActionWrapper:(id)sender {
    void (^block)(id) = objc_getAssociatedObject(self, qBlockActionKey);

    block(sender);
}

@end

/////////////////////////////////
class s::application::Impl {
    s::application& app_;
public:
    inline Impl(s::application& a) : app_(a) {
        s::js::unused(app_);
    }

    inline ~Impl() {
    }

    inline int loop() {
#ifdef NO_NIB
        NSApplication *app = [NSApplication sharedApplication];

        // create AppDelegate
        auto wd = [AppDelegate new];
        [app setDelegate : wd];

        [app run];
        return 0;
#else
        return NSApplicationMain(app_.argc, app_.argv);
#endif
    }

    inline void exit(const int& /*exitcode*/) {
        [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:0.0];
    }

    inline std::string datadir(const std::string& an) const {
        NSString* dir = 0;
        NSArray* arr = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
        if(arr != nullptr){
            dir = [arr objectAtIndex:0];
            if(dir == nullptr){
                dir = NSHomeDirectory();
            }
        }else{
            dir = NSHomeDirectory();
        }

        return getCString(dir) + "/" + an;
    }
};

/////////////////////////////////
struct s::wui::window::Impl {
    WuiSecondaryView* webView2;
    NSWindow* window2;
private:
    s::wui::window& wb;
    NSWindow* window;
    WuiBrowserView* webView;
    AppDelegate* wd;
    ContentSourceData csd;
public:
    inline Impl(s::wui::window& w) : wb(w), window(nullptr), webView(nullptr), webView2(nullptr), wd(nullptr) {
    }

    inline void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>>& lst) {
        csd.setEmbeddedSource(lst);
    }

    inline void setContentSourceResource(const std::string& path) {
        csd.setResourceSource(path);
    }

    inline const auto& getEmbeddedSource(const std::string& url) {
        return csd.getEmbeddedSource(url);
    }

    inline auto& getContentSource() const {return csd;}

    inline NSMenuItem* createMenuItem(NSMenu* menu, NSString* title, NSString* key, std::function<void()> cb = std::function<void()>()) {
        NSMenuItem* menuItem = [menu itemWithTitle:title];
        if(!menuItem){
            menuItem = [[NSMenuItem alloc] init];
            [menuItem setTitle:title];
//            [menuItem setTarget:window];
            [menu addItem : menuItem];
        }

        if(cb){
            [menuItem setBlockAction:^(id /*sender*/) {
                cb();
            }];
        }

        if(key != 0){
            [menuItem setKeyEquivalent:key];
        }

        return menuItem;
    }

    inline NSMenu* createMenu(id menubar, NSString* title) {
        auto menuItem = createMenuItem(menubar, title, 0, 0);
        NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
        [menuItem setSubmenu : menu];
//        [menu setAutoenablesItems:YES];
        return menu;
    }

    inline void setDefaultMenu() {
        id appName = [[NSProcessInfo processInfo] processName];

        NSMenu* menubar = [[NSMenu alloc] initWithTitle:@"Filex"];
//        [menubar setAutoenablesItems:YES];
        [NSApp setMainMenu : menubar];

        NSMenu* appMenu = createMenu(menubar, @"<AppName>"); // OSX always uses the actual app name here

        NSString* aboutTitle = [@"About " stringByAppendingString:appName];
        createMenuItem(appMenu, aboutTitle, 0, 0);

        NSString* quitTitle = [@"Quit " stringByAppendingString:appName];
        createMenuItem(appMenu, quitTitle, @"q", [](){
            [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:0.0];
        });


        NSMenu* fileMenu = createMenu(menubar, @"File");
        createMenuItem(fileMenu, @"New", @"n", [](){
        });

        createMenuItem(fileMenu, @"Open", @"o");
        createMenuItem(fileMenu, @"Close", @"w");
        createMenuItem(fileMenu, @"Save", @"s");

        NSMenu* editMenu = createMenu(menubar, @"Edit");
        createMenuItem(editMenu, @"Undo", @"z");
        createMenuItem(editMenu, @"Redo", @"Z");
        createMenuItem(editMenu, @"Cut", @"x");
        createMenuItem(editMenu, @"Copy", @"c");
        createMenuItem(editMenu, @"Paste", @"v");

        NSMenu* helpMenu = createMenu(menubar, @"Help");
        createMenuItem(helpMenu, @"Help", @"/");
    }

    inline void setMenu(const std::string& path, const std::string& name, const std::string& key, std::function<void()> cb) {
        NSString* mpath = getNSString(path);
        NSString* mname = getNSString(name);
        NSString* mkey = getNSString(key);

        NSArray* pathparts = [mpath componentsSeparatedByString:@"/"];
        NSMenu *mainMenu = [[NSApplication sharedApplication] mainMenu];

        NSMenu *currMenu = mainMenu;
        for (NSString* iname in pathparts) {
            NSMenuItem* mi = [currMenu itemWithTitle:iname];
            currMenu = [mi submenu];
        }

        createMenuItem(currMenu, mname, mkey, cb);
    }

    inline void setAlwaysOnTop(const bool& aot) {
        if(aot){
            [window setLevel:NSStatusWindowLevel];
        }else{
            // \todo: remove AOT
        }
    }

    inline bool open(const int& left, const int& top, const int& width, const int& height) {
        // get singleton app instance
        NSApplication *app = [NSApplication sharedApplication];

        NSArray *screenArray = [NSScreen screens];
        NSScreen *screen = [screenArray objectAtIndex:0];
        NSRect screenRect = [screen visibleFrame];

        WindowRect frc(left, top, width, height);
        frc.adjust(screenRect.size.width, screenRect.size.height);

        // position for main window
        NSRect frame = NSMakeRect(frc.left, frc.top, frc.width, frc.height);

        // create WuiBrowserView instance
        webView = [[WuiBrowserView alloc] initWithFrame:frame frameName : @"myWV" groupName : @"webViews"];

        // get AppDelegate
        wd = (AppDelegate*)[app delegate];

        // get top-level window
        auto windows = [app windows];
        assert(windows);
        if([windows count] > 0){
            window = [[app windows] objectAtIndex:0];
        }else{
            // create top window
            window = [[NSWindow alloc]
                      initWithContentRect:frame
                      styleMask : NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
                      backing : NSBackingStoreBuffered
                      defer : NO];
            [window setLevel:NSMainMenuWindowLevel + 1];
            [window setOpaque:YES];
            [window setHasShadow:YES];
            [window setPreferredBackingLocation:NSWindowBackingLocationVideoMemory];
            [window setHidesOnDeactivate:NO];
            [window setBackgroundColor : [NSColor blueColor]];
        }

        assert(wd != 0);
        [webView setWantsLayer : YES];
        [webView setCanDrawConcurrently : YES];
        [webView setFrameLoadDelegate : wd];
        [webView setUIDelegate : wd];
        [webView setResourceLoadDelegate : wd];
        [webView setPolicyDelegate : wd];

        // create secondary window
        window2 = [[NSWindow alloc]
                  initWithContentRect:frame
                  styleMask : NSTitledWindowMask | NSResizableWindowMask
                  backing : NSBackingStoreBuffered
                  defer : NO];
        [window2 setLevel:NSMainMenuWindowLevel + 1];
//        [window2 setPreferredBackingLocation:NSWindowBackingLocationVideoMemory];

        webView2 = [[WuiSecondaryView alloc] initWithFrame:frame frameName : @"myWV2" groupName : @"webViews"];
        [webView2 setUIDelegate : wd];

        [webView2 setFrameLoadDelegate : wd];
        [webView2 setResourceLoadDelegate : wd];
        [webView2 setPolicyDelegate : wd];

        webView2->win_ = window2;
        [window2 setContentView : webView2];

        // set webView as content of top-level window
        assert(window != nullptr);
        [window setContentView : webView];

        // bring window to front(required only when launching binary executable from command line)
        [window makeKeyAndOrderFront:window];
        [NSApp activateIgnoringOtherApps:YES];

        // intialize AppDelegate
        wd->wb_ = &wb;
        wd->win_ = window;
        wd->wv_ = nullptr;

        return true;
    }

    inline void loadPage(NSString *ustr) {
        NSURL *url = [NSURL URLWithString : ustr];
        assert(url != nullptr);
        NSURLRequest *request = [NSURLRequest requestWithURL : url];
        assert(request != nullptr);
        [webView.mainFrame loadRequest : request];
    }

    inline void go(const std::string& urlx) {
        auto surl = csd.normaliseUrl(urlx);
        NSString *pstr = getNSString(surl);
        dispatch_async(dispatch_get_main_queue(), ^{
           loadPage(pstr);
        });
    }

    inline void eval(const std::string& str) {
        auto jstr = jspfx + str;
        NSString* evalScriptString = [NSString stringWithUTF8String : jstr.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            auto wso = [webView windowScriptObject];
            [wso evaluateWebScript : evalScriptString];
        });
    }

    inline void addNativeObject(s::js::objectbase& jo, WebScriptObject* wso, const std::string& body) {
        WuiObjDelegate* wob = [WuiObjDelegate new];
        wob->jo_ = &jo;
        wob->wb_ = &wb;
        assert(wso != 0);
        NSString *pstr = getNSString(jo.nname);
        [wso setValue : wob forKey : pstr];
        eval(body);
    }

    inline void addNativeObject(s::js::objectbase& jo, const std::string& body) {
        WebScriptObject* wso = [webView windowScriptObject];
        assert(wso != 0);
        return addNativeObject(jo, wso, body);
    }
};

/////////////////////////////////
@implementation EmbeddedURLProtocol

+(BOOL)canInitWithRequest:(NSURLRequest *)request {
    NSURL *url = [request URL];
    return [[url scheme] isEqualToString:@"embedded"];
}

+(NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {
    return request;
}

+(BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b {
    return [[[a URL] resourceSpecifier] isEqualToString:[[b URL] resourceSpecifier]];
}

-(void)startLoading {
    NSURL *url = [[self request] URL];
    NSString *pathString = [url resourceSpecifier];
    //NSLog(@"Loading %@", pathString);

    NSApplication *app = [NSApplication sharedApplication];
    auto wd = (AppDelegate*)[app delegate];
    assert((wd != nullptr) && (wd->wb_ != nullptr));
    auto cpath = getCString(pathString);
    while(cpath.at(0) == '/'){
        cpath = cpath.substr(1);
    }
    auto& data = wd->wb_->impl().getEmbeddedSource(cpath);

    NSString *mimeType = getNSString(std::get<2>(data));
    NSURLResponse *response = [[NSURLResponse alloc] initWithURL:url MIMEType:mimeType expectedContentLength:-1 textEncodingName:nil];

    [[self client] URLProtocol:self
            didReceiveResponse:response
            cacheStoragePolicy:NSURLCacheStorageNotAllowed];
    [[self client] URLProtocol:self didLoadData:[NSData dataWithBytes:std::get<0>(data) length:std::get<1>(data)]];
    [[self client] URLProtocolDidFinishLoading:self];
}

-(void)stopLoading {
}

@end

/////////////////////////////////
@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    s::js::unused(aNotification);
    if ([NSURLProtocol registerClass:[EmbeddedURLProtocol class]]) {
        //NSLog(@"URLProtocol registration successful.");
    } else {
        NSLog(@"URLProtocol registration failed.");
    }

    // call callback
    if(s::app().onInit){
        s::app().onInit();
    }

    assert(wb_);
    if (wb_->onOpen) {
        wb_->onOpen();
    }
}

- (WebView *)webView:(WuiBrowserView *)sender createWebViewWithRequest:(NSURLRequest *)request {
//    NSLog(@"createWebViewWithRequest: %@, %@", request, [[request URL] absoluteString]);
    s::js::unused(sender);
    s::js::unused(request);
    wv_ = wb_->impl().webView2;
    return wv_;
}

- (void)webViewShow:(WebView *)sender {
//    NSLog(@"webViewShow");
    s::js::unused(sender);
    [wb_->impl().window2 makeKeyAndOrderFront:wb_->impl().window2];
}

-(BOOL)applicationShouldTerminateAfterLastWindowClosed : (NSApplication *)theApplication {
    s::js::unused(theApplication);
    return YES;
}

//- (void)webView:(WebView *)sender willPerformClientRedirectToURL:(NSURL *)URL delay:(NSTimeInterval)seconds fireDate:(NSDate *)date forFrame:(WebFrame *)frame {
//    NSLog(@"willPerformClientRedirectToURL: %@", [URL absoluteString]);
//}

//- (void)webView:(WebView *)webView didReceiveServerRedirectForProvisionalLoadForFrame:(WebFrame *)frame {
//    NSLog(@"%@",[[[[frame provisionalDataSource] request] URL] absoluteString]);
//}

- (void)webView:(WebView *)webView decidePolicyForNavigationAction:(NSDictionary *)actionInformation request:(NSURLRequest *)request frame:(WebFrame *)frame decisionListener:(id)listener {
    NSString *urlString = [[request URL] absoluteString];
    //NSLog(@"decidePolicyForNavigationAction: %@", urlString);
    if (wb_->onNavigating) {
        auto url = getCString(urlString);
        if(wb_->onNavigating(url)){
            [listener use];
        }else{
            [listener ignore];
        }
    }else{
        [listener use];
    }
}

- (void)webView:(WuiBrowserView *)sender didFinishLoadForFrame:(WebFrame *)frame {
    s::js::unused(sender);
    NSString *urlString = [[NSString alloc] initWithString:[[[[frame dataSource] request] URL] absoluteString]];
//    NSLog(@"OnLoad: %@", urlString);
    auto url = getCString(urlString);
    assert(wb_);
    auto& csd = wb_->impl().getContentSource();
    if (wb_->onLoad) {
        auto surl = url;
        surl = csd.getEmbeddedSourceURL(surl);
        wb_->onLoad(surl);
    }
}

- (void) webView:(WuiBrowserView*)webView addMessageToConsole:(NSDictionary*)message {
    s::js::unused(webView);
    if (![message isKindOfClass:[NSDictionary class]]) {
        return;
    }

    NSLog(@"JavaScript console: %@:%@: %@",
          [[message objectForKey:@"sourceURL"] lastPathComponent],	// could be nil
          [message objectForKey:@"lineNumber"],
          [message objectForKey:@"message"]);
}

-(void)webView:(WuiBrowserView *)sender runJavaScriptAlertPanelWithMessage:(NSString *)message initiatedByFrame:(WebFrame *)frame {
    s::js::unused(sender);
    s::js::unused(frame);
    NSAlert *alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:@"OK"];
    [alert setMessageText:message];

    [alert beginSheetModalForWindow:win_ completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertSecondButtonReturn) {
            return;
        }
        //NSLog(@"Modal closed");
    }];
}

-(void)webView:(WuiBrowserView *)webView windowScriptObjectAvailable : (WebScriptObject *)wso {
    s::js::unused(wso);
    s::js::unused(webView);
    assert(wb_);
    addCommonPage(*wb_);
}

@end

#endif // #ifdef WUI_OSX

#ifdef WUI_WIN
// This code adapted from Tobbe

namespace {
    struct Tracer {
        const std::string name_;
        inline Tracer(const std::string& name) : name_(name) {
            std::cout << std::this_thread::get_id() << ":ENTER:" << name_ << std::endl;
        }
        inline ~Tracer() {
            std::cout << std::this_thread::get_id() << ":LEAVE:" << name_ << std::endl;
        }
    };

#if 0
#define TRACER(n) Tracer __t__(n);
#else
#define TRACER(n)
#endif
#if 0
#define TRACER1(n) Tracer __t__(n);
#else
#define TRACER1(n)
#endif

#define WM_EVAL (WM_APP+1)

    inline std::string GetErrorAsString(HRESULT hr) {
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        std::string message(messageBuffer, size);

        //Free the buffer.
        LocalFree(messageBuffer);

        return message;
    }

    //Returns the last Win32 error, in string format. Returns an empty string if there is no error.
    inline std::string GetLastErrorAsString() {
        //Get the error message, if any.
        DWORD errorMessageID = ::GetLastError();
        if (errorMessageID == 0)
            return std::string(); //No error message has been recorded

        return GetErrorAsString(errorMessageID);
    }

    // {F1EC293F-DBBD-4A4B-94F4-FA52BA0BA6EE}
    static const GUID CLSID_TInternetProtocol = { 0xf1ec293f, 0xdbbd, 0x4a4b,{ 0x94, 0xf4, 0xfa, 0x52, 0xba, 0xb, 0xa6, 0xee } };

    inline std::string VariantToString(VARIANTARG& var){
        assert(var.vt == VT_BSTR);
        _bstr_t bstrArg = var.bstrVal;
        std::wstring arg = (const wchar_t*)bstrArg;
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
        auto rv = std::string(convertor.to_bytes(arg));
        return rv;
    }

    inline std::string getStringFromCOM(DISPPARAMS* dp, const size_t& idx){
        return VariantToString(dp->rgvarg[idx]);
    }

    inline CComPtr<IDispatch> VariantToDispatch(VARIANT var){
        CComPtr<IDispatch> disp = (var.vt == VT_DISPATCH && var.pdispVal) ? var.pdispVal : NULL;
        return disp;
    }

    inline HRESULT VariantToInteger(VARIANT var, long &integer){
        CComVariant _var;
        HRESULT hr = VariantChangeType(&_var, &var, 0, VT_I4);
        if(FAILED(hr)){
            return hr;
        }
        integer = _var.lVal;
        return S_OK;
    }

    inline HRESULT DispatchGetProp(CComPtr<IDispatch> disp, LPOLESTR name, VARIANT *pVar){
        HRESULT hr = S_OK;

        if(!pVar){
            return E_INVALIDARG;
        }

        DISPID dispid = 0;
        hr = disp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
        if(FAILED(hr)){
            return hr;
        }

        DISPPARAMS dispParams = {0};
        hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dispParams, pVar, NULL, NULL);
        if(FAILED(hr)){
            return hr;
        }
        return hr;
    }

    inline std::vector<std::string> getStringArrayFromCOM(DISPPARAMS* dp, const size_t& idx){
        assert(dp->rgvarg[idx].vt == VT_DISPATCH);

        std::vector<std::string> rv;
        CComPtr<IDispatch> pDispatch = VariantToDispatch(dp->rgvarg[idx]);
        if(!pDispatch)
            return rv;

        CComVariant varLength;
        HRESULT hr = DispatchGetProp(pDispatch, L"length", &varLength);
        if(FAILED(hr)){
            return rv;
        }

        long intLength;
        hr = VariantToInteger(varLength, intLength);
        if(FAILED(hr)){
            return rv;
        }

        WCHAR wcharIndex[25];
        CComVariant varItem;
        for(long index = intLength; index > 0; --index){
            wsprintf(wcharIndex, _T("%ld\0"), index-1);
            hr = DispatchGetProp(pDispatch, CComBSTR(wcharIndex), &varItem);
            if(FAILED(hr)){
                return rv;
            }
            auto item = VariantToString(varItem);
            rv.push_back(item);
        }

        return rv;
    }

    struct WinObject : public IDispatch {
        inline WinObject(s::js::objectbase& jo) : jo_(jo), ref(0){
            TRACER1("WinObject::WinObject");
        }

        inline ~WinObject(){
            TRACER1("WinObject::~WinObject");
            assert(ref == 0);
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv){
            TRACER1("WinObject::QueryInterface");
            *ppv = NULL;

            if(riid == IID_IUnknown || riid == IID_IDispatch){
                *ppv = static_cast<IDispatch*>(this);
            }

            if(*ppv != NULL){
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef(){
            TRACER1("WinObject::AddRef");
            return InterlockedIncrement(&ref);
        }

        ULONG STDMETHODCALLTYPE Release(){
            TRACER1("WinObject::Release");
            int tmp = InterlockedDecrement(&ref);
            assert(tmp >= 0);
            if(tmp == 0){
                //delete this;
            }

            return tmp;
        }

        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *pctinfo){
            TRACER("WinObject::GetTypeInfoCount");
            *pctinfo = 0;

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT /*iTInfo*/, LCID /*lcid*/, ITypeInfo** /*ppTInfo*/){
            TRACER("WinObject::GetTypeInfo");
            return E_FAIL;
        }

        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID /*riid*/,
                                                           LPOLESTR *rgszNames, UINT cNames, LCID /*lcid*/, DISPID *rgDispId){
            //TRACER("WinObject::GetIDsOfNames");
            HRESULT hr = S_OK;

            for(UINT i = 0; i < cNames; i++){
                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
                std::string fnname(convertor.to_bytes(rgszNames[i]));
                if(fnname == "invoke"){
                    rgDispId[i] = DISPID_VALUE + 1;
                } else{
                    rgDispId[i] = DISPID_UNKNOWN;
                    hr = DISP_E_UNKNOWNNAME;
                }
            }
            return hr;
        }

        HRESULT STDMETHODCALLTYPE Invoke(
            DISPID dispIdMember,
            REFIID /*riid*/,
            LCID /*lcid*/,
            WORD wFlags,
            DISPPARAMS *pDispParams,
            VARIANT *pVarResult,
            EXCEPINFO* /*pExcepInfo*/,
            UINT* /*puArgErr*/){
            TRACER("WinObject::Invoke");
            if(wFlags & DISPATCH_METHOD){
                if(dispIdMember == DISPID_VALUE + 1){
                    auto params = getStringArrayFromCOM(pDispParams, 0);
                    auto fn = getStringFromCOM(pDispParams, 1);
                    auto rv = jo_.invoke(fn, params);
                    CComVariant crv(rv.c_str());
                    crv.Detach(pVarResult);
                    return S_OK;
                }
                assert(false);
            }

            return E_FAIL;
        }
    private:
        s::js::objectbase& jo_;
        long ref;
    };

    const LPCWSTR WEBFORM_CLASS = L"WebUIWindowClass";

}

struct s::wui::window::Impl
    : public IOleClientSite
    , public IOleInPlaceSite
    , public IOleInPlaceFrame
    , public IDispatch
    , public IDocHostUIHandler
    , public IInternetProtocolInfo
    , public IClassFactory
{
    ContentSourceData csd;

    long ref;
    std::thread::id threadID_;

    inline const auto& getEmbeddedSource(const std::string& url) {
        return csd.getEmbeddedSource(url);
    }

    inline void Close(){
        if(ibrowser != 0){
            CComPtr<IConnectionPointContainer> cpc;
            ibrowser->QueryInterface(IID_IConnectionPointContainer, (void**)&cpc);
            if(cpc != 0){
                CComPtr<IConnectionPoint> cp;
                cpc->FindConnectionPoint(DIID_DWebBrowserEvents2, &cp);
                if(cp != 0){
                    cp->Unadvise(cookie);
                }
            }

            CComPtr<IOleObject> iole;
            ibrowser->QueryInterface(IID_IOleObject, (void**)&iole);
            if(iole != 0){
                iole->Close(OLECLOSE_NOSAVE);
            }

            ibrowser->Release();
            ibrowser = 0;
        }
    }
    inline void setMenu(const std::string& /*path*/, const std::string& /*name*/, const std::string& /*key*/, std::function<void()> /*cb*/) {
    }

    inline void SetFocus(){
        if(ibrowser != 0){
            CComPtr<IOleObject> iole;
            ibrowser->QueryInterface(IID_IOleObject, (void**)&iole);
            if(iole != 0){
                iole->DoVerb(OLEIVERB_UIACTIVATE, NULL, this, 0, hhost, 0);
            }
        }
    }

    inline IHTMLDocument2* GetDoc(){
        CComPtr<IDispatch> xdispatch;
        HRESULT hr = ibrowser->get_Document(&xdispatch);
        if(xdispatch == 0){
            throw s::wui::exception(std::string("unable to get document:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        CComPtr<IHTMLDocument2> doc;
        xdispatch->QueryInterface(IID_IHTMLDocument2, (void**)&doc);
        return doc;
    }

    inline void addCustomObject(IDispatch* custObj, const std::string& name){
        TRACER("addCustomObject");

        HRESULT hr;
        CComPtr<IHTMLDocument2> doc = GetDoc();
        if(doc == NULL){
            throw s::wui::exception(std::string("Invalid document state:") + GetLastErrorAsString());
        }

        CComPtr<IHTMLWindow2> win;
        hr = doc->get_parentWindow(&win);
        if(win == NULL){
            throw s::wui::exception(std::string("unable to get parent window:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        CComPtr<IDispatchEx> winEx;
        hr = win->QueryInterface(&winEx);
        if(winEx == NULL){
            throw s::wui::exception(std::string("unable to get DispatchEx:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        _bstr_t objName(name.c_str());

        DISPID dispid;
        hr = winEx->GetDispID(objName, fdexNameEnsure, &dispid);
        if(FAILED(hr)){
            throw s::wui::exception(std::string("unable to get DispatchID:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        DISPID namedArgs[] = {DISPID_PROPERTYPUT};
        DISPPARAMS params;
        params.rgvarg = new VARIANT[1];
        params.rgvarg[0].pdispVal = custObj;
        params.rgvarg[0].vt = VT_DISPATCH;
        params.rgdispidNamedArgs = namedArgs;
        params.cArgs = 1;
        params.cNamedArgs = 1;

        hr = winEx->InvokeEx(dispid, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &params, NULL, NULL, NULL);
        if(FAILED(hr)){
            throw s::wui::exception(std::string("unable to invoke JSE:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }
    }

    inline bool open(const int& left, const int& top, const int& width, const int& height){
        TRACER("open");
        WNDCLASSEX wcex = {0};
        HINSTANCE hInstance = GetModuleHandle(0);
        if(!::GetClassInfoExW(hInstance, WEBFORM_CLASS, &wcex)){
            wcex.cbSize = sizeof(WNDCLASSEXW);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = (WNDPROC)WebformWndProc;
            wcex.hInstance = hInstance;
            wcex.lpszClassName = WEBFORM_CLASS;
            wcex.cbWndExtra = sizeof(s::wui::window::Impl*);
            RegisterClassExW(&wcex);
        }
        HWND hwndParent = 0;
        UINT id = 0;
        UINT flags = WS_CLIPSIBLINGS | WS_VSCROLL;
        if(hwndParent == 0){
            flags |= WS_OVERLAPPEDWINDOW;
        } else{
            flags |= WS_CHILDWINDOW;
        }

        std::wstring title(convertor.from_bytes(s::app().title));

        RECT rc;
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, (void*)&rc, 0);
        WindowRect frc(left, top, width, height);
        frc.adjust(rc.right-rc.left, rc.bottom-rc.top);

        HWND hWnd = CreateWindowW(WEBFORM_CLASS, title.c_str(), flags, frc.left, frc.top, frc.width, frc.height, hwndParent, (HMENU)id, hInstance, (LPVOID)this);
        if(hWnd == NULL){
            throw s::wui::exception(std::string("Could not create window:") + GetLastErrorAsString());
        }
        ::ShowWindow(hWnd, SW_SHOW);
        ::UpdateWindow(hWnd);
        return (hWnd != 0);
    }

    // Register our protocol so that urlmon will call us for every
    // url that starts with HW_PROTO_PREFIX
    inline void RegisterInternetProtocolFactory(){
        CComPtr<IInternetSession> internetSession;
        HRESULT hr = ::CoInternetGetSession(0, &internetSession, 0);
        assert(!FAILED(hr));
        hr = internetSession->RegisterNameSpace(this, CLSID_TInternetProtocol, empfxw.c_str(), 0, nullptr, 0);
        assert(!FAILED(hr));
    }

    inline void UnregisterInternetProtocolFactory(){
        CComPtr<IInternetSession> internetSession;
        HRESULT hr = ::CoInternetGetSession(0, &internetSession, 0);
        assert(!FAILED(hr));
        internetSession->UnregisterNameSpace(this, empfxw.c_str());
    }

    inline void setDefaultMenu(){
    }

    inline void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>>& lst){
        csd.setEmbeddedSource(lst);
    }

    inline void setContentSourceResource(const std::string& path){
        csd.setResourceSource(path);
    }

    std::queue<std::string> evalList_;
    std::mutex mxEval_;
    inline void pushEval(const std::string& str) {
        std::lock_guard<std::mutex> lk(mxEval_);
        evalList_.push(str);
    }

    inline auto popEval() {
        std::lock_guard<std::mutex> lk(mxEval_);
        auto str = evalList_.front();
        evalList_.pop();
        return str;
    }

    inline void evalStr(const std::string& str){
        TRACER("evalStr:" + str);
        assert(std::this_thread::get_id() == threadID_);

        CComPtr<IHTMLDocument2> doc = GetDoc();
        if(doc == NULL){
            throw s::wui::exception(std::string("Unable to get document object:") + GetLastErrorAsString());
        }

        CComPtr<IHTMLWindow2> win;
        doc->get_parentWindow(&win);
        if(win == NULL){
            throw s::wui::exception(std::string("Unable to get parent window:") + GetLastErrorAsString());
        }

        std::wstring rv(convertor.from_bytes(str));
        VARIANT v;
        VariantInit(&v);
        HRESULT hr = win->execScript((BSTR)rv.c_str(), NULL, &v);
        if(hr != S_OK){
            throw s::wui::exception(std::string("JavaScript execution error:") + GetErrorAsString(hr));
        }

        VariantClear(&v);
        ::InvalidateRect(hhost, 0, true);
    }

    inline void evalQ() {
        auto str = popEval();
        evalStr(str);
    }

    inline void eval(const std::string& str) {
        if (std::this_thread::get_id() == threadID_) {
            evalStr(str);
        }else {
            pushEval(str);
            ::PostMessageA(hhost, WM_EVAL, 0, 0);
        }
    }

    inline void addNativeObject(s::js::objectbase& jo, const std::string& body){
        TRACER("addNativeObject");
        //std::cout << "addNativeObject:" << jo.name << ":" << jo.nname << ":" << body << std::endl;
        WinObject* nobj = new WinObject(jo);
        addCustomObject(nobj, jo.nname);
        eval(body);
    }

    inline void go(const std::string& urlx){
        auto url = csd.normaliseUrl(urlx);

        // Navigate to the new one and delete the old one
        std::wstring ws(convertor.from_bytes(url));

        VARIANT v;
        v.vt = VT_I4;
        v.lVal = 0; //v.lVal=navNoHistory;
        HRESULT hr = ibrowser->Navigate((BSTR)ws.c_str(), &v, NULL, NULL, NULL);
        s::js::unused(hr);
    }

    inline bool setupOle(){
        TRACER("setupOle");
        hasscrollbars = (GetWindowLongPtr(hhost, GWL_STYLE)&(WS_HSCROLL | WS_VSCROLL)) != 0;

        RECT rc;
        GetClientRect(hhost, &rc);

        HRESULT hr;
        CComPtr<IOleObject> iole;
        hr = CoCreateInstance(CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER, IID_IOleObject, (void**)&iole);
        if(hr != S_OK){
            throw s::wui::exception(std::string("CoCreateInstance error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        hr = iole->SetClientSite(this);
        if(hr != S_OK){
            throw s::wui::exception(std::string("SetClientSite error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        hr = iole->SetHostNames(L"MyHost", L"MyDoc");
        if(hr != S_OK){
            throw s::wui::exception(std::string("SetHostNames error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        hr = OleSetContainedObject(iole, TRUE);
        if(hr != S_OK){
            throw s::wui::exception(std::string("OleSetContainedObject error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        hr = iole->DoVerb(OLEIVERB_SHOW, 0, this, 0, hhost, &rc);
        if(hr != S_OK){
            throw s::wui::exception(std::string("DoVerb error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        bool connected = false;
        CComPtr<IConnectionPointContainer> cpc;
        hr = iole->QueryInterface(IID_IConnectionPointContainer, (void**)&cpc);
        if(cpc != 0){
            CComPtr<IConnectionPoint> cp;
            hr = cpc->FindConnectionPoint(DIID_DWebBrowserEvents2, &cp);
            if(cp != 0){
                cp->Advise((IDispatch*)this, &cookie);
                connected = true;
            }
        }

        if(!connected){
            throw s::wui::exception(std::string("Not connected error:") + GetLastErrorAsString() + "(" + GetErrorAsString(hr) + ")");
        }

        iole->QueryInterface(IID_IWebBrowser2, (void**)&ibrowser);
        return true;
    }

    inline bool hookMessage(MSG& msg){
        if((msg.message >= WM_KEYFIRST) && (msg.message <= WM_KEYLAST)){
            CComPtr<IOleInPlaceActiveObject> ioipo;
            ibrowser->QueryInterface(IID_IOleInPlaceActiveObject, (void**)&ioipo);
            assert(ioipo);
            if(ioipo != 0){
                HRESULT hr = ioipo->TranslateAccelerator(&msg);
                if(hr == S_OK){
                    return true;
                }
            }
        }
        return false;
    }

    static LRESULT CALLBACK WebformWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
        if(msg == WM_NCCREATE){
            s::wui::window::Impl* impl = (s::wui::window::Impl*)((LPCREATESTRUCT(lParam))->lpCreateParams);
            impl->hhost = hwnd;
            impl->AddRef();
            impl->setupOle();
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)impl);
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        s::wui::window::Impl* impl = (s::wui::window::Impl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if(impl == 0){
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        CREATESTRUCTA* cs = 0;

        switch(msg){
        case WM_CREATE:
            cs = (CREATESTRUCTA*)lParam;
            if(cs->style & (WS_HSCROLL | WS_VSCROLL)){
                SetWindowLongPtr(hwnd, GWL_STYLE, cs->style & ~(WS_HSCROLL | WS_VSCROLL));
            }
            if(impl->wb_.onOpen){
                impl->wb_.onOpen();
            }
            break;
        case WM_DESTROY:
            impl->Close();
            impl->Release();
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            break;
        case WM_CLOSE:
            if(impl->wb_.onClose){
                impl->wb_.onClose();
            }
            break;
        case WM_SETFOCUS:
            impl->SetFocus();
            break;
        case WM_EVAL:
            impl->evalQ();
            break;
        case WM_SIZE:
            impl->ibrowser->put_Width(LOWORD(lParam));
            impl->ibrowser->put_Height(HIWORD(lParam));
            break;
        };
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    inline void BeforeNavigate2(const wchar_t* burl, VARIANT_BOOL* cancel) {
        TRACER1("BeforeNavigate2");
        std::wstring wurl = burl;
        if (wb_.onNavigating) {
            std::string url(convertor.to_bytes(wurl));
            if (!wb_.onNavigating(url)) {
                *cancel = VARIANT_TRUE;
            }
        }
    }

    inline void NavigateComplete2(const wchar_t* burl){
        TRACER1("NavigateComplete2");
        addCommonPage(wb_);
        if(wb_.onLoad){
            std::wstring wurl = burl;
            std::string url(convertor.to_bytes(wurl));
            url = csd.getEmbeddedSourceURL(url);
            wb_.onLoad(url);
        }
    }

    inline void setIcon(const std::string& favi){
        if(csd.type != s::wui::ContentSourceType::Embedded){
            std::cout << "set-icon skipped" << std::endl;
            return;
        }
        auto& pdata = csd.getEmbeddedSource(favi);
        auto data = std::get<0>(pdata);
        auto dataLen = std::get<1>(pdata);
        s::js::unused(dataLen);
        auto bdata = (PBYTE)data;
        auto offset = LookupIconIdFromDirectoryEx(bdata, TRUE, 0, 0, LR_DEFAULTCOLOR);
        if(offset != 0){
            HICON hIcon = ::CreateIconFromResourceEx(bdata + offset, 0, TRUE, 0x30000, 0, 0, LR_DEFAULTCOLOR);
            ::SetClassLong(hhost, GCL_HICON, (long)hIcon);
        }
    }

    inline void getFaviconURLFromContent(){
        CComPtr<IHTMLDocument2> doc = GetDoc();
        if(doc == NULL){
            throw s::wui::exception(std::string("Invalid document state:") + GetLastErrorAsString());
        }

        std::string favurl;
        CComPtr<IHTMLElementCollection> elems;
        if(!SUCCEEDED(doc->get_all(&elems))){
            throw s::wui::exception(std::string("unable to get elements:") + GetLastErrorAsString());
        }

        long length;
        if(!SUCCEEDED(elems->get_length(&length))){
            throw s::wui::exception(std::string("unable to get element-list length:") + GetLastErrorAsString());
        }

        // iterate over elements in the document
        for(int i = 0; i < length; i++){
            CComVariant index = i;
            CComQIPtr<IDispatch> pElemDisp;
            elems->item(index, index, &pElemDisp);
            if(!pElemDisp){
                continue;
            }
            CComQIPtr<IHTMLElement> pElem = pElemDisp;
            if(!pElem){
                continue;
            }

            CComBSTR bstrTagName;
            if(FAILED(pElem->get_tagName(&bstrTagName))){
                continue;
            }

            std::string tagName(convertor.to_bytes(bstrTagName));
            //tagName.MakeLower();

            // to speed up, only parse elements before the body element
            if(tagName == "BODY"){
                break;
            }

            // check for title
            if(tagName == "TITLE"){
                CComBSTR wtitle;
                if(SUCCEEDED(pElem->get_innerText(&wtitle))){
                    std::string title(convertor.to_bytes(wtitle));
                    //std::cout << "got doc:title:" << title << std::endl;
                    if(title.length() > 0){
                        ::SetWindowTextA(hhost, title.c_str());
                    }
                }
            }

            // if link, check for favicon
            if(tagName == "LINK"){
                CComVariant vRel;
                if(SUCCEEDED(pElem->getAttribute(_T("rel"), 2, &vRel))){
                    std::string rel(convertor.to_bytes(vRel.bstrVal));
                    //rel.MakeLower();
                    if((rel == "shortcut icon") || (rel == "icon")){
                        CComVariant vHref;
                        if(SUCCEEDED(pElem->getAttribute(_T("href"), 2, &vHref))){
                            favurl = convertor.to_bytes(vHref.bstrVal);
                            setIcon(favurl);
                        }
                    }
                } else{
                }
            }
        }
    }


    inline void DocumentComplete(const wchar_t* /*url*/){
        TRACER1("DocumentComplete");
        CComPtr<IHTMLDocument2> doc = GetDoc();
        if(doc == NULL){
            throw s::wui::exception(std::string("Invalid document state:") + GetLastErrorAsString());
        }
        getFaviconURLFromContent();
        SetFocus();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_IUnknown) { *ppv = (IOleClientSite*)this; AddRef(); return S_OK; }
        if (riid == IID_IOleClientSite) { *ppv = (IOleClientSite*)this; AddRef(); return S_OK; }
        if (riid == IID_IOleWindow || riid == IID_IOleInPlaceSite) { *ppv = (IOleInPlaceSite*)this; AddRef(); return S_OK; }
        if (riid == IID_IOleInPlaceUIWindow || riid == IID_IOleInPlaceFrame) { *ppv = (IOleInPlaceFrame*)this; AddRef(); return S_OK; }
        if (riid == IID_IDispatch) { *ppv = (IDispatch*)this; AddRef(); return S_OK; }
        if (riid == IID_IDocHostUIHandler) { *ppv = (IDocHostUIHandler*)this; AddRef(); return S_OK; }
        if (riid == IID_IInternetProtocolInfo) { *ppv = (IInternetProtocolInfo*)this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() {
        //TRACER("window::Impl::AddRef");
        return InterlockedIncrement(&ref);
    }

    ULONG STDMETHODCALLTYPE Release() {
        //TRACER("window::Impl::Release");
        int tmp = InterlockedDecrement(&ref);
        assert(tmp >= 0);
        if (tmp == 0) {
            //delete this;
        }
        return tmp;
    }

    // IOleClientSite
    HRESULT STDMETHODCALLTYPE SaveObject(){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD /*dwAssign*/, DWORD /*dwWhichMoniker*/, IMoniker** /*ppmk*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer **ppContainer){ *ppContainer = 0; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE ShowObject(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL /*fShow*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout(){ return E_NOTIMPL; }

    // IOleWindow
    HRESULT STDMETHODCALLTYPE GetWindow(HWND *phwnd){ *phwnd = hhost; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL /*fEnterMode*/){ return E_NOTIMPL; }
    // IOleInPlaceSite
    HRESULT STDMETHODCALLTYPE CanInPlaceActivate(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceActivate(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUIActivate(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO info){
        *ppFrame = this;
        AddRef();
        *ppDoc = 0;
        info->fMDIApp = FALSE; info->hwndFrame = hhost; info->haccel = 0; info->cAccelEntries = 0;
        GetClientRect(hhost, lprcPosRect);
        GetClientRect(hhost, lprcClipRect);
        return(S_OK);
    }
    HRESULT STDMETHODCALLTYPE Scroll(SIZE /*scrollExtant*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL /*fUndoable*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE DiscardUndoState(){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeactivateAndUndo(){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT lprcPosRect){
        IOleInPlaceObject *iole = 0;
        ibrowser->QueryInterface(IID_IOleInPlaceObject, (void**)&iole);
        if(iole != 0){ iole->SetObjectRects(lprcPosRect, lprcPosRect); iole->Release(); }
        return S_OK;
    }

    // IOleInPlaceUIWindow
    HRESULT STDMETHODCALLTYPE GetBorder(LPRECT /*lprectBorder*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS /*pborderwidths*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS /*pborderwidths*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject* /*pActiveObject*/, LPCOLESTR /*pszObjName*/){ return S_OK; }
    // IOleInPlaceFrame
    HRESULT STDMETHODCALLTYPE InsertMenus(HMENU /*hmenuShared*/, LPOLEMENUGROUPWIDTHS /*lpMenuWidths*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMenu(HMENU /*hmenuShared*/, HOLEMENU /*holemenu*/, HWND /*hwndActiveObject*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU /*hmenuShared*/){ return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR /*pszStatusText*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE IOleInPlaceFrame::EnableModeless(BOOL /*fEnable*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG /*lpmsg*/, WORD /*wID*/){ return E_NOTIMPL; }

    // IDispatch
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *pctinfo){ *pctinfo = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT /*iTInfo*/, LCID /*lcid*/, ITypeInfo** /*ppTInfo*/){ return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID /*riid*/, LPOLESTR* /*rgszNames*/, UINT /*cNames*/, LCID /*lcid*/, DISPID* /*rgDispId*/){ return E_FAIL; }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID /*riid*/, LCID /*lcid*/, WORD /*wFlags*/, DISPPARAMS* Params, VARIANT* pVarResult, EXCEPINFO* /*pExcepInfo*/, UINT* /*puArgErr*/){
        switch(dispIdMember){ // DWebBrowserEvents2
        case DISPID_BEFORENAVIGATE2:
            BeforeNavigate2(Params->rgvarg[5].pvarVal->bstrVal, Params->rgvarg[0].pboolVal);
            break;
        case DISPID_NAVIGATECOMPLETE2:
            NavigateComplete2(Params->rgvarg[0].pvarVal->bstrVal);
            break;
        case DISPID_DOCUMENTCOMPLETE:
            DocumentComplete(Params->rgvarg[0].pvarVal->bstrVal);
            break;
        case DISPID_AMBIENT_DLCONTROL:
        {
            pVarResult->vt = VT_I4;
            pVarResult->lVal = DLCTL_DLIMAGES | DLCTL_VIDEOS | DLCTL_BGSOUNDS | DLCTL_SILENT;
        }

        default:
            return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }

    // IDocHostUIHandler
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD /*dwID*/, POINT* /*ppt*/, IUnknown* /*pcmdtReserved*/, IDispatch* /*pdispReserved*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO *pInfo){ pInfo->dwFlags = (hasscrollbars ? 0 : DOCHOSTUIFLAG_SCROLL_NO) | DOCHOSTUIFLAG_NO3DOUTERBORDER; return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD /*dwID*/, IOleInPlaceActiveObject* /*pActiveObject*/, IOleCommandTarget* /*pCommandTarget*/, IOleInPlaceFrame* /*pFrame*/, IOleInPlaceUIWindow* /*pDoc*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE HideUI(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateUI(){ return S_OK; }
    HRESULT STDMETHODCALLTYPE IDocHostUIHandler::EnableModeless(BOOL /*fEnable*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL /*fActivate*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL /*fActivate*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT /*prcBorder*/, IOleInPlaceUIWindow* /*pUIWindow*/, BOOL /*fRameWindow*/){ return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG /*lpMsg*/, const GUID* /*pguidCmdGroup*/, DWORD /*nCmdID*/){ return S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR* /*pchKey*/, DWORD /*dw*/){ return S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget* /*pDropTarget*/, IDropTarget** /*ppDropTarget*/){ return S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** ppDispatch){ *ppDispatch = 0; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD /*dwTranslate*/, OLECHAR* /*pchURLIn*/, OLECHAR** ppchURLOut){ *ppchURLOut = 0; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject* /*pDO*/, IDataObject** ppDORet){ *ppDORet = 0; return S_FALSE; }

    // IInternetProtocolInfo
    STDMETHODIMP ParseUrl(LPCWSTR /*pwzUrl*/, PARSEACTION /*parseAction*/, DWORD /*dwParseFlags*/,
                          LPWSTR /*pwzResult*/, DWORD /*cchResult*/, DWORD* /*pcchResult*/, DWORD /*dwReserved*/){
        return INET_E_DEFAULT_ACTION;
    }

    STDMETHODIMP CombineUrl(LPCWSTR /*pwzBaseUrl*/, LPCWSTR /*pwzRelativeUrl*/,
                            DWORD /*dwCombineFlags*/, LPWSTR /*pwzResult*/, DWORD /*cchResult*/, DWORD* /*pcchResult*/,
                            DWORD /*dwReserved*/){
        return INET_E_DEFAULT_ACTION;
    }

    STDMETHODIMP CompareUrl(LPCWSTR /*pwzUrl1*/, LPCWSTR /*pwzUrl2*/, DWORD /*dwCompareFlags*/){
        return INET_E_DEFAULT_ACTION;
    }

    STDMETHODIMP QueryInfo(LPCWSTR /*pwzUrl*/, QUERYOPTION /*queryOption*/, DWORD /*dwQueryFlags*/,
                           LPVOID /*pBuffer*/, DWORD /*cbBuffer*/, DWORD* /*pcbBuf*/, DWORD /*dwReserved*/){
        return INET_E_DEFAULT_ACTION;
    }

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObject){
        TRACER("TInternetProtocolFactory::CreateInstance");
        if(pUnkOuter != nullptr)
            return CLASS_E_NOAGGREGATION;
        if(riid == IID_IInternetProtocol){
            CComPtr<IInternetProtocol> proto(new TInternetProtocol(*this));
            return proto->QueryInterface(riid, ppvObject);
        }
        if(riid == IID_IInternetProtocolInfo){
            return QueryInterface(riid, ppvObject);
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP LockServer(BOOL /*fLock*/){ return S_OK; }

    struct TInternetProtocol :public IInternetProtocol
    {
        TInternetProtocol(Impl& impl) : impl_(impl), refCount(1), data(nullptr), dataLen(0), dataCurrPos(0) { }
        virtual ~TInternetProtocol() { }

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) {
            static const QITAB qit[] = {
                QITABENT(TInternetProtocol, IInternetProtocol),
                QITABENT(TInternetProtocol, IInternetProtocolRoot),
                { 0 }
            };
            return QISearch(this, qit, riid, ppvObject);
        }

        ULONG STDMETHODCALLTYPE AddRef() { return InterlockedIncrement(&refCount); }
        ULONG STDMETHODCALLTYPE Release() {
            TRACER("TInternetProtocol::Release");
            LONG res = InterlockedDecrement(&refCount);
            assert(res >= 0);
            if (0 == res)
                delete this;
            return res;
        }

        // IInternetProtocol
        STDMETHODIMP Start(
            LPCWSTR szUrl,
            IInternetProtocolSink* pIProtSink,
            IInternetBindInfo* /*pIBindInfo*/,
            DWORD /*grfSTI*/,
            HANDLE_PTR /*dwReserved*/)
        {
            TRACER("TInternetProtocol::Start");
            std::string url(impl_.convertor.to_bytes(szUrl));
            auto& pdata = impl_.getEmbeddedSource(url);
            data = std::get<0>(pdata);
            dataLen = std::get<1>(pdata);
            auto& mimetype = std::get<2>(pdata);
            s::js::unused(mimetype);
            dataCurrPos = 0;

            pIProtSink->ReportProgress(BINDSTATUS_FINDINGRESOURCE, L"");
            pIProtSink->ReportProgress(BINDSTATUS_CONNECTING, L"");
            pIProtSink->ReportProgress(BINDSTATUS_SENDINGREQUEST, L"");
            pIProtSink->ReportData(BSCF_FIRSTDATANOTIFICATION | BSCF_LASTDATANOTIFICATION | BSCF_DATAFULLYAVAILABLE, (ULONG)dataLen, (ULONG)dataLen);
            pIProtSink->ReportResult(S_OK, 200, nullptr);
            return S_OK;
        }

        STDMETHODIMP Continue(PROTOCOLDATA* /*pStateInfo*/) { return S_OK; }
        STDMETHODIMP Abort(HRESULT /*hrReason*/, DWORD /*dwOptions*/) { return S_OK; }
        STDMETHODIMP Terminate(DWORD /*dwOptions*/) { return S_OK; }
        STDMETHODIMP Suspend() { return E_NOTIMPL; }
        STDMETHODIMP Resume() { return E_NOTIMPL; }
        STDMETHODIMP Read(void *pv, ULONG cb, ULONG *pcbRead) {
            TRACER("TInternetProtocol::Read");
            //std::cout << "TInternetProtocol::Read:len:" << dataLen << ":" << dataCurrPos << std::endl;
            if (!data)
                return S_FALSE;
            size_t dataAvail = dataLen - dataCurrPos;
            if (0 == dataAvail)
                return S_FALSE;
            ULONG toRead = cb;
            if (toRead > dataAvail)
                toRead = (ULONG)dataAvail;
            const unsigned char *dataToRead = data + dataCurrPos;
            memcpy(pv, dataToRead, toRead);
            dataCurrPos += toRead;
            *pcbRead = toRead;
            return S_OK;
        }
        STDMETHODIMP Seek(LARGE_INTEGER /*dlibMove*/, DWORD /*dwOrigin*/, ULARGE_INTEGER* /*plibNewPosition*/) {
            TRACER("TInternetProtocol::Seek");
            // doesn't seem to be called
            return E_NOTIMPL;
        }
        STDMETHODIMP LockRequest(DWORD /*dwOptions*/) { return S_OK; }
        STDMETHODIMP UnlockRequest() { return S_OK; }
    protected:
        Impl& impl_;
        LONG refCount;

        // those are filled in Start() and represent data to be sent
        // for a given url
        const unsigned char* data;
        size_t dataLen;
        size_t dataCurrPos;
    };

    inline Impl(s::wui::window& w) : wb_(w){
        TRACER("window::Impl::Impl");
        ref = 0;
        threadID_ = std::this_thread::get_id();
        this->hhost = 0;
        ibrowser = 0;
        cookie = 0;
        wlist.push_back(this);
        RegisterInternetProtocolFactory();
    }

    inline ~Impl(){
        TRACER("window::Impl::~Impl");

        // \todo: sometimes one final IOleClientSite::Release() does not get called
        assert(ref <= 1);
        // e.g: when http://www.google.com is called

        for(auto it = wlist.begin(), ite = wlist.end(); it != ite; ++it){
            if(*it == this){
                wlist.erase(it);
                break;
            }
        }
        UnregisterInternetProtocolFactory();
    }

    static std::vector<Impl*> wlist;

    s::wui::window& wb_;
    HWND hhost;               // This is the window that hosts us
    IWebBrowser2 *ibrowser;   // Our pointer to the browser itself. Released in Close().
    DWORD cookie;             // By this cookie shall the watcher be known
                              //
    bool hasscrollbars;       // This is read from WS_VSCROLL|WS_HSCROLL at WM_CREATE
    std::string curl;         // This was the url that the user just clicked on
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
};

std::vector<s::wui::window::Impl*> s::wui::window::Impl::wlist;

class s::application::Impl{
    s::application& app;
public:
    inline Impl(s::application& a) : app(a){
        ::_tzset();
        if(::OleInitialize(NULL) != S_OK){
            throw s::wui::exception(std::string("OleInitialize() failed:") + GetLastErrorAsString());
        }

        char apath[MAX_PATH];
        DWORD rv = ::GetModuleFileNameA(NULL, apath, MAX_PATH);
        if(rv == 0){
            DWORD ec = GetLastError();
            assert(ec != ERROR_SUCCESS);
            throw s::wui::exception(std::string("Internal error retrieving process path:") + GetLastErrorAsString());
        }

        app.path = apath;
        char *ptr = strrchr(apath, '\\');
        if(ptr != NULL)
            strcpy_s(apath, MAX_PATH, ptr + 1);

        app.name = apath;
    }

    inline ~Impl(){
        ::OleUninitialize();
    }

    inline int loop(){
        if(s::app().onInit){
            s::app().onInit();
        }

        //auto hInstance = ::GetModuleHandle(NULL);
        //HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_APP));
        HACCEL hAccelTable = 0;

        // Main message loop
        MSG msg;
        while(::GetMessage(&msg, NULL, 0, 0) != 0){
            if(!::TranslateAccelerator(msg.hwnd, hAccelTable, &msg)){
                bool done = false;
                for(auto w : s::wui::window::Impl::wlist){
                    done = w->hookMessage(msg);
                    if(done){
                        break;
                    }
                }
                if(!done){
                    ::TranslateMessage(&msg);
                    ::DispatchMessage(&msg);
                }
            }
        }
        return 0;
    }

    inline void exit(const int& exitcode){
        ::PostQuitMessage(exitcode);
    }

    inline std::string datadir(const std::string& /*an*/) const{
        char chPath[MAX_PATH];
        /// \todo Use SHGetKnownFolderPath for vista and later.
        HRESULT hr = ::SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, chPath);
        if(!SUCCEEDED(hr)){
            throw s::wui::exception(std::string("Internal error retrieving data directory:") + GetErrorAsString(hr));
        }
        std::string data(chPath);
        std::replace(data.begin(), data.end(), '\\', '/');
        return data;
    }
};


#endif // #ifdef WUI_WIN

#ifdef WUI_NDK
////////////////////////////////////

#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, _s_tag.c_str(), __VA_ARGS__)
namespace {
    static std::string _s_tag = "WUILOG";

    static JavaVM* _s_jvm = nullptr;
    static std::string _s_datadir;
    static ::jobject _s_activity = 0;
    static ::jclass _s_activityCls = 0;
    static ::jobject _s_assetManager = 0;
    static ::jmethodID _s_setObjectFn = 0;
    static ::jmethodID _s_goEmbeddedFn = 0;
    static ::jmethodID _s_goStandardFn = 0;
    static s::application::Impl* _s_impl = nullptr;
    static s::wui::window::Impl* _s_wimpl = nullptr;

    struct JniEnvGuard {
        JNIEnv* env;
        inline JniEnvGuard() : env(0) {
            _s_jvm->AttachCurrentThread(&env, NULL);
            assert(env != 0);
        }
        inline ~JniEnvGuard(){
            _s_jvm->DetachCurrentThread();
        }
    };

    inline auto convertJavaArrayToVector(JNIEnv* env, jobjectArray jparams){
        std::vector<std::string> params;
        int stringCount = env->GetArrayLength(jparams);
        for (int i=0; i<stringCount; i++) {
            jstring string = (jstring) env->GetObjectArrayElement(jparams, i);
            const char *rawString = env->GetStringUTFChars(string, 0);
            params.push_back(rawString);
            env->ReleaseStringUTFChars(string, rawString);
        }
        return params;
    }

    struct JniStringGuard {
        JNIEnv* env;
        jstring jobj;
        const char *str;
        inline JniStringGuard(JNIEnv* e, jstring o){
            env = e;
            jobj = o;
            str = env->GetStringUTFChars(jobj, 0);
        }

        inline ~JniStringGuard(){
            env->ReleaseStringUTFChars(jobj, str);
        }
    };

    inline std::string convertJniStringToStdString(JNIEnv* env, jstring jstr){
        JniStringGuard sg(env, jstr);
        return sg.str;
    }

    inline jstring convertStdStringToJniString(JNIEnv* env, const std::string& str){
        return env->NewStringUTF(str.c_str());
    }
}

class s::wui::window::Impl {
    s::wui::window& wb;
    ContentSourceData csd;
public:
    inline Impl(s::wui::window& w) : wb(w) {
        assert(_s_wimpl == nullptr);
        _s_wimpl = this;
    }

    inline ~Impl(){
        _s_wimpl = nullptr;
    }

    inline void onLoad(const std::string& url) {
        //ALOG("onLoad:%s", url.c_str());

        if(url.compare(0, jspfx.length(), jspfx) == 0){
            return;
        }
        if(wb.onLoad){
            wb.onLoad(url);
        }
    }

    inline void onInitPage(const std::string& url) {
        if(url.compare(0, jspfx.length(), jspfx) == 0){
            return;
        }
        addCommonPage(wb);
    }

    inline void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>>& lst) {
        csd.setEmbeddedSource(lst);
    }

    inline void setContentSourceResource(const std::string& path) {
        csd.setResourceSource(path);
    }

    inline const auto& getEmbeddedSource(const std::string& url) {
        //ALOG("getEmbeddedSource:%s", url.c_str());
        return csd.getEmbeddedSource(url);
    }

    inline bool open(const int& left, const int& top, const int& width, const int& height) {
        if(wb.onOpen){
            wb.onOpen();
        }
        return true;
    }

    inline void setDefaultMenu() {
    }

    inline void setMenu(const std::string& path, const std::string& name, const std::string& key, std::function<void()> cb) {
    }

    inline std::string invoke(const std::string& obj, const std::string& fn, const std::vector<std::string>& params) {
        auto& jo = wb.getObject(obj);
        auto rv = jo.invoke(fn, params);
        return rv;
    }

    inline void addNativeObject(s::js::objectbase& jo, const std::string& body) {
        //ALOG("addNativeObject:%s", jo.name.c_str());
        auto fbody = jspfx + body;
        JniEnvGuard envg;
        jstring jname = envg.env->NewStringUTF(jo.name.c_str());
        jstring jnname = envg.env->NewStringUTF(jo.nname.c_str());
        jstring jbody = envg.env->NewStringUTF(fbody.c_str());
        envg.env->CallVoidMethod(_s_activity, _s_setObjectFn, jname, jnname, jbody);
    }

    inline void eval(const std::string& str) {
        auto jstr = jspfx + str;
        JniEnvGuard envg;
        jstring jdata = envg.env->NewStringUTF(jstr.c_str());
        envg.env->CallVoidMethod(_s_activity, _s_goStandardFn, jdata);
    }

    inline void go(const std::string& urlx) {
        auto url = csd.normaliseUrl(urlx);
        try{
            onLoad(url);
        }catch(const std::exception& ex){
            ALOG("onLoad-error:%s", ex.what());
        }
        JniEnvGuard envg;
        //ALOG("Loading:%s", url.c_str());
        if(csd.type == s::wui::ContentSourceType::Embedded){
            auto& data = getEmbeddedSource(url);
            jstring jurl = envg.env->NewStringUTF(url.c_str());
            jstring jstr = envg.env->NewStringUTF((const char*)std::get<0>(data));
            jstring jmimetype = envg.env->NewStringUTF(std::get<2>(data).c_str());
            envg.env->CallVoidMethod(_s_activity, _s_goEmbeddedFn, jurl, jstr, jmimetype);
        }else if(csd.type == s::wui::ContentSourceType::Resource){
            jstring jurl = envg.env->NewStringUTF(url.c_str());
            envg.env->CallVoidMethod(_s_activity, _s_goStandardFn, jurl);
        }
        //ALOG("Loaded:%s", url.c_str());
    }
};

class s::application::Impl {
    s::application& app;
    bool done;
    std::condition_variable cv_;
    std::mutex mxq_;
    std::queue<std::function<void()>> mq_;
public:
    inline Impl(s::application& a) : app(a), done(false) {
        assert(_s_impl == nullptr);
        _s_impl = this;
    }

    inline ~Impl() {
        _s_impl = nullptr;
    }

    inline void exit(const int& exitcode) {
        ALOG("exiting loop");
        done = true;
    }

    inline std::string datadir(const std::string& /*an*/) const {
        return _s_datadir;
    }

    inline void post(std::function<void()> fn) {
        // set done to true
        {
            std::lock_guard<std::mutex> lk(mxq_);
            mq_.push(fn);
        }
        // notify loop
        cv_.notify_one();
    }

    inline auto wait() {
        std::unique_lock<std::mutex> lk(mxq_);
        while(mq_.size() == 0){
            cv_.wait(lk);
        }
        auto fn = mq_.front();
        mq_.pop();
        return fn;
    }

    inline int loop() {
        // wait for done to be true
        ALOG("enter loop");
        while(!done){
            auto fn = wait();
            try {
                fn();
            }catch(const std::exception& ex){
                ALOG("task-error:%s", ex.what());
            }catch(...){
                ALOG("task-error:<unknown>");
            }
        }
        ALOG("leave loop");
        return 0;
    }
};

extern "C" {
    int main(int argc, const char* argv[]);
}

namespace {
    std::unique_ptr<std::thread> thrd;
    void mainx(std::vector<std::string> params) {
        std::vector<const char*> args(params.size());
        size_t idx = 0;
        for(auto& p : params){
            args[idx++] = p.c_str();
        }
        try{
            main(args.size(), &args.front());
        }catch(const std::exception& ex){
            ALOG("error in main worker thread:%s", ex.what());
        }
    }
}

extern "C" {
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* aReserved) {
        _s_jvm = vm;
        return JNI_VERSION_1_6;
    }

    JNIEXPORT void JNICALL Java_com_renjipanicker_wui_initNative(JNIEnv* env, jobject activity, jstring jtag, jobjectArray jparams, jobject assetManager) {
        jclass activityCls = env->FindClass("com/renjipanicker/wui");
        if (!activityCls) {
            throw s::wui::exception(std::string("unable to obtain wui class"));
        }

        _s_activityCls = reinterpret_cast<jclass>(env->NewGlobalRef(activityCls));

        _s_setObjectFn = env->GetMethodID(_s_activityCls, "setObject", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (!_s_setObjectFn) {
            throw s::wui::exception(std::string("unable to obtain wui::setObject"));
        }

        _s_goEmbeddedFn = env->GetMethodID(_s_activityCls, "go_embedded", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (!_s_goEmbeddedFn) {
            throw s::wui::exception(std::string("unable to obtain wui::go_embedded"));
        }

        _s_goStandardFn = env->GetMethodID(_s_activityCls, "go_standard", "(Ljava/lang/String;)V");
        if (!_s_goStandardFn) {
            throw s::wui::exception(std::string("unable to obtain wui::go_standard"));
        }

        _s_tag = convertJniStringToStdString(env, jtag);
        _s_activity = reinterpret_cast<jobject>(env->NewGlobalRef(activity));
        _s_assetManager = reinterpret_cast<jobject>(env->NewGlobalRef(assetManager));

        auto params = convertJavaArrayToVector(env, jparams);
        thrd = std::make_unique<std::thread>(mainx, params);
    }

    JNIEXPORT void JNICALL Java_com_renjipanicker_wui_initWindow(JNIEnv* env, jobject activity) {
        if(_s_impl != nullptr){
            _s_impl->post([](){
                if(s::app().onInit){
                    s::app().onInit();
                }
            });
        }
    }

    JNIEXPORT void JNICALL Java_com_renjipanicker_wui_initPage(JNIEnv* env, jobject activity, jstring jurl) {
        const std::string url = convertJniStringToStdString(env, jurl);
        if(_s_impl != nullptr){
            _s_impl->post([url](){
                if(_s_wimpl != nullptr){
                    _s_wimpl->onInitPage(url);
                }
            });
        }
    }

    JNIEXPORT void JNICALL Java_com_renjipanicker_wui_exitNative(JNIEnv* env, jobject activity) {
        if(_s_impl != nullptr){
            _s_impl->post([](){
                _s_impl->exit(0);
            });
        }
    }

    JNIEXPORT jstring JNICALL Java_com_renjipanicker_wui_invokeNative(JNIEnv* env, jobject activity, jstring jobj, jstring jfn, jobjectArray jparams) {
        const std::string obj = convertJniStringToStdString(env, jobj);
        const std::string fn = convertJniStringToStdString(env, jfn);
        std::string rv = "";

        auto params = convertJavaArrayToVector(env, jparams);
        if(_s_impl != nullptr){
            std::mutex m;
            std::condition_variable cv;
            std::unique_lock<std::mutex> lk(m);

            _s_impl->post([obj, fn, params, &rv, &cv, &m](){
                std::lock_guard<std::mutex> lk(m);
                if(_s_wimpl != nullptr){
                    rv = _s_wimpl->invoke(obj, fn, params);
                }
                cv.notify_one();
            });

            cv.wait(lk);
        }
        return convertStdStringToJniString(env, rv);
    }

    JNIEXPORT jobjectArray JNICALL Java_com_renjipanicker_wui_getPageData(JNIEnv* env, jobject activity, jstring jurl) {
        const std::string url = convertJniStringToStdString(env, jurl);
        if(_s_wimpl == nullptr){
            return 0;
        }

        auto& data = _s_wimpl->getEmbeddedSource(url);
        jstring jmimetype = env->NewStringUTF(std::get<2>(data).c_str());
        auto len = std::get<1>(data);
        auto bin = std::get<3>(data);
        jstring jenc;
        if(bin){
            jenc = env->NewStringUTF("binary");
        }else{
            jenc = env->NewStringUTF("UTF-8");
        }
        ALOG("GetPageData:Loading:%s(%u)", url.c_str(), len);
        jbyteArray jstr = env->NewByteArray(len);
        env->SetByteArrayRegion(jstr, 0, len, (const jbyte*)std::get<0>(data));

        jobjectArray ret = (jobjectArray)env->NewObjectArray(3, env->FindClass("java/lang/Object"), 0);
        env->SetObjectArrayElement(ret,0,jmimetype);
        env->SetObjectArrayElement(ret,1,jstr);
        env->SetObjectArrayElement(ret,2,jenc);

        ALOG("GetPageData:Loaded:%s(%u)", url.c_str(), len);
        return ret;
    }

} // extern "C"

#endif // WUI_NDK

#ifdef WUI_LINUX
class s::wui::window::Impl {
    s::wui::window& wb;
public:
    inline Impl(s::wui::window& w) : wb(w) {
    }

    inline bool open(const int& left, const int& top, const int& width, const int& height) {
        return false;
    }

    inline void setDefaultMenu() {
    }

    inline void go(const std::string& url) {
    }

    inline void eval(const std::string& str) {
    }
};

class s::application::Impl {
    s::application& app;
public:
    inline Impl(s::application& a) : app(a) {
    }
    inline ~Impl() {
    }
    inline int loop() {
        return 0;
    }
    inline void exit(const int& exitcode) {
    }

    inline std::string datadir(const std::string& an) const {
    }
};

#endif // WUI_LINUX

s::wui::window::window() {
    impl_ = std::make_unique<Impl>(*this);
}

s::wui::window::~window() {
}

void s::wui::window::setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>>& lst) {
    return impl_->setContentSourceEmbedded(lst);
}

void s::wui::window::setContentSourceResource(const std::string& path) {
    return impl_->setContentSourceResource(path);
}

bool s::wui::window::open(const int& left, const int& top, const int& width, const int& height) {
    return impl_->open(left, top, width, height);
}

void s::wui::window::setDefaultMenu() {
    impl_->setDefaultMenu();
}

void s::wui::window::setMenu(const std::string& path, const std::string& name, const std::string& key, std::function<void()> cb) {
    impl_->setMenu(path, name, key, cb);
}

void s::wui::window::go(const std::string& url) {
    impl_->go(url);
}

void s::wui::window::eval(const std::string& str) {
    impl_->eval(str);
}

void s::wui::window::addNativeObject(s::js::objectbase& jo, const std::string& body) {
    impl_->addNativeObject(jo, body);
}

////////////////////////////
std::vector<std::string> s::asset::listFiles(const std::string& src) {
#if defined(WUI_NDK)
    struct dir{
        JniEnvGuard envg;
        AAssetManager* mgr;
        AAssetDir* assetDir;
        inline dir(const std::string& filename){
            mgr = AAssetManager_fromJava(envg.env, _s_assetManager);
            assetDir = AAssetManager_openDir(mgr, filename.c_str());
        }

        inline auto list(){
            std::vector<std::string> rv;
            const char* filename = 0;
            while ((filename = AAssetDir_getNextFileName(assetDir)) != NULL) {
                rv.push_back(filename);
            }
            return rv;
        }
        inline ~dir(){
            AAssetDir_close(assetDir);
        }
    };

    dir onx(src);
    return onx.list();
#else
    s::js::unused(src);
#endif
    std::vector<std::string> rv;
    return rv;
}

void s::asset::readFile(const std::string& filename, std::function<bool(const char*, const size_t&)> fn) {
#if defined(WUI_NDK)
    file onx(filename);
    if(!onx){
        return;
    }
    onx.readAll(fn);
#else
    s::js::unused(filename);
    s::js::unused(fn);
#endif
}

#if defined(WUI_NDK)
struct s::asset::file::Impl {
    JniEnvGuard envg;
    AAssetManager* mgr;
    AAsset* asset;
    inline Impl(const std::string& filename) {
        mgr = AAssetManager_fromJava(envg.env, _s_assetManager);
        asset = AAssetManager_open(mgr, filename.c_str(), AASSET_MODE_STREAMING);
    }

    inline int read(char* buf, const size_t& len) {
        return AAsset_read(asset, buf, len);
    }

    inline void readAll(std::function<bool(const char*, const size_t&)>& fn){
        char buf[BUFSIZ];
        int nb_read = 0;
        while ((nb_read = read(buf, BUFSIZ)) > 0){
            fn(buf, nb_read);
        }
    }

    inline bool valid() const {
        return (asset != 0);
    }

    inline ~Impl() {
        AAsset_close(asset);
    }
};
#else
struct s::asset::file::Impl {
};
#endif

s::asset::file::file(const std::string& filename, std::ios_base::openmode) {
#if defined(WUI_NDK)
    impl_ = std::make_unique<Impl>(filename);
#else
    s::js::unused(filename);
#endif
}

s::asset::file::~file() {
}

int s::asset::file::read(char* buf, const size_t& len) {
#if defined(WUI_NDK)
    return impl_->read(buf, len);
#else
    s::js::unused(buf);
    s::js::unused(len);
    return -1;
#endif
}

void s::asset::file::readAll(std::function<bool(const char*, const size_t&)>& fn) {
#if defined(WUI_NDK)
    return impl_->readAll(fn);
#else
    s::js::unused(fn);
#endif
}

bool s::asset::file::valid() const {
#if defined(WUI_NDK)
    return impl_->valid();
#else
    return false;
#endif
}

////////////////////////////
namespace {
    s::application* s_app = nullptr;
}
s::application::application(int c, const char** v, const std::string& t) : argc(c), argv(v), title(t) {
    assert(s_app == nullptr);
    s_app = this;
    impl_ = std::make_unique<Impl>(*this);
}

s::application::~application() {
    s_app = nullptr;
}

int s::application::loop() {
    return impl_->loop();
}

void s::application::exit(const int& exitcode) const {
    return impl_->exit(exitcode);
}

std::string s::application::datadir(const std::string& an) const {
    return impl_->datadir(an);
}

const s::application& s::app() {
    return *s_app;
}
