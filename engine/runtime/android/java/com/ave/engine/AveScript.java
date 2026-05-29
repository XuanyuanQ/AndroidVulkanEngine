package com.ave.engine;

import android.util.Log;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public class AveScript {
    private String objectId = "";
    private String targetObjectId = "";
    private Map<String, String> params = Collections.emptyMap();

    final void __bindObject(String id) {
        objectId = id != null ? id : "";
    }

    final void __bindParams(String targetId, Map<String, String> scriptParams) {
        targetObjectId = targetId != null ? targetId : objectId;
        params = scriptParams != null ? new HashMap<>(scriptParams) : Collections.emptyMap();
    }

    protected final String getObjectId() {
        return objectId;
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

    public void start() {
    }

    public void update(float dt) {
    }

    public void onClick(String target) {
    }

    protected final void setPosition(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) {
            AveObjectController.setPosition(target, x, y, z);
        }
    }

    protected final void setRotation(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) {
            AveObjectController.setRotation(target, x, y, z);
        }
    }

    protected final void setScale(float x, float y, float z) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) {
            AveObjectController.setScale(target, x, y, z);
        }
    }

    protected final void setVisible(boolean visible) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) {
            AveObjectController.setVisible(target, visible);
        }
    }

    protected final void setColor(float r, float g, float b, float a) {
        String target = getTargetObjectId();
        if (!target.isEmpty()) {
            AveObjectController.setColor(target, r, g, b, a);
        }
    }

    protected final boolean getVisible() {
        String target = getTargetObjectId();
        
        // 1. 检查目标 ID 是否合法
        if (target != null && !target.isEmpty()) {
            // 调用修改后的 native 方法，它现在直接返回 boolean
            return AveObjectController.getVisible(target);
        }
        
        // 2. 补全兜底返回，如果 target 为空，默认返回 false
        LogUtil.log("target ID does not exist " + target);
        return false;
    }

    

    protected final void log(String message) {
        LogUtil.log(message);
    }
}
