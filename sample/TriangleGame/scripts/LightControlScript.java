package com.example.trianglegame;

import android.view.MotionEvent;

import com.ave.engine.AveActivity;
import com.ave.engine.AveActivityEventHandler;
import com.ave.engine.AveObjectController;
import com.ave.engine.AveScript;

public final class LightControlScript extends AveScript implements AveActivityEventHandler {
    private String lightId = "KeyLight";
    private String autoButtonId = "light_auto_button";
    private String xSliderId = "light_x_slider";
    private String ySliderId = "light_y_slider";
    private String zSliderId = "light_z_slider";

    private boolean autoRotate = true;
    private String activeSlider = "";
    private float angle = 0.0f;
    private float rotateSpeed = 0.8f;
    private float radius = 6.0f;

    private float minX = -8.0f;
    private float maxX = 8.0f;
    private float minY = 0.5f;
    private float maxY = 10.0f;
    private float minZ = -8.0f;
    private float maxZ = 8.0f;

    private float lightX = 0.0f;
    private float lightY = 6.0f;
    private float lightZ = 6.0f;

    @Override
    public void start() {
        lightId = getParam("light", lightId);
        autoButtonId = getParam("autoButton", autoButtonId);
        xSliderId = getParam("xSlider", xSliderId);
        ySliderId = getParam("ySlider", ySliderId);
        zSliderId = getParam("zSlider", zSliderId);
        autoRotate = Boolean.parseBoolean(getParam("auto", "true"));
        rotateSpeed = readFloatParam("speed", rotateSpeed);
        radius = readFloatParam("radius", radius);
        minX = readFloatParam("minX", minX);
        maxX = readFloatParam("maxX", maxX);
        minY = readFloatParam("minY", minY);
        maxY = readFloatParam("maxY", maxY);
        minZ = readFloatParam("minZ", minZ);
        maxZ = readFloatParam("maxZ", maxZ);

        float[] lightPosition = AveObjectController.getPosition(lightId);
        lightX = readFloatParam("x", lightPosition[0]);
        lightY = readFloatParam("y", lightPosition[1]);
        lightZ = readFloatParam("z", lightPosition[2]);
        angle = (float) Math.atan2(lightX, lightZ);

        applyLightPosition();
        updateSliderViews();
        updateButtonView();
        log("Light control script started");
    }

    @Override
    public void update(float dt) {
        if (!autoRotate) {
            return;
        }
        log(autoRotate ? "111Light auto rotate enabled" : "Light manual sliders enabled");

        angle += dt * rotateSpeed;
        lightX = (float) Math.sin(angle) * radius;
        lightZ = (float) Math.cos(angle) * radius;
        applyLightPosition();
        updateSliderViews();
    }

    public void toggleAuto() {
        autoRotate = !autoRotate;
        activeSlider = "";
        updateButtonView();
        log(autoRotate ? "Light auto rotate enabled" : "Light manual sliders enabled");
    }

    @Override
    public void onClick(String target) {
         log(  "switchTouch1"+target);
        if ("light_auto_button".equals(target)) {
            toggleAuto();
        }
    }

    @Override
    public boolean dispatchTouchEvent(AveActivity activity, MotionEvent event) {
        if (autoRotate) {
            return false;
        }
        // ---------- 新增日志 ----------
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: {
                log("dispatchTouchEvent ACTION_DOWN  x=" + event.getX() + " y=" + event.getY());
                activeSlider = hitSlider(activity, event.getX(), event.getY());
                log("hitSlider result='" + activeSlider + "'");
                if (activeSlider.isEmpty()) {
                    log("ACTION_DOWN no slider hit");
                    return false;
                }
                updateSliderFromTouch(activity, activeSlider, event.getX());
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                log("dispatchTouchEvent ACTION_MOVE activeSlider='" + activeSlider + "'");
                if (activeSlider.isEmpty()) {
                    log("ACTION_MOVE no active slider");
                    return false;
                }
                updateSliderFromTouch(activity, activeSlider, event.getX());
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                boolean handled = !activeSlider.isEmpty();
                log("dispatchTouchEvent ACTION_UP/CANCEL handled=" + handled);
                activeSlider = "";
                return handled;
            }
            default: {
                log("dispatchTouchEvent default action=" + event.getActionMasked());
                return false;
            }
        }
    }

    private void updateSliderFromTouch(AveActivity activity, String sliderId, float x) {
        float normalized = sliderNormalizedFromTouch(activity, sliderId, x);
        if (sliderId.equals(xSliderId)) {
            lightX = lerp(minX, maxX, normalized);
        } else if (sliderId.equals(ySliderId)) {
            lightY = lerp(minY, maxY, normalized);
        } else if (sliderId.equals(zSliderId)) {
            lightZ = lerp(minZ, maxZ, normalized);
        }
        angle = (float) Math.atan2(lightX, lightZ);
        applyLightPosition();
        updateSliderViews();
    }

    private void applyLightPosition() {
        AveObjectController.setPosition(lightId, lightX, lightY, lightZ);
    }

    private void updateSliderViews() {
        AveObjectController.setProgress(xSliderId, normalize(lightX, minX, maxX));
        AveObjectController.setProgress(ySliderId, normalize(lightY, minY, maxY));
        AveObjectController.setProgress(zSliderId, normalize(lightZ, minZ, maxZ));
    }

    private void updateButtonView() {
        if (autoRotate) {
            AveObjectController.setColor(autoButtonId, 0.18f, 0.62f, 1.0f, 0.90f);
        } else {
            AveObjectController.setColor(autoButtonId, 1.0f, 0.55f, 0.18f, 0.90f);
        }
    }

    private String hitSlider(AveActivity activity, float x, float y) {
        if (isInsideSlider(activity, xSliderId, x, y)) {
            return xSliderId;
        }
        if (isInsideSlider(activity, ySliderId, x, y)) {
            return ySliderId;
        }
        if (isInsideSlider(activity, zSliderId, x, y)) {
            return zSliderId;
        }
        return "";
    }

    private boolean isInsideSlider(AveActivity activity, String sliderId, float x, float y) {
       
        int width = activity.getWindow().getDecorView().getWidth();
        int height = activity.getWindow().getDecorView().getHeight();
        if (width <= 0 || height <= 0) {
            return false;
        }
        log(  "isInsideSlider"+sliderId);
        float[] position = AveObjectController.getPosition(sliderId);
        float[] scale = AveObjectController.getScale(sliderId);
        float ndcX = x / (float) width * 2.0f - 1.0f;
        float ndcY = 1.0f - y / (float) height * 2.0f;
        // 1. 修正屏幕宽高比对宽度的影响
        float aspect = height / (float) width;

        float halfWidth = Math.max(scale[0] * 0.60f, 0.10f) / aspect * 0.5f;

        float halfHeight = Math.max(scale[1] * 0.08f, 0.02f) * 1.8f;
        log("isInsideSlider width=" + width + " height=" + height);
        return ndcX >= position[0] - halfWidth && ndcX <= position[0] + halfWidth
                && ndcY >= position[1] - halfHeight && ndcY <= position[1] + halfHeight;
    }

    private float sliderNormalizedFromTouch(AveActivity activity, String sliderId, float x) {
        int width = activity.getWindow().getDecorView().getWidth();
        if (width <= 0) {
            return 0.0f;
        }

        float[] position = AveObjectController.getPosition(sliderId);
        float[] scale = AveObjectController.getScale(sliderId);
        float ndcX = x / (float) width * 2.0f - 1.0f;
        float aspect = activity.getWindow().getDecorView().getHeight() / (float) width;
        float halfWidth = Math.max(scale[0] * 0.60f, 0.10f) / aspect * 0.5f;
        if (halfWidth <= 0.0001f) {
            return 0.0f;
        }
        return clamp((ndcX - (position[0] - halfWidth)) / (halfWidth * 2.0f), 0.0f, 1.0f);
    }

    private float normalize(float value, float min, float max) {
        return clamp((value - min) / Math.max(max - min, 0.0001f), 0.0f, 1.0f);
    }

    private float lerp(float min, float max, float t) {
        return min + (max - min) * clamp(t, 0.0f, 1.0f);
    }

    private float readFloatParam(String name, float fallback) {
        try {
            return Float.parseFloat(getParam(name, Float.toString(fallback)));
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    private float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }
}
