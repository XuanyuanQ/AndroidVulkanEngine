package com.example.trianglegame;

import android.view.MotionEvent;

import com.ave.engine.AveActivity;
import com.ave.engine.AveActivityEventHandler;
import com.ave.engine.AveScript;

public final class CameraController extends AveScript implements AveActivityEventHandler {
    private float yaw = 0.0f;
    private float pitch = 0.0f;
    private float lastX = 0.0f;
    private float lastY = 0.0f;
    private boolean dragging = false;

    @Override
    public void start() {
        yaw = Float.parseFloat(getParam("yaw", "0"));
        pitch = Float.parseFloat(getParam("pitch", "0"));
        setRotation(yaw, pitch, 0.0f);
        log("Camera script started");
    }

    @Override
    public void update(float dt) {
    }

    @Override
    public boolean dispatchTouchEvent(AveActivity activity, MotionEvent event) {
        float x = event.getX();
        float y = event.getY();

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                lastX = x;
                lastY = y;
                dragging = true;
                return true;

            case MotionEvent.ACTION_MOVE:
                if (!dragging) {
                    return false;
                }

                float dx = x - lastX;
                float dy = y - lastY;
                lastX = x;
                lastY = y;

                yaw += dx * 0.15f;
                pitch -= dy * 0.15f;
                if (pitch > 89.0f) {
                    pitch = 89.0f;
                }
                if (pitch < -89.0f) {
                    pitch = -89.0f;
                }

                setRotation(yaw, pitch, 0.0f);
                return true;

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                dragging = false;
                return true;

            default:
                return false;
        }
    }
}
