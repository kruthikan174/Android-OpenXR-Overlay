package com.example.addr2;

import android.app.NativeActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

public class MainActivity extends NativeActivity {
    private static final String TAG = "OverlayAppBlue";

    static {
        System.loadLibrary("overlay_app_cpp");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "Blue OverlayApp onCreate started");

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);

        Log.i(TAG, "Set window flags for blue overlay.");
    }
}
