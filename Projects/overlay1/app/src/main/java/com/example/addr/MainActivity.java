package com.example.addr;

import android.app.NativeActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

public class MainActivity extends NativeActivity {
    private static final String TAG = "OverlayApp";

    static {
        System.loadLibrary("overlay_app_cpp");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "OverlayApp onCreate started");

        // *** THIS IS THE MOST IMPORTANT FIX ***
        // Set window flags to prevent this activity from taking focus from the base app.
        // FLAG_NOT_FOCUSABLE: This window won't ever get key input focus.
        // FLAG_NOT_TOUCH_MODAL: Events outside this window are sent to the window behind it.
        // FLAG_NOT_TOUCHABLE: This window can never receive touch events, ensuring focus stays behind.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);

        Log.i(TAG, "Set FLAG_NOT_FOCUSABLE and FLAG_NOT_TOUCHABLE on overlay window.");
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.i(TAG, "OverlayApp onResume");
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.i(TAG, "OverlayApp onPause");
    }
}