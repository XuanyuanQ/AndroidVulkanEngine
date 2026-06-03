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

    public static void setTexture(String objectId, String texture) {
        AveActivity.jniSetObjectTexture(objectId, texture);
    }

    public static void setProgress(String objectId, float value) {
        AveActivity.jniSetObjectProgress(objectId, value);
    }

    public static float[] getPosition(String objectId) {
        return AveActivity.jniGetObjectPosition(objectId);
    }

    public static float[] getRotation(String objectId) {
        return AveActivity.jniGetObjectRotation(objectId);
    }

    public static float[] getScale(String objectId) {
        return AveActivity.jniGetObjectScale(objectId);
    }

    public static boolean getVisible(String objectId) {
        return AveActivity.jniGetObjectVisible(objectId);
    }

    public static float[] getColor(String objectId) {
        return AveActivity.jniGetObjectColor(objectId);
    }

    public static String getTexture(String objectId) {
        return AveActivity.jniGetObjectTexture(objectId);
    }

    public static float getProgress(String objectId) {
        return AveActivity.jniGetObjectProgress(objectId);
    }

    public static String instantiatePrefab(String prefabPath, String parentId) {
        return AveActivity.jniInstantiatePrefab(prefabPath, parentId);
    }

    public static String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return AveActivity.jniInstantiatePrefab(prefabPath, parentId, x, y, z);
    }
}
