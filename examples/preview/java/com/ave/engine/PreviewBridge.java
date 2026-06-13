package com.ave.engine;

import java.util.HashMap;
import java.util.Map;

public final class PreviewBridge {
    public static final class ObjectState {
        final float[] position = new float[] {0.0f, 0.0f, 0.0f};
        final float[] rotation = new float[] {0.0f, 0.0f, 0.0f};
        final float[] scale = new float[] {1.0f, 1.0f, 1.0f};
        final float[] color = new float[] {1.0f, 1.0f, 1.0f, 1.0f};
        boolean visible = true;
        String texture = "";
        float progress = 0.0f;
    }

    private static final Map<String, ObjectState> objects = new HashMap<>();
    private static int prefabCounter = 0;

    private PreviewBridge() {
    }

    public static void clearObjects() {
        objects.clear();
        prefabCounter = 0;
    }

    public static void defineObject(String objectId,
                                    float px,
                                    float py,
                                    float pz,
                                    float rx,
                                    float ry,
                                    float rz,
                                    float sx,
                                    float sy,
                                    float sz) {
        ObjectState state = stateFor(objectId);
        state.position[0] = px;
        state.position[1] = py;
        state.position[2] = pz;
        state.rotation[0] = rx;
        state.rotation[1] = ry;
        state.rotation[2] = rz;
        state.scale[0] = sx;
        state.scale[1] = sy;
        state.scale[2] = sz;
    }

    public static void log(String message) {
        nativeLog(message != null ? message : "");
    }

    public static void setObjectPosition(String objectId, float x, float y, float z) {
        ObjectState state = stateFor(objectId);
        state.position[0] = x;
        state.position[1] = y;
        state.position[2] = z;
        nativeSetPosition(objectId != null ? objectId : "", x, y, z);
    }

    public static void setObjectRotation(String objectId, float x, float y, float z) {
        ObjectState state = stateFor(objectId);
        state.rotation[0] = x;
        state.rotation[1] = y;
        state.rotation[2] = z;
        nativeSetRotation(objectId != null ? objectId : "", x, y, z);
    }

    public static void setObjectScale(String objectId, float x, float y, float z) {
        ObjectState state = stateFor(objectId);
        state.scale[0] = x;
        state.scale[1] = y;
        state.scale[2] = z;
        nativeSetScale(objectId != null ? objectId : "", x, y, z);
    }

    public static void setObjectVisible(String objectId, boolean visible) {
        stateFor(objectId).visible = visible;
        nativeSetVisible(objectId != null ? objectId : "", visible);
    }

    public static void setObjectColor(String objectId, float r, float g, float b, float a) {
        ObjectState state = stateFor(objectId);
        state.color[0] = r;
        state.color[1] = g;
        state.color[2] = b;
        state.color[3] = a;
        nativeSetColor(objectId != null ? objectId : "", r, g, b, a);
    }

    public static void setObjectTexture(String objectId, String texture) {
        stateFor(objectId).texture = texture != null ? texture : "";
        nativeSetTexture(objectId != null ? objectId : "", stateFor(objectId).texture);
    }

    public static void setObjectText(String objectId, String text) {
        nativeSetText(objectId != null ? objectId : "", text != null ? text : "");
    }

    public static void setObjectProgress(String objectId, float value) {
        stateFor(objectId).progress = value;
        nativeSetProgress(objectId != null ? objectId : "", value);
    }

    public static boolean destroyObject(String objectId) {
        objects.remove(objectId);
        return nativeDestroyObject(objectId != null ? objectId : "");
    }

    public static String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        String id = "__java_prefab_" + (++prefabCounter);
        defineObject(id, x, y, z, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        nativeInstantiatePrefab(id, prefabPath != null ? prefabPath : "", parentId != null ? parentId : "", x, y, z);
        return id;
    }

    public static float[] getObjectPosition(String objectId) {
        return stateFor(objectId).position.clone();
    }

    public static float[] getObjectRotation(String objectId) {
        return stateFor(objectId).rotation.clone();
    }

    public static float[] getObjectScale(String objectId) {
        return stateFor(objectId).scale.clone();
    }

    public static boolean getObjectVisible(String objectId) {
        return stateFor(objectId).visible;
    }

    public static float[] getObjectColor(String objectId) {
        return stateFor(objectId).color.clone();
    }

    public static String getObjectTexture(String objectId) {
        return stateFor(objectId).texture;
    }

    public static float getObjectProgress(String objectId) {
        return stateFor(objectId).progress;
    }

    private static ObjectState stateFor(String objectId) {
        return objects.computeIfAbsent(objectId != null ? objectId : "", ignored -> new ObjectState());
    }

    private static native void nativeLog(String message);
    private static native void nativeSetPosition(String objectId, float x, float y, float z);
    private static native void nativeSetRotation(String objectId, float x, float y, float z);
    private static native void nativeSetScale(String objectId, float x, float y, float z);
    private static native void nativeSetVisible(String objectId, boolean visible);
    private static native void nativeSetColor(String objectId, float r, float g, float b, float a);
    private static native void nativeSetTexture(String objectId, String texture);
    private static native void nativeSetText(String objectId, String text);
    private static native void nativeSetProgress(String objectId, float value);
    private static native boolean nativeDestroyObject(String objectId);
    private static native void nativeInstantiatePrefab(String requestedId, String prefabPath, String parentId, float x, float y, float z);
}
