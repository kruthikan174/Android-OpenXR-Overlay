package com.example.androidsamsung;

import android.app.NativeActivity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.util.HashSet;
import java.util.Set;

public class MainActivity extends NativeActivity {
    private static final String TAG = "BaseApp";
    // *** This array now includes all three overlay apps ***
    private static final String[] OVERLAY_PACKAGE_NAMES = {
            "com.example.addr",     // Magenta Overlay
            "com.example.addr1",    // Green Overlay
            "com.example.addr2",     // Blue Overlay
            "com.example.addr3"      //3D Overlay
            //"com.example.addr4",     //Yellow Overlay
            //"com.example.addr5",     //Cyan Overlay
            //"com.example.addr6",      //Orange Overlay
            //"com.example.addr7",        //Red Overlay
            //"com.example.addr8",       //Purple Overlay
            //"com.example.addr9"         //Tael Overlay
    };
    private Handler handler;
    private Set<String> launchedOverlays = new HashSet<>();

    public native boolean isSessionFocused();

    static {
        System.loadLibrary("base_app_cpp");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.d(TAG, "BaseApp onCreate started");
        handler = new Handler(Looper.getMainLooper());
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "BaseApp onResume");
        handler.post(this::tryLaunchOverlays);
    }

    private void tryLaunchOverlays() {
        if (launchedOverlays.size() >= OVERLAY_PACKAGE_NAMES.length) {
            return;
        }

        if (isSessionFocused()) {
            Log.i(TAG, "Base session is focused. Checking for overlays to launch.");

            for (String packageName : OVERLAY_PACKAGE_NAMES) {
                if (!launchedOverlays.contains(packageName)) {
                    try {
                        Log.i(TAG, "Attempting to launch: " + packageName);
                        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(packageName);
                        if (launchIntent != null) {
                            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                                    Intent.FLAG_ACTIVITY_MULTIPLE_TASK |
                                    Intent.FLAG_ACTIVITY_NO_HISTORY);
                            startActivity(launchIntent);
                            launchedOverlays.add(packageName);
                            Log.i(TAG, "Launch intent sent for: " + packageName);
                        } else {
                            Log.e(TAG, "Package not found: " + packageName);
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "Failed to launch " + packageName + ": " + e.getMessage(), e);
                    }
                }
            }
        } else {
            Log.d(TAG, "Session not focused yet, retrying in 1 second...");
            handler.postDelayed(this::tryLaunchOverlays, 1000);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.d(TAG, "BaseApp onPause");
    }
}