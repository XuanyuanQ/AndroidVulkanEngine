package com.ave.engine;

public final class AveObjectController {
    private AveObjectController() {
    }

    public static void setPosition(String objectId, float x, float y, float z) {
        AveRuntime.bridge().setObjectPosition(objectId, x, y, z);
    }

    public static void setRotation(String objectId, float x, float y, float z) {
        AveRuntime.bridge().setObjectRotation(objectId, x, y, z);
    }

    public static void setScale(String objectId, float x, float y, float z) {
        AveRuntime.bridge().setObjectScale(objectId, x, y, z);
    }

    public static void setVisible(String objectId, boolean visible) {
        AveRuntime.bridge().setObjectVisible(objectId, visible);
    }

    public static void setColor(String objectId, float r, float g, float b, float a) {
        AveRuntime.bridge().setObjectColor(objectId, r, g, b, a);
    }

    public static void setTexture(String objectId, String texture) {
        AveRuntime.bridge().setObjectTexture(objectId, texture);
    }

    public static void setText(String objectId, String text) {
        AveRuntime.bridge().setObjectText(objectId, text);
    }

    public static void setProgress(String objectId, float value) {
        AveRuntime.bridge().setObjectProgress(objectId, value);
    }

    public static float[] getPosition(String objectId) {
        return AveRuntime.bridge().getObjectPosition(objectId);
    }

    public static float[] getRotation(String objectId) {
        return AveRuntime.bridge().getObjectRotation(objectId);
    }

    public static float[] getScale(String objectId) {
        return AveRuntime.bridge().getObjectScale(objectId);
    }

    public static boolean getVisible(String objectId) {
        return AveRuntime.bridge().getObjectVisible(objectId);
    }

    public static float[] getColor(String objectId) {
        return AveRuntime.bridge().getObjectColor(objectId);
    }

    public static String getTexture(String objectId) {
        return AveRuntime.bridge().getObjectTexture(objectId);
    }

    public static float getProgress(String objectId) {
        return AveRuntime.bridge().getObjectProgress(objectId);
    }

    public static String instantiatePrefab(String prefabPath, String parentId) {
        return AveRuntime.bridge().instantiatePrefab(prefabPath, parentId, 0.0f, 0.0f, 0.0f);
    }

    public static String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return AveRuntime.bridge().instantiatePrefab(prefabPath, parentId, x, y, z);
    }

    public static boolean destroyObject(String objectId) {
        return AveRuntime.bridge().destroyObject(objectId);
    }
}
