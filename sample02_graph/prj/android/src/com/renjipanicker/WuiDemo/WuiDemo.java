package com.renjipanicker;

import android.app.Activity;
import android.webkit.WebView;
import android.webkit.WebSettings;
import android.webkit.ConsoleMessage;
import android.webkit.WebSettings.RenderPriority;
import android.webkit.WebViewClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebChromeClient;
import android.webkit.JsResult;
import android.webkit.JavascriptInterface;
import android.provider.Settings.Secure;
import android.graphics.Bitmap;

import android.content.res.AssetManager;

import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayInputStream;

import android.util.Log;

public class WuiDemo extends Activity {
    private static final String TAG = "WuiLog";
    private WebView primaryWebView;
    private Handler mainHandler;
    private wui w;

    static {
        System.loadLibrary("WuiDemoLib");
    }

    public class napp {
        @JavascriptInterface
        public void invoke(String obj, String fn, String[] params) {
            Log.d(TAG, "nproxy::invoke(" + obj + "." + fn + ", " + params + "); // java code");
            w.invokeNative(obj, fn, params);
        }

    }

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        w = new wui();

        w.goCallBack = new wui.GoCB() {
            @Override
            public void go_embedded(final String url, final String data, final String mimetype){
                Log.d(TAG, "go_embedded:" + url);
                mainHandler.post(new Runnable() {
                    public void run() {
                        primaryWebView.loadDataWithBaseURL("embedded:", data, mimetype, "utf-8", null);
                    }
                });
            }

            @Override
            public void go_standard(final String url){
                Log.d(TAG, "go_standard:[" + url + "]");
                mainHandler.post(new Runnable() {
                    public void run() {
                        primaryWebView.loadUrl(url);
                    }
                });
            }
        };

        primaryWebView=new WebView(this);
        setContentView(primaryWebView);

        primaryWebView.getSettings().setJavaScriptEnabled(true);

        primaryWebView.getSettings().setRenderPriority(RenderPriority.HIGH);
        primaryWebView.getSettings().setCacheMode(WebSettings.LOAD_NO_CACHE);

        primaryWebView.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                String url = request.getUrl().toString();
                Log.d(TAG,"shouldInterceptRequest: " + url);
                String[] arr = w.handleRequest(url);
                if(arr == null){
                    Log.d(TAG,"-shouldInterceptRequest-X: " + url);
                    return super.shouldInterceptRequest(view, request);
                }
                Log.d(TAG,"-shouldInterceptRequestD:[[" + arr[1].length() + "]]:" + url);
                return new WebResourceResponse(arr[0], "utf-8", new ByteArrayInputStream(arr[1].getBytes()));
            }

            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                Log.d(TAG, "pstart:" + url);
		        w.initPage(url);
		    }
		});

        primaryWebView.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onJsAlert(WebView view, String url, String message, JsResult result) {
                Log.d(TAG, "alert(" + url + "):" + message);
                return super.onJsAlert(view, url, message, result);
            }
            @Override
            public boolean onConsoleMessage(ConsoleMessage cm) {
                Log.d(TAG, cm.message() + " -- From line " + cm.lineNumber() + " of " + cm.sourceId());
                return true;
            }
        });

        primaryWebView.addJavascriptInterface(new napp(), "nproxy");
        String extDir = Environment.getExternalStorageDirectory().toString();
        String dataDir = extDir + "/" + this.getPackageName();
        Log.d(TAG, "dataDir:" + dataDir);

        mainHandler = new Handler(this.getMainLooper());
        String[] params = new String[3];
        params[0] = "WuiDemo";
        params[1] = "-d";
        params[2] = dataDir;
        w.initNative(params);
    }

    /** Called when the activity is destroyed. */
    @Override
    public void onDestroy() {
        super.onDestroy();
        w.exitNative();
    }

    @Override
    public void onBackPressed() {
        moveTaskToBack(true);
    }
}

