package com.ave.engine;

public class AveActivity {
    public AveActivity() {
    }

    static void jniSetObjectPosition(String objectId, float x, float y, float z) {
        PreviewBridge.setObjectPosition(objectId, x, y, z);
    }

    static void jniSetObjectRotation(String objectId, float x, float y, float z) {
        PreviewBridge.setObjectRotation(objectId, x, y, z);
    }

    static void jniSetObjectScale(String objectId, float x, float y, float z) {
        PreviewBridge.setObjectScale(objectId, x, y, z);
    }

    static void jniSetObjectVisible(String objectId, boolean visible) {
        PreviewBridge.setObjectVisible(objectId, visible);
    }

    static void jniSetObjectColor(String objectId, float r, float g, float b, float a) {
        PreviewBridge.setObjectColor(objectId, r, g, b, a);
    }

    static void jniSetObjectTexture(String objectId, String texture) {
        PreviewBridge.setObjectTexture(objectId, texture);
    }

    static void jniSetObjectText(String objectId, String text) {
        PreviewBridge.setObjectText(objectId, text);
    }

    static void jniSetObjectProgress(String objectId, float value) {
        PreviewBridge.setObjectProgress(objectId, value);
    }

    static boolean jniDestroyObject(String objectId) {
        return PreviewBridge.destroyObject(objectId);
    }

    static float jniGetObjectProgress(String objectId) {
        return PreviewBridge.getObjectProgress(objectId);
    }

    static float[] jniGetObjectPosition(String objectId) {
        return PreviewBridge.getObjectPosition(objectId);
    }

    static float[] jniGetObjectRotation(String objectId) {
        return PreviewBridge.getObjectRotation(objectId);
    }

    static float[] jniGetObjectScale(String objectId) {
        return PreviewBridge.getObjectScale(objectId);
    }

    static boolean jniGetObjectVisible(String objectId) {
        return PreviewBridge.getObjectVisible(objectId);
    }

    static float[] jniGetObjectColor(String objectId) {
        return PreviewBridge.getObjectColor(objectId);
    }

    static String jniGetObjectTexture(String objectId) {
        return PreviewBridge.getObjectTexture(objectId);
    }

    public static String jniInstantiatePrefab(String prefabPath, String parentId) {
        return PreviewBridge.instantiatePrefab(prefabPath, parentId, 0.0f, 0.0f, 0.0f);
    }

    public static String jniInstantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return PreviewBridge.instantiatePrefab(prefabPath, parentId, x, y, z);
    }
}
