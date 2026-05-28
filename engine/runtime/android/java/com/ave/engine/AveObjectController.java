package com.ave.engine;

public final class AveObjectController {
    private AveObjectController() {
    }

    public static void setPosition(String objectId, float x, float y, float z) {
        AveActivity.jniSetObjectPosition(objectId, x, y, z);
    }

    public static void setRotation(String objectId, float x, float y, float z) {
        AveActivity.jniSetObjectRotation(objectId, x, y, z);
    }

    public static void setScale(String objectId, float x, float y, float z) {
        AveActivity.jniSetObjectScale(objectId, x, y, z);
    }

    public static void setVisible(String objectId, boolean visible) {
        AveActivity.jniSetObjectVisible(objectId, visible);
    }

    public static void setColor(String objectId, float r, float g, float b, float a) {
        AveActivity.jniSetObjectColor(objectId, r, g, b, a);
    }
}
