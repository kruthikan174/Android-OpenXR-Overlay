package com.example.addr1;

import android.app.NativeActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

public class MainActivity extends NativeActivity {
    private static final String TAG = "OverlayAppGreen";

    static {
        System.loadLibrary("overlay_app_cpp");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "OverlayAppGreen onCreate started");

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);

        Log.i(TAG, "Set FLAG_NOT_FOCUSABLE and FLAG_NOT_TOUCHABLE on overlay window.");
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.i(TAG, "OverlayAppGreen onResume");
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.i(TAG, "OverlayAppGreen onPause");
    }
}
