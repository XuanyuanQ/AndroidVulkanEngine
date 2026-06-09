package com.example.trianglegame;

import com.ave.engine.AveObjectController;
import com.ave.engine.AveScript;

public final class LightControlScript extends AveScript {
    private String lightId = "KeyLight";
    private String autoButtonId = "light_auto_button";
    private String xSliderId = "light_x_slider";
    private String ySliderId = "light_y_slider";
    private String zSliderId = "light_z_slider";

    private boolean autoRotate = true;
    private float sun_time = 1.57f;
    private float rotateSpeed = 0.4f;
    private float radius = 8.0f;

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
        // angle = (float) Math.atan2(lightX, lightZ);

        applyLightPosition();
        updateSliderViews();
        // updateButtonView();
        log("Light control script started");
    }

    @Override
    public void update(float dt) {
        if (!autoRotate) {
            return;
        }

        sun_time += dt * rotateSpeed;
        // 2. 使用同一个 angle 变量控制三维轨迹
        // 限制 sun_time 产生的角度，让它在 0 到 180 度（仰角）之间循环，模拟白天的太阳
        // 如果想支持黑夜（灯光跑到地下），可以去掉 Math.abs
        float angle = sun_time; 

            // 3. 【太阳轨迹数学模型】
            // 假设太阳从东边（-radius, 0）升起，划过天空，落到西边（+radius, 0）
            lightX = (float) Math.cos(angle) * radius;
            
            // Math.abs 确保太阳永远在地平线（Y >= 0）以上，最高点就是 radius
            // 加上一个基础高度 1.5f，防止太阳完全贴地导致阴影拉伸到无限长而崩盘
            lightY = Math.abs((float) Math.sin(angle) * radius) + 1.5f; 
            
            // lightZ 可以保持静止，或者给一个微小的斜角偏移，让阴影方向更有层次感
            lightZ = (float) Math.sin(angle * 0.5f) * (radius * 0.5f);
        applyLightPosition();
        updateSliderViews();
    }

    public void toggleAuto() {
        autoRotate = !autoRotate;
        updateButtonView();
        log(autoRotate ? "Light auto rotate enabled" : "Light manual sliders enabled");
    }

    @Override
    public void onClick(String target) {
        if ("toggleAuto".equals(target) || "light_auto_button".equals(target)) {
            toggleAuto();
        }
    }

    public void setLightAxis(String sliderId, float value) {
        if (autoRotate) {
            toggleAuto();
        }
        if (sliderId.equals(xSliderId)) {
            lightX = value;
        } else if (sliderId.equals(ySliderId)) {
            lightY = value;
        } else if (sliderId.equals(zSliderId)) {
            lightZ = value;
        } else {
            return;
        }
        // angle = (float) Math.atan2(lightX, lightZ);
        applyLightPosition();
        updateSliderViews();
    }

    @Override
    public void onValueChanged(String sourceId, float value) {
        setLightAxis(sourceId, value);
    }

    private void applyLightPosition() {
        AveObjectController.setPosition(lightId, lightX, lightY, lightZ);
    }

    private void updateSliderViews() {
        AveObjectController.setProgress(xSliderId, clamp(lightX, minX, maxX));
        AveObjectController.setProgress(ySliderId, clamp(lightY, minY, maxY));
        AveObjectController.setProgress(zSliderId, clamp(lightZ, minZ, maxZ));
    }

    private void updateButtonView() {
        if (autoRotate) {
            AveObjectController.setColor(autoButtonId, 0.18f, 0.62f, 1.0f, 0.90f);
        } else {
            AveObjectController.setColor(autoButtonId, 1.0f, 0.55f, 0.18f, 0.90f);
        }
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
