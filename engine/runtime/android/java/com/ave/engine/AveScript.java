package com.ave.engine;

import android.util.Log;

public class AveScript {
    private String objectId = "";

    final void __bindObject(String id) {
        objectId = id != null ? id : "";
    }

    public void start() {
    }

    public void update(float dt) {
    }

    public void onClick(String target) {
    }

    protected final void setPosition(float x, float y, float z) {
        if (!objectId.isEmpty()) {
            AveActivity.jniSetObjectPosition(objectId, x, y, z);
        }
    }

    protected final void setRotation(float x, float y, float z) {
        if (!objectId.isEmpty()) {
            AveActivity.jniSetObjectRotation(objectId, x, y, z);
        }
    }

    protected final void setScale(float x, float y, float z) {
        if (!objectId.isEmpty()) {
            AveActivity.jniSetObjectScale(objectId, x, y, z);
        }
    }

    protected final void setVisible(boolean visible) {
        if (!objectId.isEmpty()) {
            AveActivity.jniSetObjectVisible(objectId, visible);
        }
    }

    protected final void setColor(float r, float g, float b, float a) {
        if (!objectId.isEmpty()) {
            AveActivity.jniSetObjectColor(objectId, r, g, b, a);
        }
    }

    protected final void log(String message) {
        LogUtil.log(message);
    }
}
