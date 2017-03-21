package com.renjipanicker;

import java.util.ArrayList;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.lang.Thread;
import android.provider.Settings.Secure;
import android.graphics.Bitmap;
import android.content.res.AssetManager;

import android.content.Intent;
import android.net.Uri;
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
        public String invoke(String fn, String[] params) {
            return invokeNative(name, fn, params);
        }
    };

    private final String TAG;
    private WebView webView;
    private Handler mainHandler;
    private nproxy consoleobj = null;

    private native void initNative(String tag, String[] params, AssetManager assetManager);
    private native void exitNative();
    private native void initWindow();
    private native void initPage(String url);
    private native String invokeNative(String obj, String fn, String[] params);
    private native Object[] getPageData(String url);

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

    public void connect(WebView wv, Handler mh) {
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
                //Log.d(TAG, "pstart:" + url);
            }
            @Override
            public void onPageFinished(WebView view, String url) {
                //Log.d(TAG, "pfinish:" + url);
            }

            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, String url) {
                //Log.d(TAG, "shouldInterceptRequest:" + url);
                final Uri uri = Uri.parse(url);
                final String scheme = uri.getScheme();
                if (scheme.equals("embedded")) {
                    Object data[] = getPageData(url);
                    if(data != null){
                        String mim = (String)data[0];
                        byte[] dat = (byte[])data[1];
                        ByteArrayInputStream is = new ByteArrayInputStream(dat);
                        String enc = "UTF-8";
                        if(mim.equals("image/png")){
                            enc = "binary";
                        }
                        Log.d(TAG, "webres:" + url + ":" + enc);
                        return new WebResourceResponse(mim, enc, is);
                    }

                }
                return null;
            }
        });
        initWindow();
        //Log.d(TAG, "connected");
    }

    public void disconnect() {
        this.webView = null;
        this.mainHandler = null;
    }

    public wui(String tg, String[] params, AssetManager assetManager) {
        this.TAG = tg;
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
        if(mainHandler == null){
            Log.d(TAG, "mainHandler is empty in go_embedded:" + url);
            return;
        }
        mainHandler.post(new Runnable() {
            public void run() {
                insertObjects();
                Log.d(TAG, "loadDataWithBaseURL:" + url);
                webView.loadDataWithBaseURL(url, data, mimetype, "utf-8", null);
                insertObjectBody();
                initPage(url);
            }
        });
    }

    public void go_standard(final String url) {
        if(mainHandler == null){
            Log.d(TAG, "mainHandler is empty in go_standard:" + url);
            return;
        }
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
