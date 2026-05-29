package com.ave.engine;

import android.app.Activity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class AveActivity extends Activity implements SurfaceHolder.Callback {
    private SurfaceView surfaceView;

    static {
        System.loadLibrary("ave_runtime");
    }
    private static final AveScriptManager scriptManager = new AveScriptManager();

    protected final SurfaceView getAveSurfaceView() {
        return surfaceView;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        setContentView(surfaceView);
        nativeCreate(getAssets(), "project.xml");
        surfaceView.setFocusable(true);
        surfaceView.setFocusableInTouchMode(true);
        surfaceView.requestFocus();
    }

    @Override
    protected void onDestroy() {
        nativeDestroy();
        super.onDestroy();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeSetSurface(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeResize(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeClearSurface();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_W || keyCode == KeyEvent.KEYCODE_A
                || keyCode == KeyEvent.KEYCODE_S || keyCode == KeyEvent.KEYCODE_D) {
            nativeKeyEvent(keyCode, 0);
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_W || keyCode == KeyEvent.KEYCODE_A
                || keyCode == KeyEvent.KEYCODE_S || keyCode == KeyEvent.KEYCODE_D) {
            nativeKeyEvent(keyCode, 1);
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        handleEngineTouchEvent(event);
        if (scriptManager.dispatchTouchEvent(this, event)) {
            return true;
        }
        return super.dispatchTouchEvent(event);
    }

    protected final void handleEngineTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN ||
                action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_CANCEL) {
            nativeTouchEvent(event.getX(), event.getY(), event.getActionMasked());
        }
    }

    public static void jniInstantiateScript(String objectId, String className, String targetObjectId, String[] paramKeys, String[] paramValues) {
        scriptManager.instantiateScript(objectId, className, targetObjectId, paramKeys, paramValues);
    }

    public static void jniUpdateScripts(float dt) {
        scriptManager.update(dt);
    }

    public static void jniTriggerScriptMethod(String target, String method) {
        scriptManager.triggerScriptMethod(target, method);
    }

    public static void jniClearScripts() {
        scriptManager.clear();
    }

    static void jniSetObjectPosition(String objectId, float x, float y, float z) {
        nativeSetObjectPosition(objectId, x, y, z);
    }

    static void jniSetObjectRotation(String objectId, float x, float y, float z) {
        nativeSetObjectRotation(objectId, x, y, z);
    }

    static void jniSetObjectScale(String objectId, float x, float y, float z) {
        nativeSetObjectScale(objectId, x, y, z);
    }

    static void jniSetObjectVisible(String objectId, boolean visible) {
        nativeSetObjectVisible(objectId, visible);
    }

    static void jniSetObjectColor(String objectId, float r, float g, float b, float a) {
        nativeSetObjectColor(objectId, r, g, b, a);
    }

    static boolean jniGetObjectVisible(String objectId) {
        return nativeGetObjectVisible(objectId);
    }

    private static native void nativeCreate(android.content.res.AssetManager assets, String projectPath);
    private static native void nativeDestroy();
    private static native void nativeSetSurface(Surface surface);
    private static native void nativeClearSurface();
    private static native void nativeKeyEvent(int keyCode, int action);
    private static native void nativeTouchEvent(float x, float y, int action);
    private static native void nativeSetObjectPosition(String objectId, float x, float y, float z);
    private static native void nativeSetObjectRotation(String objectId, float x, float y, float z);
    private static native void nativeSetObjectScale(String objectId, float x, float y, float z);
    private static native void nativeSetObjectVisible(String objectId, boolean visible);
    private static native void nativeSetObjectColor(String objectId, float r, float g, float b, float a);
    private static native void nativeResize(int width, int height);

    public static native boolean nativeGetObjectVisible(String objectId);
}
