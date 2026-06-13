package com.ave.engine;

final class AndroidAveRuntimeBridge implements AveRuntimeBridge {
    @Override public void setObjectPosition(String objectId, float x, float y, float z) { AveActivity.jniSetObjectPosition(objectId, x, y, z); }
    @Override public void setObjectRotation(String objectId, float x, float y, float z) { AveActivity.jniSetObjectRotation(objectId, x, y, z); }
    @Override public void setObjectScale(String objectId, float x, float y, float z) { AveActivity.jniSetObjectScale(objectId, x, y, z); }
    @Override public void setObjectVisible(String objectId, boolean visible) { AveActivity.jniSetObjectVisible(objectId, visible); }
    @Override public void setObjectColor(String objectId, float r, float g, float b, float a) { AveActivity.jniSetObjectColor(objectId, r, g, b, a); }
    @Override public void setObjectTexture(String objectId, String texture) { AveActivity.jniSetObjectTexture(objectId, texture); }
    @Override public void setObjectText(String objectId, String text) { AveActivity.jniSetObjectText(objectId, text); }
    @Override public void setObjectProgress(String objectId, float value) { AveActivity.jniSetObjectProgress(objectId, value); }
    @Override public boolean destroyObject(String objectId) { return AveActivity.jniDestroyObject(objectId); }
    @Override public float getObjectProgress(String objectId) { return AveActivity.jniGetObjectProgress(objectId); }
    @Override public float[] getObjectPosition(String objectId) { return AveActivity.jniGetObjectPosition(objectId); }
    @Override public float[] getObjectRotation(String objectId) { return AveActivity.jniGetObjectRotation(objectId); }
    @Override public float[] getObjectScale(String objectId) { return AveActivity.jniGetObjectScale(objectId); }
    @Override public boolean getObjectVisible(String objectId) { return AveActivity.jniGetObjectVisible(objectId); }
    @Override public float[] getObjectColor(String objectId) { return AveActivity.jniGetObjectColor(objectId); }
    @Override public String getObjectTexture(String objectId) { return AveActivity.jniGetObjectTexture(objectId); }
    @Override public String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return AveActivity.jniInstantiatePrefab(prefabPath, parentId, x, y, z);
    }
    @Override public void log(String message) {
        android.util.Log.i("AveScript", message);
    }
}
