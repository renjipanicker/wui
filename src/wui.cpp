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

namespace {
    const std::string jspfx = "javascript:";
    const std::string empfx = "embedded:";
    struct ContentSourceData {
        s::wui::ContentSourceType type;
        std::string path;
        std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>> const* lst;
        inline ContentSourceData() : type(s::wui::ContentSourceType::Standard), lst(nullptr) {}

        /// \brief set embedded source
        inline void setEmbeddedSource(std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>> const& l) {
            type = s::wui::ContentSourceType::Embedded;
            lst = &l;
        }

        /// \brief set resource source
        inline void setResourceSource(const std::string& p) {
            type = s::wui::ContentSourceType::Resource;
            path = p;
        }

        inline const auto& getEmbeddedSource(const std::string& url) {
            assert(type == s::wui::ContentSourceType::Embedded);
            assert(lst != nullptr);
            auto fit = lst->find(url);
            if(fit == lst->end()){
                throw std::runtime_error(std::string("unknown url:") + url);
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
        "  nval = eval(val);\n"
        "  return nval;\n"
        "}\n"
        ;
        wb.eval(initstr);
    }
}

inline s::wui::window::Impl& s::wui::window::impl() {
    return *impl_;
}

#ifdef WUI_OSX
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

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
@interface AppDelegate : NSObject<NSApplicationDelegate, WebFrameLoadDelegate, WebResourceLoadDelegate, WebUIDelegate, WebPolicyDelegate>{
@public
    s::wui::window* wb_;
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

    inline void exit(const int& exitcode) {
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
class s::wui::window::Impl {
    s::wui::window& wb;
    NSWindow* window;
    WebView* webView;
    AppDelegate* wd;
    ContentSourceData csd;
public:
    inline Impl(s::wui::window& w) : wb(w), window(nullptr), webView(nullptr), wd(nullptr) {
    }

    inline void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>>& lst) {
        csd.setEmbeddedSource(lst);
    }

    inline void setContentSourceResource(const std::string& path) {
        csd.setResourceSource(path);
    }

    inline const auto& getEmbeddedSource(const std::string& url) {
        return csd.getEmbeddedSource(url);
    }

    inline NSMenuItem* createMenuItem(id menu, NSString* title, NSString* keyEq, std::function<void()> fn = std::function<void()>()) {
        NSMenuItem* menuItem = [[NSMenuItem alloc] init];
        [menuItem setTitle:title];
        if(keyEq != 0){
            [menuItem setKeyEquivalent:keyEq];
        }
        if(fn){
            [menuItem setBlockAction:^(id sender) {
                fn();
            }];
        }
        [menu addItem : menuItem];
        return menuItem;
    }

    inline NSMenu* createMenu(id menubar, NSString* title) {
        auto menuItem = createMenuItem(menubar, title, 0);
        NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
        [menuItem setSubmenu : menu];
        return menu;
    }

    inline void setDefaultMenu() {
        auto& wb = this->wb;
        id appName = [[NSProcessInfo processInfo] processName];

        NSMenu* menubar = [[NSMenu alloc] initWithTitle:@"Filex"];
        [NSApp setMainMenu : menubar];

        NSMenu* appMenu = createMenu(menubar, @"<AppName>"); // OSX always uses the actual app name here

        NSString* aboutTitle = [@"About " stringByAppendingString:appName];
        createMenuItem(appMenu, aboutTitle, 0);

        NSString* quitTitle = [@"Quit " stringByAppendingString:appName];
        createMenuItem(appMenu, quitTitle, @"q", [](){
            [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:0.0];
        });


        NSMenu* fileMenu = createMenu(menubar, @"File");
        createMenuItem(fileMenu, @"New", @"n", [](){
        });

        createMenuItem(fileMenu, @"Open", @"o");
        createMenuItem(fileMenu, @"Close", @"w");
        createMenuItem(fileMenu, @"Save", @"s", [&wb](){
            if(wb.onSaveFile){
                wb.onSaveFile();
            }
        });

        NSMenu* editMenu = createMenu(menubar, @"Edit");
        createMenuItem(editMenu, @"Undo", @"z");
        createMenuItem(editMenu, @"Redo", @"Z");
        createMenuItem(editMenu, @"Cut", @"x");
        createMenuItem(editMenu, @"Copy", @"c");
        createMenuItem(editMenu, @"Paste", @"v");

        NSMenu* helpMenu = createMenu(menubar, @"Help");
        createMenuItem(helpMenu, @"Help", @"/");
    }

    inline void setMenu(const menu&) {
    }

    inline void setAlwaysOnTop(const bool& aot) {
        if(aot){
            [window setLevel:NSStatusWindowLevel];
        }else{
            // \todo: remove AOT
        }
    }

    inline bool open() {
        // get singleton app instance
        NSApplication *app = [NSApplication sharedApplication];
        // create WebView instance
        NSRect frame = NSMakeRect(100, 0, 400, 800);
        webView = [[WebView alloc] initWithFrame:frame frameName : @"myWV" groupName : @"webViews"];

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

#ifdef NO_NIB
        setDefaultMenu();
#endif

        assert(wd != 0);
        [webView setWantsLayer : YES];
        [webView setCanDrawConcurrently : YES];
        [webView setFrameLoadDelegate : wd];
        [webView setUIDelegate : wd];
        [webView setResourceLoadDelegate : wd];
        [webView setPolicyDelegate : wd];

        BOOL _allowKeyCapture = TRUE;
        [NSEvent addLocalMonitorForEventsMatchingMask : NSKeyDownMask
                                              handler : ^ NSEvent * (NSEvent * event) {
                                                  if (!_allowKeyCapture) {
                                                      [window makeFirstResponder : window.contentView];
                                                  }
                                                  return event;
                                              }];

        // set webView as content of top-level window
        assert(window != nullptr);
        [window setContentView : webView];

        // bring window to front(required only when launching binary executable from command line)
        [NSApp activateIgnoringOtherApps:YES];

        // intialize AppDelegate
        wd->wb_ = &wb;
        std::cout << "OPEN:" << &wd << ":" << &wb << std::endl;

        return true;
    }

    inline void loadPage(NSString *ustr) {
        NSURL *url = [NSURL URLWithString : ustr];
        assert(url != nullptr);
        NSURLRequest *request = [NSURLRequest requestWithURL : url];
        assert(request != nullptr);
        [webView.mainFrame loadRequest : request];
        std::cout << "LOADED:" << getCString(ustr) << std::endl;
    }

    inline void go(const std::string& surl) {
        NSString *pstr = getNSString(surl);
        if(csd.type == s::wui::ContentSourceType::Embedded){
            pstr = getNSString(empfx + surl);
        }else if(csd.type == s::wui::ContentSourceType::Resource){
            NSString *resourcesPath = [[NSBundle mainBundle] resourcePath];
            resourcesPath = [resourcesPath stringByAppendingString : getNSString(csd.path)];
            pstr = [resourcesPath stringByAppendingString : pstr];
        }
        loadPage(pstr);
    }

    inline void eval(const std::string& str) {
        auto jstr = jspfx + str;
        auto wso = [webView windowScriptObject];
        NSString* evalScriptString = [NSString stringWithUTF8String : jstr.c_str()];
        std::cout << "EVAL:" << std::string([evalScriptString UTF8String]) << std::endl;
        id x = [wso evaluateWebScript : evalScriptString];
        (void)x;
        //std::cout << "EVAL_RES:" << x << std::endl;
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
    //NSLog(@"canInit:%@", [request URL]);
    return [[[request URL] scheme] isEqualToString:@"embedded"];
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
    //NSLog(@"startLoading:%@", pathString);

    NSApplication *app = [NSApplication sharedApplication];
    auto wd = (AppDelegate*)[app delegate];
    assert((wd != nullptr) && (wd->wb_ != nullptr));
    auto cpath = getCString(pathString);
    auto& data = wd->wb_->impl().getEmbeddedSource(cpath);
    std::cout << "DATA:[" << std::get<0>(data) << "]" << std::endl;

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
    std::cout << "applicationDidFinishLaunching" << std::endl;
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

-(BOOL)applicationShouldTerminateAfterLastWindowClosed : (NSApplication *)theApplication {
    return YES;
}

- (void) webView:(WebView*)webView addMessageToConsole:(NSDictionary*)message {
	if (![message isKindOfClass:[NSDictionary class]]) {
		return;
	}

	NSLog(@"JavaScript console: %@:%@: %@",
		  [[message objectForKey:@"sourceURL"] lastPathComponent],	// could be nil
		  [message objectForKey:@"lineNumber"],
		  [message objectForKey:@"message"]);
}

-(void)webView:(WebView *)sender runJavaScriptAlertPanelWithMessage:(NSString *)message initiatedByFrame:(WebFrame *)frame {
    NSAlert *alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:@"OK"];
    [alert setMessageText:message];
    [alert runModal];
}

-(void)webView:(WebView *)webView windowScriptObjectAvailable : (WebScriptObject *)wso {
    std::cout << "windowScriptObjectAvailable" << std::endl;

    assert(wb_);
    addCommonPage(*wb_);
    if (wb_->onLoad) {
        wb_->onLoad("");
    }
}

@end

#endif // #ifdef WUI_OSX

#ifdef WUI_WIN
//#pragma comment( linker, "/subsystem:windows" )
//#pragma comment( linker, "/subsystem:console" )

namespace {
#if 1
#define TRACER(n) s::ftracer _t_(n)
#else
#define TRACER(n)
#endif
#if 1
#define TRACER1(n) s::ftracer _t1_(n)
#else
#define TRACER1(n)
#endif

    //Returns the last Win32 error, in string format. Returns an empty string if there is no error.
    inline std::string GetLastErrorAsString() {
        //Get the error message, if any.
        DWORD errorMessageID = ::GetLastError();
        if (errorMessageID == 0)
            return std::string(); //No error message has been recorded

        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        std::string message(messageBuffer, size);

        //Free the buffer.
        LocalFree(messageBuffer);

        return message;
    }
}

class s::application::Impl {
    s::application& app;
public:
    inline Impl(s::application& a) : app(a) {
        ::_tzset();
        if (::OleInitialize(NULL) != S_OK) {
            throw s::exception(s::argl("OleInitialize() failed: %m").arg("m", GetLastErrorAsString()));
        }

        char apath[MAX_PATH];
        DWORD rv = ::GetModuleFileNameA(NULL, apath, MAX_PATH);
        if (rv == 0) {
            DWORD ec = GetLastError();
            assert(ec != ERROR_SUCCESS);
            throw s::exception(s::argl("Internal error retrieving process path: %m").arg("m", GetLastErrorAsString()));
        }

        app.path = apath;
        app.name = s::filesystem::basename(app.path);
    }

    inline ~Impl() {
        ::OleUninitialize();
    }

    inline int loop();

    inline void exit(const int& exitcode) {
        ::PostQuitMessage(exitcode);
    }

    inline std::string datadir(const std::string& an) const {
        z::char_t chPath[MAX_PATH];
        /// \todo Use SHGetKnownFolderPath for vista and later.
        HRESULT hr = ::SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, chPath);
        if(!SUCCEEDED(hr)) {
            throw s::exception(s::argl("Internal error retrieving data directory: %{s}").arg("s", z::str(hr))));
        }
        auto data = z::string(chPath);
        data.replace("\\", "/");
        return data;
    }
};

#define NOTIMPLEMENTED _ASSERT(0); return E_NOTIMPL

namespace {
    struct WinObject : public IDispatch {
    private:
        s::wui::window& wb_;
        long ref;
    public:

        WinObject(s::wui::window& w);
        ~WinObject();

        // IUnknown
        virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv);
        virtual ULONG STDMETHODCALLTYPE AddRef();
        virtual ULONG STDMETHODCALLTYPE Release();

        // IDispatch
        virtual HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *pctinfo);
        virtual HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid,
            ITypeInfo **ppTInfo);
        virtual HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid,
            LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
        virtual HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid,
            LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
            EXCEPINFO *pExcepInfo, UINT *puArgErr);
    };
}

WinObject::WinObject(s::wui::window& w) : wb_(w), ref(0) {
    TRACER("WinObject::WinObject");
}

WinObject::~WinObject() {
    TRACER("WinObject::~WinObject");
    assert(ref == 0);
}

HRESULT STDMETHODCALLTYPE WinObject::QueryInterface(REFIID riid, void **ppv) {
    TRACER("WinObject::QueryInterface");
    *ppv = NULL;

    if (riid == IID_IUnknown || riid == IID_IDispatch) {
        *ppv = static_cast<IDispatch*>(this);
    }

    if (*ppv != NULL) {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WinObject::AddRef() {
    TRACER("WinObject::AddRef");
    return InterlockedIncrement(&ref);
}

ULONG STDMETHODCALLTYPE WinObject::Release() {
    TRACER("WinObject::Release");
    int tmp = InterlockedDecrement(&ref);
    assert(tmp >= 0);
    if (tmp == 0) {
        //delete this;
    }

    return tmp;
}

HRESULT STDMETHODCALLTYPE WinObject::GetTypeInfoCount(UINT *pctinfo) {
    TRACER("WinObject::GetTypeInfoCount");
    *pctinfo = 0;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinObject::GetTypeInfo(UINT /*iTInfo*/, LCID /*lcid*/, ITypeInfo** /*ppTInfo*/) {
    TRACER("WinObject::GetTypeInfo");
    return E_FAIL;
}

HRESULT STDMETHODCALLTYPE WinObject::GetIDsOfNames(REFIID /*riid*/,
    LPOLESTR *rgszNames, UINT cNames, LCID /*lcid*/, DISPID *rgDispId) {
    TRACER("WinObject::GetIDsOfNames");
    HRESULT hr = S_OK;

    for (UINT i = 0; i < cNames; i++) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
        std::string fnname(convertor.to_bytes(rgszNames[i]));
        if (fnname == "invoke") {
            rgDispId[i] = DISPID_VALUE + 1;
        } else {
            rgDispId[i] = DISPID_UNKNOWN;
            hr = DISP_E_UNKNOWNNAME;
        }
    }
    return hr;
}

inline std::string VariantToString(VARIANTARG& var) {
    assert(var.vt == VT_BSTR);
    _bstr_t bstrArg = var.bstrVal;
    std::cout << (long)(const wchar_t*)bstrArg << std::endl;
    std::wstring arg = (const wchar_t*)bstrArg;
    std::wcout << "WGET:" << arg << std::endl << std::endl;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
    auto rv = std::string(convertor.to_bytes(arg));
    std::cout << "GET:[" << rv << "]" << std::endl;
    return rv;
}

inline std::string getStringFromCOM(DISPPARAMS* dp, const size_t& idx) {
    return VariantToString(dp->rgvarg[idx]);
}

inline CComPtr<IDispatch> VariantToDispatch(VARIANT var) {
    CComPtr<IDispatch> disp = (var.vt == VT_DISPATCH && var.pdispVal) ? var.pdispVal : NULL;
    return disp;
}

inline HRESULT VariantToInteger(VARIANT var, long &integer) {
    CComVariant _var;
    HRESULT hr = VariantChangeType(&_var, &var, 0, VT_I4);
    if (FAILED(hr)) {
        return hr;
    }
    integer = _var.lVal;
    return S_OK;
}

inline HRESULT DispatchGetProp(CComPtr<IDispatch> disp, LPOLESTR name, VARIANT *pVar) {
    HRESULT hr = S_OK;

    if (!pVar) {
        return E_INVALIDARG;
    }

    DISPID dispid = 0;
    hr = disp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) {
        return hr;
    }

    DISPPARAMS dispParams = { 0 };
    hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dispParams, pVar, NULL, NULL);
    if (FAILED(hr)) {
        return hr;
    }
    return hr;
}

inline std::vector<std::string> getStringArrayFromCOM(DISPPARAMS* dp, const size_t& idx) {
    assert(dp->rgvarg[idx].vt == VT_DISPATCH);

    std::vector<std::string> rv;
    CComPtr<IDispatch> pDispatch = VariantToDispatch(dp->rgvarg[idx]);
    if (!pDispatch)
        return rv;

    CComVariant varLength;
    HRESULT hr = DispatchGetProp(pDispatch, L"length", &varLength);
    if (FAILED(hr)) {
        return rv;
    }

    long intLength;
    hr = VariantToInteger(varLength, intLength);
    if (FAILED(hr)) {
        return rv;
    }

    WCHAR wcharIndex[25];
    CComVariant varItem;
    for (long index = 0; index < intLength; ++index) {
        wsprintf(wcharIndex, _T("%ld\0"), index);
        hr = DispatchGetProp(pDispatch, CComBSTR(wcharIndex), &varItem);
        if (FAILED(hr)) {
            return rv;
        }
        auto item = VariantToString(varItem);
        std::cout << "ITEM:" << item << std::endl;
        rv.push_back(item);
    }

    return rv;
}

HRESULT STDMETHODCALLTYPE WinObject::Invoke(
    DISPID dispIdMember,
    REFIID /*riid*/,
    LCID /*lcid*/,
    WORD wFlags,
    DISPPARAMS *pDispParams,
    VARIANT *pVarResult,
    EXCEPINFO* /*pExcepInfo*/,
    UINT* /*puArgErr*/)
{
    TRACER("WinObject::Invoke");
    if (wFlags & DISPATCH_METHOD) {
        if (dispIdMember == DISPID_VALUE + 1) {
            auto params = getStringArrayFromCOM(pDispParams, 0);
//            std::cout << "PARAMS:" << params << std::endl;
            auto fn = getStringFromCOM(pDispParams, 1);
            std::cout << "FN:" << fn << std::endl;
            auto obj = getStringFromCOM(pDispParams, 2);
            std::cout << "OBJ:" << obj << std::endl;
            wb_.invoke(obj, fn, params);
            return S_OK;
        }
        assert(false);
    }

    return E_FAIL;
}

const LPCWSTR WEBFORM_CLASS = L"WebUIWindowClass";
#define WEBFN_CLICKED      2
#define WEBFN_LOADED       3

struct s::wui::window::Impl : public IUnknown {
    static std::vector<s::wui::window::Impl*> wlist;
    long ref;
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_IUnknown) { *ppv = this; AddRef(); return S_OK; }
        if (riid == IID_IOleClientSite) { *ppv = &clientsite; AddRef(); return S_OK; }
        if (riid == IID_IOleWindow || riid == IID_IOleInPlaceSite) { *ppv = &site; AddRef(); return S_OK; }
        if (riid == IID_IOleInPlaceUIWindow || riid == IID_IOleInPlaceFrame) { *ppv = &frame; AddRef(); return S_OK; }
        if (riid == IID_IDispatch) { *ppv = &dispatch; AddRef(); return S_OK; }
        if (riid == IID_IDocHostUIHandler) { *ppv = &uihandler; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() {
        TRACER("window::Impl::AddRef");
        return InterlockedIncrement(&ref);
    }

    ULONG STDMETHODCALLTYPE Release() {
        TRACER("window::Impl::Release");
        int tmp = InterlockedDecrement(&ref);
        assert(tmp >= 0);
        if (tmp == 0) {
            //delete this;
        }
        return tmp;
    }

    struct TOleClientSite : public IOleClientSite {
    public: Impl *webf;
            // IUnknown
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
            ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
            ULONG STDMETHODCALLTYPE Release() { TRACER("TOleClientSite::Release"); return webf->Release(); }
            // IOleClientSite
            HRESULT STDMETHODCALLTYPE SaveObject() { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetMoniker(DWORD /*dwAssign*/, DWORD /*dwWhichMoniker*/, IMoniker** /*ppmk*/) { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer **ppContainer) { *ppContainer = 0; return E_NOINTERFACE; }
            HRESULT STDMETHODCALLTYPE ShowObject() { return S_OK; }
            HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL /*fShow*/) { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() { return E_NOTIMPL; }
    } clientsite;

    struct TOleInPlaceSite : public IOleInPlaceSite {
        s::wui::window::Impl *webf;
        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
        ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() { return webf->Release(); }
        // IOleWindow
        HRESULT STDMETHODCALLTYPE GetWindow(HWND *phwnd) { *phwnd = webf->hhost; return S_OK; }
        HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL /*fEnterMode*/) { return E_NOTIMPL; }
        // IOleInPlaceSite
        HRESULT STDMETHODCALLTYPE CanInPlaceActivate() { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnInPlaceActivate() { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnUIActivate() { return S_OK; }
        HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO info) {
            *ppFrame = &webf->frame; webf->frame.AddRef();
            *ppDoc = 0;
            info->fMDIApp = FALSE; info->hwndFrame = webf->hhost; info->haccel = 0; info->cAccelEntries = 0;
            GetClientRect(webf->hhost, lprcPosRect);
            GetClientRect(webf->hhost, lprcClipRect);
            return(S_OK);
        }
        HRESULT STDMETHODCALLTYPE Scroll(SIZE /*scrollExtant*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL /*fUndoable*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() { return S_OK; }
        HRESULT STDMETHODCALLTYPE DiscardUndoState() { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DeactivateAndUndo() { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT lprcPosRect) {
            IOleInPlaceObject *iole = 0;
            webf->ibrowser->QueryInterface(IID_IOleInPlaceObject, (void**)&iole);
            if (iole != 0) { iole->SetObjectRects(lprcPosRect, lprcPosRect); iole->Release(); }
            return S_OK;
        }
    } site;

    struct TOleInPlaceFrame : public IOleInPlaceFrame {
        s::wui::window::Impl *webf;
        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
        ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() { TRACER("TOleInPlaceFrame::Release"); return webf->Release(); }
        // IOleWindow
        HRESULT STDMETHODCALLTYPE GetWindow(HWND *phwnd) { *phwnd = webf->hhost; return S_OK; }
        HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL /*fEnterMode*/) { return E_NOTIMPL; }
        // IOleInPlaceUIWindow
        HRESULT STDMETHODCALLTYPE GetBorder(LPRECT /*lprectBorder*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS /*pborderwidths*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS /*pborderwidths*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject* /*pActiveObject*/, LPCOLESTR /*pszObjName*/) { return S_OK; }
        // IOleInPlaceFrame
        HRESULT STDMETHODCALLTYPE InsertMenus(HMENU /*hmenuShared*/, LPOLEMENUGROUPWIDTHS /*lpMenuWidths*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetMenu(HMENU /*hmenuShared*/, HOLEMENU /*holemenu*/, HWND /*hwndActiveObject*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU /*hmenuShared*/) { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR /*pszStatusText*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE EnableModeless(BOOL /*fEnable*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG /*lpmsg*/, WORD /*wID*/) { return E_NOTIMPL; }
    } frame;

    struct TDispatch : public IDispatch {
        s::wui::window::Impl *webf;
        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
        ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() { TRACER("TDispatch::Release"); return webf->Release(); }
        // IDispatch
        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *pctinfo) { *pctinfo = 0; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT /*iTInfo*/, LCID /*lcid*/, ITypeInfo** /*ppTInfo*/) { return E_FAIL; }
        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID /*riid*/, LPOLESTR* /*rgszNames*/, UINT /*cNames*/, LCID /*lcid*/, DISPID* /*rgDispId*/) { return E_FAIL; }
        HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID /*riid*/, LCID /*lcid*/, WORD /*wFlags*/, DISPPARAMS* Params, VARIANT* pVarResult, EXCEPINFO* /*pExcepInfo*/, UINT* /*puArgErr*/) {
            switch (dispIdMember) { // DWebBrowserEvents2
            case DISPID_BEFORENAVIGATE2:
                webf->BeforeNavigate2(Params->rgvarg[5].pvarVal->bstrVal, Params->rgvarg[0].pboolVal);
                break;
            case DISPID_NAVIGATECOMPLETE2:
                webf->NavigateComplete2(Params->rgvarg[0].pvarVal->bstrVal);
                break;
            case DISPID_DOCUMENTCOMPLETE:
                webf->DocumentComplete(Params->rgvarg[0].pvarVal->bstrVal);
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
    } dispatch;

    struct TDocHostUIHandler : public IDocHostUIHandler {
        s::wui::window::Impl *webf;
        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
        ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() { return webf->Release(); }
        // IDocHostUIHandler
        HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD /*dwID*/, POINT* /*ppt*/, IUnknown* /*pcmdtReserved*/, IDispatch* /*pdispReserved*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO *pInfo) { pInfo->dwFlags = (webf->hasscrollbars ? 0 : DOCHOSTUIFLAG_SCROLL_NO) | DOCHOSTUIFLAG_NO3DOUTERBORDER; return S_OK; }
        HRESULT STDMETHODCALLTYPE ShowUI(DWORD /*dwID*/, IOleInPlaceActiveObject* /*pActiveObject*/, IOleCommandTarget* /*pCommandTarget*/, IOleInPlaceFrame* /*pFrame*/, IOleInPlaceUIWindow* /*pDoc*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE HideUI() { return S_OK; }
        HRESULT STDMETHODCALLTYPE UpdateUI() { return S_OK; }
        HRESULT STDMETHODCALLTYPE EnableModeless(BOOL /*fEnable*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL /*fActivate*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL /*fActivate*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT /*prcBorder*/, IOleInPlaceUIWindow* /*pUIWindow*/, BOOL /*fRameWindow*/) { return S_OK; }
        HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG /*lpMsg*/, const GUID* /*pguidCmdGroup*/, DWORD /*nCmdID*/) { return S_FALSE; }
        HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR* /*pchKey*/, DWORD /*dw*/) { return S_FALSE; }
        HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget* /*pDropTarget*/, IDropTarget** /*ppDropTarget*/) { return S_FALSE; }
        HRESULT STDMETHODCALLTYPE GetExternal(IDispatch** ppDispatch) { *ppDispatch = 0; return S_FALSE; }
        HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD /*dwTranslate*/, OLECHAR* /*pchURLIn*/, OLECHAR** ppchURLOut) { *ppchURLOut = 0; return S_FALSE; }
        HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject* /*pDO*/, IDataObject** ppDORet) { *ppDORet = 0; return S_FALSE; }
    } uihandler;

    struct TDocHostShowUI : public IDocHostShowUI {
        s::wui::window::Impl *webf;
        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) { return webf->QueryInterface(riid, ppv); }
        ULONG STDMETHODCALLTYPE AddRef() { return webf->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() { return webf->Release(); }
        // IDocHostShowUI
        HRESULT STDMETHODCALLTYPE ShowMessage(HWND /*hwnd*/, LPOLESTR /*lpstrText*/, LPOLESTR /*lpstrCaption*/, DWORD /*dwType*/, LPOLESTR /*lpstrHelpFile*/, DWORD /*dwHelpContext*/, LRESULT *plResult) { *plResult = IDCANCEL; return S_OK; }
        HRESULT STDMETHODCALLTYPE ShowHelp(HWND /*hwnd*/, LPOLESTR /*pszHelpFile*/, UINT /*uCommand*/, DWORD /*dwData*/, POINT /*ptMouse*/, IDispatch* /*pDispatchObjectHit*/) { return S_OK; }
    } showui;


    Impl(s::wui::window& w);
    ~Impl();
    void Close();
    void SetFocus();
    void addCustomObject(IDispatch* custObj, const std::string& name);
    //
    bool open();
    inline void setDefaultMenu() {
    }

    inline void setMenu(const menu& /*m*/) {
        throw s::exception("Not implemented: setMenu");
    }

    inline void eval(const std::string& str) {
        CComPtr<IHTMLDocument2> doc = GetDoc();
        if (doc == NULL) {
            throw s::exception(s::argl("Unable to get document object: %m").arg("m", GetLastErrorAsString()));
        }

        CComPtr<IHTMLWindow2> win;
        doc->get_parentWindow(&win);
        if (win == NULL) {
            throw s::exception(s::argl("Unable to get parent window: %m").arg("m", GetLastErrorAsString()));
        }

        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
        std::wstring rv(convertor.from_bytes(str));
        VARIANT v;
        VariantInit(&v);
        HRESULT hr = win->execScript((BSTR)rv.c_str(), NULL, &v);
        if (hr != S_OK) {
            s::log() << "JavaScript execution error: " << str << ":" << hr << std::endl;
        }

        VariantClear(&v);
        ::InvalidateRect(hhost, 0, true);
    }

    std::unique_ptr<WinObject> nproxy_;
    inline void addObject(const std::string& name) {
        TRACER("addObject");
        nproxy_ = std::make_unique<WinObject>(browser_);
        addCustomObject(nproxy_.get(), name);
    }

    void go(const std::string& fn);
    IHTMLDocument2 *GetDoc();
    bool setupOle();
    bool hookMessage(MSG& msg);
    static LRESULT CALLBACK WebformWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //
    void BeforeNavigate2(const wchar_t *url, short *cancel);
    void NavigateComplete2(const wchar_t *url);
    void DocumentComplete(const wchar_t *url);
    enum NavState {
        PreBeforeNavigate = 0x01,
        PreDocumentComplete = 0x02,
        PreNavigateComplete = 0x04,
        All = 0x07,
    };

    unsigned int isnaving;    // bitmask

    s::wui::window& browser_;
    HWND hhost;               // This is the window that hosts us
    IWebBrowser2 *ibrowser;   // Our pointer to the browser itself. Released in Close().
    DWORD cookie;             // By this cookie shall the watcher be known
                              //
    bool hasscrollbars;       // This is read from WS_VSCROLL|WS_HSCROLL at WM_CREATE
    std::string curl;         // This was the url that the user just clicked on
};

std::vector<s::wui::window::Impl*> s::wui::window::Impl::wlist;

s::wui::window::Impl::Impl(s::wui::window& w) : browser_(w) {
    TRACER("window::Impl::Impl");
    ref = 0; clientsite.webf = this; site.webf = this; frame.webf = this; dispatch.webf = this; uihandler.webf = this; showui.webf = this;
    this->hhost = 0;
    ibrowser = 0;
    cookie = 0;
    isnaving = 0;
    wlist.push_back(this);
}

s::wui::window::Impl::~Impl() {
    TRACER("window::Impl::~Impl");
    assert(ref <= 1); // \todo: sometimes one final IOleClientSite::Release() does not get called

    for (auto it = wlist.begin(), ite = wlist.end(); it != ite; ++it) {
        if (*it == this) {
            wlist.erase(it);
            break;
        }
    }
}

void s::wui::window::Impl::Close() {
    if (ibrowser != 0) {
        CComPtr<IConnectionPointContainer> cpc;
        ibrowser->QueryInterface(IID_IConnectionPointContainer, (void**)&cpc);
        if (cpc != 0) {
            CComPtr<IConnectionPoint> cp;
            cpc->FindConnectionPoint(DIID_DWebBrowserEvents2, &cp);
            if (cp != 0) {
                cp->Unadvise(cookie);
            }
        }

        CComPtr<IOleObject> iole;
        ibrowser->QueryInterface(IID_IOleObject, (void**)&iole);
        if (iole != 0) {
            iole->Close(OLECLOSE_NOSAVE);
        }

        ibrowser->Release();
        ibrowser = 0;
    }
}

void s::wui::window::Impl::SetFocus() {
    if (ibrowser != 0) {
        CComPtr<IOleObject> iole;
        ibrowser->QueryInterface(IID_IOleObject, (void**)&iole);
        if (iole != 0) {
            iole->DoVerb(OLEIVERB_UIACTIVATE, NULL, &clientsite, 0, hhost, 0);
        }
    }
}

bool s::wui::window::Impl::setupOle() {
    TRACER("setupOle");
    hasscrollbars = (GetWindowLongPtr(hhost, GWL_STYLE)&(WS_HSCROLL | WS_VSCROLL)) != 0;

    RECT rc;
    GetClientRect(hhost, &rc);

    HRESULT hr;
    CComPtr<IOleObject> iole;
    hr = CoCreateInstance(CLSID_WebBrowser, NULL, CLSCTX_INPROC_SERVER, IID_IOleObject, (void**)&iole);
    if (hr != S_OK) {
        throw s::exception(s::argl("CoCreateInstance error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    hr = iole->SetClientSite(&clientsite);
    if (hr != S_OK) {
        throw s::exception(s::argl("SetClientSite error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    hr = iole->SetHostNames(L"MyHost", L"MyDoc");
    if (hr != S_OK) {
        throw s::exception(s::argl("SetHostNames error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    hr = OleSetContainedObject(iole, TRUE);
    if (hr != S_OK) {
        throw s::exception(s::argl("OleSetContainedObject error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    hr = iole->DoVerb(OLEIVERB_SHOW, 0, &clientsite, 0, hhost, &rc);
    if (hr != S_OK) {
        throw s::exception(s::argl("DoVerb error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    bool connected = false;
    CComPtr<IConnectionPointContainer> cpc;
    hr = iole->QueryInterface(IID_IConnectionPointContainer, (void**)&cpc);
    if (cpc != 0) {
        CComPtr<IConnectionPoint> cp;
        hr = cpc->FindConnectionPoint(DIID_DWebBrowserEvents2, &cp);
        if (cp != 0) {
            cp->Advise((IDispatch*)this, &cookie);
            connected = true;
        }
    }

    if (!connected) {
        throw s::exception(s::argl("Not connected error: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    iole->QueryInterface(IID_IWebBrowser2, (void**)&ibrowser);
    return true;
}

void s::wui::window::Impl::addCustomObject(IDispatch* custObj, const std::string& name) {
    TRACER("addCustomObject");

    HRESULT hr;
    CComPtr<IHTMLDocument2> doc = GetDoc();
    if (doc == NULL) {
        throw s::exception(s::argl("Invalid document state: %m(%e)").arg("m", GetLastErrorAsString()));
    }

    CComPtr<IHTMLWindow2> win;
    hr = doc->get_parentWindow(&win);
    if (win == NULL) {
        throw s::exception(s::argl("unable to get parent window: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    CComPtr<IDispatchEx> winEx;
    hr = win->QueryInterface(&winEx);
    if (winEx == NULL) {
        throw s::exception(s::argl("unable to get DispatchEx: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    _bstr_t objName(name.c_str());

    DISPID dispid;
    hr = winEx->GetDispID(objName, fdexNameEnsure, &dispid);
    if (FAILED(hr)) {
        throw s::exception(s::argl("unable to get DispatchID: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    DISPID namedArgs[] = { DISPID_PROPERTYPUT };
    DISPPARAMS params;
    params.rgvarg = new VARIANT[1];
    params.rgvarg[0].pdispVal = custObj;
    params.rgvarg[0].vt = VT_DISPATCH;
    params.rgdispidNamedArgs = namedArgs;
    params.cArgs = 1;
    params.cNamedArgs = 1;

    hr = winEx->InvokeEx(dispid, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &params, NULL, NULL, NULL);
    if (FAILED(hr)) {
        throw s::exception(s::argl("unable to invoke JSE: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }
}

bool s::wui::window::Impl::hookMessage(MSG& msg) {
    if ((msg.message >= WM_KEYFIRST) && (msg.message <= WM_KEYLAST)) {
        CComPtr<IOleInPlaceActiveObject> ioipo;
        ibrowser->QueryInterface(IID_IOleInPlaceActiveObject, (void**)&ioipo);
        assert(ioipo);
        if (ioipo != 0) {
            HRESULT hr = ioipo->TranslateAccelerator(&msg);
            if (hr == S_OK) {
                return true;
            }
        }
    }
    return false;
}

LRESULT CALLBACK s::wui::window::Impl::WebformWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        s::wui::window::Impl* impl = (s::wui::window::Impl*)((LPCREATESTRUCT(lParam))->lpCreateParams);
        impl->hhost = hwnd;
        impl->AddRef();
        impl->setupOle();
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)impl);
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    s::wui::window::Impl* impl = (s::wui::window::Impl*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (impl == 0) {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    CREATESTRUCTA* cs = 0;

    switch (msg) {
    case WM_CREATE:
        cs = (CREATESTRUCTA*)lParam;
        if (cs->style & (WS_HSCROLL | WS_VSCROLL)) {
            SetWindowLongPtr(hwnd, GWL_STYLE, cs->style & ~(WS_HSCROLL | WS_VSCROLL));
        }
        if (impl->browser_.onOpen) {
            impl->browser_.onOpen();
        }
        break;
    case WM_DESTROY:
        impl->Close();
        impl->Release();
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_CLOSE:
        if (impl->browser_.onClose) {
            impl->browser_.onClose();
        }
        break;
    case WM_SETFOCUS:
        impl->SetFocus();
        break;
    case WM_SIZE:
        impl->ibrowser->put_Width(LOWORD(lParam));
        impl->ibrowser->put_Height(HIWORD(lParam));
        break;
    };
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool s::wui::window::Impl::open() {
    TRACER("open");
    WNDCLASSEX wcex = { 0 };
    HINSTANCE hInstance = GetModuleHandle(0);
    if (!::GetClassInfoExW(hInstance, WEBFORM_CLASS, &wcex)) {
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
    if (hwndParent == 0) {
        flags |= WS_OVERLAPPEDWINDOW;
    } else {
        flags |= WS_CHILDWINDOW;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
    std::wstring title(convertor.from_bytes(wi.title));

    HWND hWnd = CreateWindowW(WEBFORM_CLASS, title.c_str(), flags, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndParent, (HMENU)id, hInstance, (LPVOID)this);
    if (hWnd == NULL) {
        throw s::exception(s::argl("Could not create window: %m").arg("m", GetLastErrorAsString()));
    }
    ::ShowWindow(hWnd, SW_SHOW);
    ::UpdateWindow(hWnd);
    addObject(("nproxy"));
    return (hWnd != 0);
}

void s::wui::window::Impl::go(const std::string& urlx) {
    auto url = urlx;
    if (url.substr(0, 4) != "http") {
        url = "res://" + s::app().name + ".exe/" + url;
    }

    // Navigate to the new one and delete the old one
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
    std::wstring ws(convertor.from_bytes(url));

    isnaving = All;
    VARIANT v;
    v.vt = VT_I4;
    v.lVal = 0; //v.lVal=navNoHistory;
    ibrowser->Navigate((BSTR)ws.c_str(), &v, NULL, NULL, NULL);

    // nb. the events know not to bother us for currentlynav.
    // (Special case: maybe it's already loaded by the time we get here!)
    if ((isnaving & PreDocumentComplete) == 0) {
        WPARAM w = (GetWindowLong(hhost, GWL_ID) & 0xFFFF) | ((WEBFN_LOADED & 0xFFFF) << 16);
        PostMessage(GetParent(hhost), WM_COMMAND, w, (LPARAM)hhost);
        if (browser_.onLoad) {
            browser_.onLoad(url);
        }
    }
    isnaving &= ~PreNavigateComplete;
    return;
}

void s::wui::window::Impl::DocumentComplete(const wchar_t* /*wurl*/) {
    TRACER("DocumentComplete");
    isnaving &= ~PreDocumentComplete;
    if (isnaving & PreNavigateComplete) {
        return; // we're in the middle of Go(), so the notification will be handled there
    }

    WPARAM w = (GetWindowLong(hhost, GWL_ID) & 0xFFFF) | ((WEBFN_LOADED & 0xFFFF) << 16);
    PostMessage(hhost, WM_COMMAND, w, (LPARAM)hhost);
    SetFocus();
}

void s::wui::window::Impl::BeforeNavigate2(const wchar_t* wurl, short *cancel) {
    TRACER("BeforeNavigate2");
    *cancel = FALSE;
    int oldisnav = isnaving;
    isnaving &= ~PreBeforeNavigate;
    if (oldisnav & PreBeforeNavigate) {
        return; // ignore events that came from our own Go()
    }

    *cancel = TRUE;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
    curl = convertor.to_bytes(wurl);

    WPARAM w = (GetWindowLong(hhost, GWL_ID) & 0xFFFF) | ((WEBFN_CLICKED & 0xFFFF) << 16);
    PostMessage(GetParent(hhost), WM_COMMAND, w, (LPARAM)hhost);
}

void s::wui::window::Impl::NavigateComplete2(const wchar_t* burl) {
    TRACER("NavigateComplete2");
    std::wstring wurl = burl;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convertor;
    std::string url(convertor.to_bytes(wurl));
    if (browser_.onLoad) {
        browser_.onLoad(url);
    }
}

IHTMLDocument2 *s::wui::window::Impl::GetDoc() {
    CComPtr<IDispatch> xdispatch;
    HRESULT hr = ibrowser->get_Document(&xdispatch);
    if (xdispatch == 0) {
        throw s::exception(s::argl("unable to get document: %m(%e)").arg("m", GetLastErrorAsString()).arg("e", hr));
    }

    CComPtr<IHTMLDocument2> doc;
    xdispatch->QueryInterface(IID_IHTMLDocument2, (void**)&doc);
    return doc;
}

inline int s::application::Impl::loop() {
    if (s::app().onInit) {
        s::app().onInit();
    }

    //auto hInstance = ::GetModuleHandle(NULL);
    //HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_APP));
    HACCEL hAccelTable = 0;

    // Main message loop
    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0) != 0) {
        if (!::TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            bool done = false;
            for (auto w : s::wui::window::Impl::wlist) {
                done = w->hookMessage(msg);
                if (done) {
                    break;
                }
            }
            if (!done) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
        }
    }
    return 0;
}

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

    inline void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>>& lst) {
        csd.setEmbeddedSource(lst);
    }

    inline void setContentSourceResource(const std::string& path) {
        csd.setResourceSource(path);
    }

    inline const auto& getEmbeddedSource(const std::string& url) {
        return csd.getEmbeddedSource(url);
    }

    inline bool open() {
        if(wb.onOpen){
            wb.onOpen();
        }
        return true;
    }

    inline void setDefaultMenu() {
    }

    inline void setMenu(const menu& m) {
    }

    inline std::string invoke(const std::string& obj, const std::string& fn, const std::vector<std::string>& params) {
        auto& jo = wb.getObject(obj);
        auto rv = jo.invoke(fn, params);
        return rv;
    }

    inline void addNativeObject(s::js::objectbase& jo, const std::string& body) {
        ALOG("addNativeObject:%s", jo.name.c_str());
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

    inline void go(const std::string& url) {
        try{
            onLoad(url);
        }catch(const std::exception& ex){
            ALOG("onLoad-error:%s", ex.what());
        }
        JniEnvGuard envg;
        if(csd.type == s::wui::ContentSourceType::Embedded){
            auto& data = getEmbeddedSource(url);
            jstring jurl = envg.env->NewStringUTF(url.c_str());
            jstring jstr = envg.env->NewStringUTF((const char*)std::get<0>(data));
            jstring jmimetype = envg.env->NewStringUTF(std::get<2>(data).c_str());
            envg.env->CallVoidMethod(_s_activity, _s_goEmbeddedFn, jurl, jstr, jmimetype);
        }else if(csd.type == s::wui::ContentSourceType::Resource){
            auto rpath = csd.path;
            if(rpath.at(0) == '/') {
                rpath = rpath.substr(1);
            }
            if(rpath.at(rpath.length()-1) == '/') {
                rpath = rpath.substr(0, rpath.length()-1);
            }
            auto furl = "file:///android_asset/" + rpath + "/" + url;
            jstring jurl = envg.env->NewStringUTF(furl.c_str());
            envg.env->CallVoidMethod(_s_activity, _s_goStandardFn, jurl);
        }
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
        cv_.wait(lk, [&]{return mq_.size() > 0;});
        auto fn = mq_.front();
        mq_.pop();
        return fn;
    }

    inline int loop() {
        auto& app = this->app;
        post([&app](){
            if(app.onInit){
                app.onInit();
            }
        });

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
            ALOG("error in worker thread:%s", ex.what());
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
            throw std::runtime_error(std::string("unable to obtain wui class"));
        }

        _s_activityCls = reinterpret_cast<jclass>(env->NewGlobalRef(activityCls));

        _s_setObjectFn = env->GetMethodID(_s_activityCls, "setObject", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (!_s_setObjectFn) {
            throw std::runtime_error(std::string("unable to obtain wui::setObject"));
        }

        _s_goEmbeddedFn = env->GetMethodID(_s_activityCls, "go_embedded", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (!_s_goEmbeddedFn) {
            throw std::runtime_error(std::string("unable to obtain wui::go_embedded"));
        }

        _s_goStandardFn = env->GetMethodID(_s_activityCls, "go_standard", "(Ljava/lang/String;)V");
        if (!_s_goStandardFn) {
            throw std::runtime_error(std::string("unable to obtain wui::go_standard"));
        }

        _s_tag = convertJniStringToStdString(env, jtag);
        _s_activity = reinterpret_cast<jobject>(env->NewGlobalRef(activity));
        _s_assetManager = reinterpret_cast<jobject>(env->NewGlobalRef(assetManager));

        auto params = convertJavaArrayToVector(env, jparams);
        thrd = std::make_unique<std::thread>(mainx, params);
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

    JNIEXPORT void JNICALL Java_com_renjipanicker_wui_invokeNative(JNIEnv* env, jobject activity, jstring jobj, jstring jfn, jobjectArray jparams) {
        const std::string obj = convertJniStringToStdString(env, jobj);
        const std::string fn = convertJniStringToStdString(env, jfn);

        auto params = convertJavaArrayToVector(env, jparams);
        if(_s_impl != nullptr){
            _s_impl->post([obj, fn, params](){
                if(_s_wimpl != nullptr){
                    _s_wimpl->invoke(obj, fn, params);
                }
            });
        }
    }
} // extern "C"

#endif // WUI_NDK

#ifdef WUI_LINUX
class s::wui::window::Impl {
    s::wui::window& wb;
public:
    inline Impl(s::wui::window& w) : wb(w) {
    }

    inline bool open() {
        return false;
    }

    inline void setDefaultMenu() {
    }

    inline void setMenu(const menu& m) {
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

void s::wui::window::setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>>& lst) {
    return impl_->setContentSourceEmbedded(lst);
}

void s::wui::window::setContentSourceResource(const std::string& path) {
    return impl_->setContentSourceResource(path);
}

bool s::wui::window::open() {
    return impl_->open();
}

void s::wui::window::setDefaultMenu() {
    impl_->setDefaultMenu();
}

void s::wui::window::setMenu(const menu& m) {
    impl_->setMenu(m);
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
#endif
    std::vector<std::string> rv;
    return rv;
}

void s::asset::readFile(const std::string& filename, std::function<bool(const char*, const size_t&)> fn) {
#if defined(WUI_NDK)
    struct file{
        JniEnvGuard envg;
        AAssetManager* mgr;
        AAsset* asset;
        inline file(const std::string& filename){
            mgr = AAssetManager_fromJava(envg.env, _s_assetManager);
            asset = AAssetManager_open(mgr, filename.c_str(), AASSET_MODE_STREAMING);
        }

        void readAll(std::function<bool(const char*, const size_t&)>& fn){
            char buf[BUFSIZ];
            int nb_read = 0;
            while ((nb_read = AAsset_read(asset, buf, BUFSIZ)) > 0){
                fn(buf, nb_read);
            }
        }
        inline ~file(){
            AAsset_close(asset);
        }
    };

    file onx(filename);
    onx.readAll(fn);
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
