package com.ave.engine;

public final class PreviewAveRuntimeBridge implements AveRuntimeBridge {
    @Override public void setObjectPosition(String objectId, float x, float y, float z) { PreviewBridge.setObjectPosition(objectId, x, y, z); }
    @Override public void setObjectRotation(String objectId, float x, float y, float z) { PreviewBridge.setObjectRotation(objectId, x, y, z); }
    @Override public void setObjectScale(String objectId, float x, float y, float z) { PreviewBridge.setObjectScale(objectId, x, y, z); }
    @Override public void setObjectVisible(String objectId, boolean visible) { PreviewBridge.setObjectVisible(objectId, visible); }
    @Override public void setObjectColor(String objectId, float r, float g, float b, float a) { PreviewBridge.setObjectColor(objectId, r, g, b, a); }
    @Override public void setObjectTexture(String objectId, String texture) { PreviewBridge.setObjectTexture(objectId, texture); }
    @Override public void setObjectText(String objectId, String text) { PreviewBridge.setObjectText(objectId, text); }
    @Override public void setObjectProgress(String objectId, float value) { PreviewBridge.setObjectProgress(objectId, value); }
    @Override public boolean destroyObject(String objectId) { return PreviewBridge.destroyObject(objectId); }
    @Override public float getObjectProgress(String objectId) { return PreviewBridge.getObjectProgress(objectId); }
    @Override public float[] getObjectPosition(String objectId) { return PreviewBridge.getObjectPosition(objectId); }
    @Override public float[] getObjectRotation(String objectId) { return PreviewBridge.getObjectRotation(objectId); }
    @Override public float[] getObjectScale(String objectId) { return PreviewBridge.getObjectScale(objectId); }
    @Override public boolean getObjectVisible(String objectId) { return PreviewBridge.getObjectVisible(objectId); }
    @Override public float[] getObjectColor(String objectId) { return PreviewBridge.getObjectColor(objectId); }
    @Override public String getObjectTexture(String objectId) { return PreviewBridge.getObjectTexture(objectId); }
    @Override public String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return PreviewBridge.instantiatePrefab(prefabPath, parentId, x, y, z);
    }
    @Override public void log(String message) { PreviewBridge.log(message); }
}
