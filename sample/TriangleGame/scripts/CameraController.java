package com.example.trianglegame;

import android.view.MotionEvent;

import com.ave.engine.AveActivity;
import com.ave.engine.AveActivityEventHandler;
import com.ave.engine.AveObjectController;
import com.ave.engine.AveScript;

public final class CameraController extends AveScript implements AveActivityEventHandler {
    private static final int MODE_TRANSLATE = 0;
    private static final int MODE_ROTATE = 1;

    private int mode = MODE_TRANSLATE;
    private String modeButtonId = "switch_mode_button";
    private String translateTexture = "textures/tanslate.png";
    private String rotationTexture = "textures/rotation.png";

    private float translateSensitivity = 0.0015f;
    private float rotateSensitivity = 0.15f;
    private float zoomSensitivity = 0.02f;
    private float minPitch = -80.0f;
    private float maxPitch = 80.0f;
    private float minDistance = 1.0f;
    private float maxDistance = 30.0f;

    private float targetX = 0.0f;
    private float targetY = 0.0f;
    private float targetZ = 0.0f;
    private float distance = 8.0f;
    private float pitch = 0.0f;
    private float yaw = 0.0f;

    private float lastX = 0.0f;
    private float lastY = 0.0f;
    private float lastPinchDistance = 0.0f;
    private boolean singleDragging = false;
    private boolean pinching = false;

    @Override
    public void start() {
        modeButtonId = getParam("modeButton", modeButtonId);
        translateTexture = getParam("translateTexture", translateTexture);
        rotationTexture = getParam("rotationTexture", rotationTexture);
        translateSensitivity = readFloatParam("translateSensitivity", translateSensitivity);
        rotateSensitivity = readFloatParam("rotateSensitivity", rotateSensitivity);
        zoomSensitivity = readFloatParam("zoomSensitivity", zoomSensitivity);
        minPitch = readFloatParam("minPitch", minPitch);
        maxPitch = readFloatParam("maxPitch", maxPitch);
        minDistance = readFloatParam("minDistance", readFloatParam("minZ", minDistance));
        maxDistance = readFloatParam("maxDistance", readFloatParam("maxZ", maxDistance));
        targetX = readFloatParam("targetX", targetX);
        targetY = readFloatParam("targetY", targetY);
        targetZ = readFloatParam("targetZ", targetZ);

        float[] position = getPosition();
        position[0] = readFloatParam("x", position[0]);
        position[1] = readFloatParam("y", position[1]);
        position[2] = readFloatParam("z", position[2]);

        float offsetX = position[0] - targetX;
        float offsetY = position[1] - targetY;
        float offsetZ = position[2] - targetZ;
        distance = clamp(length(offsetX, offsetY, offsetZ), minDistance, maxDistance);
        if (distance > 0.0001f) {
            float forwardX = -offsetX / distance;
            float forwardY = -offsetY / distance;
            float forwardZ = -offsetZ / distance;
            pitch = (float) Math.toDegrees(Math.asin(clamp(forwardY, -1.0f, 1.0f)));
            yaw = (float) Math.toDegrees(Math.atan2(-forwardX, -forwardZ));
        }

        pitch = readFloatParam("pitch", pitch);
        yaw = readFloatParam("yaw", yaw);

        applyCameraTransform();
        applyModeTexture();
        log("Camera script started");
    }

    @Override
    public void update(float dt) {
    }

    @Override
    public boolean dispatchTouchEvent(AveActivity activity, MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                beginSingleDrag(event.getX(), event.getY());
                return true;

            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.getPointerCount() < 2) {
                    return false;
                }
                pinching = true;
                singleDragging = false;
                lastPinchDistance = distance(event);
                return true;

            case MotionEvent.ACTION_MOVE:
                if (event.getPointerCount() >= 2) {
                    updatePinchZoom(event);
                    return true;
                }
                updateSingleDrag(event.getX(), event.getY());
                return true;

            case MotionEvent.ACTION_POINTER_UP:
                pinching = false;
                if (event.getPointerCount() <= 2) {
                    int remainingIndex = event.getActionIndex() == 0 ? 1 : 0;
                    beginSingleDrag(event.getX(remainingIndex), event.getY(remainingIndex));
                }
                return true;

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                singleDragging = false;
                pinching = false;
                return true;

            default:
                return false;
        }
    }

    public void switchMode() {
        mode = mode == MODE_TRANSLATE ? MODE_ROTATE : MODE_TRANSLATE;
        applyModeTexture();
        log(mode == MODE_TRANSLATE ? "Camera mode: translate" : "Camera mode: rotate");
    }

    @Override
    public void onClick(String target) {
        if ("switchMode".equals(target) || "interact".equals(target)) {
            switchMode();
        }
    }

    private void beginSingleDrag(float x, float y) {
        lastX = x;
        lastY = y;
        singleDragging = true;
    }

    private void updateSingleDrag(float x, float y) {
        if (!singleDragging || pinching) {
            beginSingleDrag(x, y);
            return;
        }

        float rawDx = x - lastX;
        float rawDy = y - lastY;
        lastX = x;
        lastY = y;
        float dx = rawDy;
        float dy = rawDx;

        if (mode == MODE_TRANSLATE) {
            panCamera(dx, dy);
        } else {
            yaw -= dx * rotateSensitivity;
            pitch = clamp(pitch - dy * rotateSensitivity, minPitch, maxPitch);
        }
        applyCameraTransform();
    }

    private void updatePinchZoom(MotionEvent event) {
        float currentDistance = distance(event);
        if (!pinching || lastPinchDistance <= 0.0f) {
            pinching = true;
            lastPinchDistance = currentDistance;
            return;
        }

        float delta = currentDistance - lastPinchDistance;
        lastPinchDistance = currentDistance;
        distance = clamp(distance - delta * zoomSensitivity, minDistance, maxDistance);
        applyCameraTransform();
    }

    private void applyCameraTransform() {
        float[] forward = forwardVector();
        setPosition(
                targetX - forward[0] * distance,
                targetY - forward[1] * distance,
                targetZ - forward[2] * distance);
        setRotation(pitch, yaw, 0.0f);
    }

    private void applyModeTexture() {
        if (!modeButtonId.isEmpty()) {
            AveObjectController.setTexture(modeButtonId, mode == MODE_TRANSLATE ? translateTexture : rotationTexture);
        }
    }

    private void panCamera(float dx, float dy) {
        float[] forward = forwardVector();
        float[] right = normalize(cross(forward, new float[] {0.0f, 1.0f, 0.0f}));
        float[] up = normalize(cross(right, forward));
        float scale = translateSensitivity * distance;

        targetX += (-right[0] * dx + up[0] * dy) * scale;
        targetY += (-right[1] * dx + up[1] * dy) * scale;
        targetZ += (-right[2] * dx + up[2] * dy) * scale;
    }

    private float[] forwardVector() {
        double yawRad = Math.toRadians(yaw);
        double pitchRad = Math.toRadians(pitch);
        float cosPitch = (float) Math.cos(pitchRad);
        return normalize(new float[] {
                -(float) Math.sin(yawRad) * cosPitch,
                (float) Math.sin(pitchRad),
                -(float) Math.cos(yawRad) * cosPitch
        });
    }

    private float[] cross(float[] a, float[] b) {
        return new float[] {
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]
        };
    }

    private float[] normalize(float[] value) {
        float len = length(value[0], value[1], value[2]);
        if (len <= 0.0001f) {
            return new float[] {0.0f, 0.0f, -1.0f};
        }
        return new float[] {value[0] / len, value[1] / len, value[2] / len};
    }

    private float length(float x, float y, float z) {
        return (float) Math.sqrt(x * x + y * y + z * z);
    }

    private float distance(MotionEvent event) {
        if (event.getPointerCount() < 2) {
            return 0.0f;
        }
        float dx = event.getX(0) - event.getX(1);
        float dy = event.getY(0) - event.getY(1);
        return (float) Math.sqrt(dx * dx + dy * dy);
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
