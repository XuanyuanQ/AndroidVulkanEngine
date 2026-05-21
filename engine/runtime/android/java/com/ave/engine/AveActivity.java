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
    private boolean mIsRightButtonPressed = false;

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        // 检查是否是鼠标事件，且按下了右键 (BUTTON_SECONDARY 就是鼠标右键)
        boolean rightClick = (event.getButtonState() & MotionEvent.BUTTON_SECONDARY) != 0;
        int action = event.getActionMasked();

        if (action == MotionEvent.ACTION_DOWN || rightClick) {
            if (!mIsRightButtonPressed) {
                mIsRightButtonPressed = true;
                mLastX = event.getX();
                mLastY = event.getY();
            }
            
            // 计算鼠标移动的相对偏差 (Delta)
            float dx = event.getX() - mLastX;
            float dy = event.getY() - mLastY;
            
            mLastX = event.getX();
            mLastY = event.getY();

            // 如果鼠标在移动，把偏差发给 C++
            if (action == MotionEvent.ACTION_MOVE && (Math.abs(dx) > 0.1f || Math.abs(dy) > 0.1f)) {
                nativeMotionEvent(dx, dy, action);
            }
            return true;
        } else {
            mIsRightButtonPressed = false;
        }

        return super.onGenericMotionEvent(event);
    }


    private static native void nativeCreate(android.content.res.AssetManager assets, String projectPath);
    private static native void nativeDestroy();
    private static native void nativeSetSurface(Surface surface);
    private static native void nativeClearSurface();
    private static native void nativeKeyEvent(int keyCode, int action);
    private static native void nativeMotionEvent(float dx, float dy, int action);
    private static native void nativeResize(int width, int height);
}
