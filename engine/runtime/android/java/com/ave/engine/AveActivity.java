package com.ave.engine;

import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.KeyEvent;
import android.view.MotionEvent;

public class AveActivity extends Activity implements SurfaceHolder.Callback {
    private SurfaceView surfaceView;

    static {
        System.loadLibrary("ave_runtime");
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
        surfaceView.requestFocus(); // 强行把焦点抢过来
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
        android.util.Log.d("AVE_INPUT", "Java 收到按键: " + keyCode);
        if (keyCode == KeyEvent.KEYCODE_W || keyCode == KeyEvent.KEYCODE_A || 
            keyCode == KeyEvent.KEYCODE_S || keyCode == KeyEvent.KEYCODE_D) {
            
            nativeKeyEvent(keyCode, 0); // 0 代表按下 ACTION_DOWN
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_W || keyCode == KeyEvent.KEYCODE_A || 
            keyCode == KeyEvent.KEYCODE_S || keyCode == KeyEvent.KEYCODE_D) {
            
            nativeKeyEvent(keyCode, 1); // 1 代表抬起 ACTION_UP
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    // 2. 响应鼠标右键拖动事件
    private float mLastX = 0;
    private float mLastY = 0;
    private boolean mHasLastTouch = false;

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                mLastX = event.getX();
                mLastY = event.getY();
                mHasLastTouch = true;
                break;

            case MotionEvent.ACTION_MOVE:
                if (!mHasLastTouch) {
                    mLastX = event.getX();
                    mLastY = event.getY();
                    mHasLastTouch = true;
                    break;
                }
                float dx = event.getX() - mLastX;
                float dy = event.getY() - mLastY;

                mLastX = event.getX();
                mLastY = event.getY();

                // 过滤掉极其微小的抖动
                if (Math.abs(dx) > 0.1f || Math.abs(dy) > 0.1f) {
                    android.util.Log.d("AVE_INPUT_DEBUG", "🔴 屏幕划动中 -> dx: " + dx + ", dy: " + dy);
                    // 把偏移量丢给 C++ 
                    nativeMotionEvent(dx, dy, action);
                }
                break;
                
            case MotionEvent.ACTION_UP:
                nativeTouchEvent(event.getX(), event.getY(), action);
            case MotionEvent.ACTION_CANCEL:
                mHasLastTouch = false;
                break;
        }

        // 依然让系统底层继续处理该事件
        return super.dispatchTouchEvent(event);
    }


    private static final AveScriptManager scriptManager = new AveScriptManager();

    // Invoked by C++ via JNI to instantiate scripts
    public static void jniInstantiateScript(String objectId, String className) {
        scriptManager.instantiateScript(objectId, className);
    }

    // Invoked by C++ via JNI on every frame update
    public static void jniUpdateScripts(float dt) {
        scriptManager.update(dt);
    }

    // Invoked by C++ via JNI for UI Button click events
    public static void jniTriggerScriptMethod(String target, String method) {
        scriptManager.triggerScriptMethod(target, method);
    }

    // Invoked by C++ via JNI when scene is destroyed
    public static void jniClearScripts() {
        scriptManager.clear();
    }

    static void jniSetObjectPosition(String objectId, float x, float y, float z) {
        nativeSetObjectPosition(objectId, x, y, z);
    }

    static void jniSetObjectVisible(String objectId, boolean visible) {
        nativeSetObjectVisible(objectId, visible);
    }

    static void jniSetObjectColor(String objectId, float r, float g, float b, float a) {
        nativeSetObjectColor(objectId, r, g, b, a);
    }

    private static native void nativeCreate(android.content.res.AssetManager assets, String projectPath);
    private static native void nativeDestroy();
    private static native void nativeSetSurface(Surface surface);
    private static native void nativeClearSurface();
    private static native void nativeKeyEvent(int keyCode, int action);
    private static native void nativeMotionEvent(float dx, float dy, int action);
    private static native void nativeTouchEvent(float x, float y, int action);
    private static native void nativeSetObjectPosition(String objectId, float x, float y, float z);
    private static native void nativeSetObjectVisible(String objectId, boolean visible);
    private static native void nativeSetObjectColor(String objectId, float r, float g, float b, float a);
    private static native void nativeResize(int width, int height);
}
