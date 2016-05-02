package com.renjipanicker;

import android.webkit.JavascriptInterface;
import android.util.Log;

public class wui {
    private final String TAG;
    public class nproxy {
        @JavascriptInterface
        public void invoke(String obj, String fn, String[] params) {
            Log.d(TAG, "nproxy::invoke(" + obj + "." + fn + ", " + params + "); // java code");
            wui.this.invokeNative(obj, fn, params);
        }
    };

    public interface GoCB {
        void go_embedded(final String url, final String data, final String mimetype);
        void go_standard(final String url);
    };

    public wui(String tg) {
        this.TAG = tg;
    }

    public GoCB goCallBack;
    public native void initNative(String[] params);
    public native void exitNative();
    public native void initPage(String url);
    public native String[] handleRequest(String url);
    public native void invokeNative(String obj, String fn, String[] params);

    public void go_embedded(final String url, final String data, final String mimetype) {
        goCallBack.go_embedded(url, data, mimetype);
    }

    public void go_standard(final String url) {
        goCallBack.go_standard(url);
    }
};
