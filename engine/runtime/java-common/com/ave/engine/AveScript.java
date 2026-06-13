package com.ave.engine;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public class AveScript {
    private String objectId = "";
    private String targetObjectId = "";
    private Map<String, String> params = Collections.emptyMap();
    private boolean destroyed = false;

    public final void __bindObject(String id) {
        objectId = id != null ? id : "";
    }

    public final void __bindParams(String targetId, Map<String, String> scriptParams) {
        targetObjectId = targetId != null ? targetId : objectId;
        params = scriptParams != null ? new HashMap<>(scriptParams) : Collections.emptyMap();
    }

    protected final String getObjectId() {
        return objectId;
    }

    public final boolean __isDestroyed() {
        return destroyed;
    }

    protected final String getTargetObjectId() {
        return targetObjectId.isEmpty() ? objectId : targetObjectId;
    }

    protected final String getParam(String name) {
        return params.getOrDefault(name, "");
    }

    protected final String getParam(String name, String fallback) {
        return params.getOrDefault(name, fallback);
    }

    public void start() {}
    public void update(float dt) {}
    public void onClick(String target) {}
    public void onValueChanged(String sourceId, float value) {}

    protected final void setPosition(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setPosition(target, x, y, z);
    }

    protected final void setRotation(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setRotation(target, x, y, z);
    }

    protected final void setScale(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setScale(target, x, y, z);
    }

    protected final void setVisible(boolean visible) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setVisible(target, visible);
    }

    protected final void setColor(float r, float g, float b, float a) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setColor(target, r, g, b, a);
    }

    protected final void setTexture(String texture) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setTexture(target, texture);
    }

    protected final void setText(String text) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setText(target, text);
    }

    protected final void setProgress(float value) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) AveObjectController.setProgress(target, value);
    }

    protected final float[] getPosition() {
        String target = getTargetObjectId();
        return target.isEmpty() ? new float[] {0.0f, 0.0f, 0.0f} : AveObjectController.getPosition(target);
    }

    protected final float[] getRotation() {
        String target = getTargetObjectId();
        return target.isEmpty() ? new float[] {0.0f, 0.0f, 0.0f} : AveObjectController.getRotation(target);
    }

    protected final float[] getScale() {
        String target = getTargetObjectId();
        return target.isEmpty() ? new float[] {1.0f, 1.0f, 1.0f} : AveObjectController.getScale(target);
    }

    protected final boolean getVisible() {
        String target = getTargetObjectId();
        return !target.isEmpty() && AveObjectController.getVisible(target);
    }

    protected final float[] getColor() {
        String target = getTargetObjectId();
        return target.isEmpty() ? new float[] {1.0f, 1.0f, 1.0f, 1.0f} : AveObjectController.getColor(target);
    }

    protected final String getTexture() {
        String target = getTargetObjectId();
        return target.isEmpty() ? "" : AveObjectController.getTexture(target);
    }

    protected final float getProgress() {
        String target = getTargetObjectId();
        return target.isEmpty() ? 0.0f : AveObjectController.getProgress(target);
    }

    protected final void log(String message) {
        LogUtil.log(message);
    }

    protected final void destroySelf() {
        String target = getObjectId();
        if (!target.isEmpty()) destroyed = AveObjectController.destroyObject(target);
    }

    protected final void destroyObject(String objectId) {
        if (objectId != null && !objectId.isEmpty()) AveObjectController.destroyObject(objectId);
    }
}
