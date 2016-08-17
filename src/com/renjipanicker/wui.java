package com.renjipanicker;

import java.util.ArrayList;
import java.lang.Thread;
import android.provider.Settings.Secure;
import android.graphics.Bitmap;
import android.content.res.AssetManager;

import android.webkit.WebView;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;
import android.webkit.WebViewClient;
import android.webkit.ConsoleMessage;
import android.webkit.WebSettings;
import android.webkit.WebSettings.RenderPriority;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebChromeClient;
import android.webkit.JsResult;
import android.webkit.JavascriptInterface;
import android.util.Log;

public class wui {
    private class nproxy {
        public String name;
        public nproxy(String n) {
            this.name = n;
        }
        @JavascriptInterface
        public void invoke(String fn, String[] params) {
            invokeNative(name, fn, params);
        }
    };

    private final String TAG;
    private WebView webView;
    private Handler mainHandler;
    private nproxy consoleobj = null;

    private native void initNative(String tag, String[] params, AssetManager assetManager);
    private native void exitNative();
    private native void initPage(String url);
    private native void invokeNative(String obj, String fn, String[] params);

    class JsObject {
        public String name;
        public String nname;
        public String body;
        public JsObject(String n, String nn, String b) {
            this.name = n;
            this.nname = nn;
            this.body = b;
        }
    };

    private ArrayList<JsObject> jsoList = new ArrayList<JsObject>();

    public wui(String tg, WebView wv, Handler mh, String[] params, AssetManager assetManager) {
        this.TAG = tg;
        this.webView = wv;
        this.mainHandler = mh;
        webView.getSettings().setJavaScriptEnabled(true);
        webView.getSettings().setRenderPriority(RenderPriority.HIGH);
        webView.getSettings().setCacheMode(WebSettings.LOAD_NO_CACHE);

        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onJsAlert(WebView view, String url, String message, JsResult result) {
                return super.onJsAlert(view, url, message, result);
            }
            @Override
            public boolean onConsoleMessage(ConsoleMessage cm) {
                if(consoleobj != null){
                    String[] params = new String[3];
                    params[0] = cm.message();
                    consoleobj.invoke("log", params);
                }else{
                    //Log.d(TAG, cm.message() + ":--:yFrom line " + cm.lineNumber() + " of " + cm.sourceId());
                }
                return true;
            }
        });

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                Log.d(TAG, "pstart:" + url);
		    }
            @Override
            public void onPageFinished(WebView view, String url) {
                Log.d(TAG, "pfinish:" + url);
		    }
		});

        initNative(TAG, params, assetManager);
    }

    public void onDestroy() {
        exitNative();
    }

    private void insertObjects() {
        for(JsObject jso : jsoList){
            nproxy np = new nproxy(jso.name);
            if(jso.name == "console"){
                consoleobj = np;
            }else{
                webView.addJavascriptInterface(np, jso.nname);
            }
        }
    }

    private void insertObjectBody() {
        for(JsObject jso : jsoList){
            webView.loadUrl(jso.body);
        }
        jsoList.clear();
    }

    public void setObject(final String name, final String nname, final String body) {
        jsoList.add(new JsObject(name, nname, body));
    }

    public void go_embedded(final String url, final String data, final String mimetype) {
        mainHandler.post(new Runnable() {
            public void run() {
                insertObjects();
                webView.loadDataWithBaseURL("embedded:", data, mimetype, "utf-8", null);
                insertObjectBody();
                initPage(url);
            }
        });
    }

    public void go_standard(final String url) {
        mainHandler.post(new Runnable() {
            public void run() {
                insertObjects();
                webView.loadUrl(url);
                insertObjectBody();
                initPage(url);
            }
        });
    }
};
