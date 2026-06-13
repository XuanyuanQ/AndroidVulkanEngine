package com.ave.engine;

public interface AveRuntimeBridge {
    void setObjectPosition(String objectId, float x, float y, float z);
    void setObjectRotation(String objectId, float x, float y, float z);
    void setObjectScale(String objectId, float x, float y, float z);
    void setObjectVisible(String objectId, boolean visible);
    void setObjectColor(String objectId, float r, float g, float b, float a);
    void setObjectTexture(String objectId, String texture);
    void setObjectText(String objectId, String text);
    void setObjectProgress(String objectId, float value);
    boolean destroyObject(String objectId);
    float getObjectProgress(String objectId);
    float[] getObjectPosition(String objectId);
    float[] getObjectRotation(String objectId);
    float[] getObjectScale(String objectId);
    boolean getObjectVisible(String objectId);
    float[] getObjectColor(String objectId);
    String getObjectTexture(String objectId);
    String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z);
    void log(String message);
}
