package com.ave.engine;

public final class AveRuntime {
    private static AveRuntimeBridge bridge = new NoopBridge();

    private AveRuntime() {
    }

    public static void setBridge(AveRuntimeBridge runtimeBridge) {
        bridge = runtimeBridge != null ? runtimeBridge : new NoopBridge();
    }

    static AveRuntimeBridge bridge() {
        return bridge;
    }

    private static final class NoopBridge implements AveRuntimeBridge {
        @Override public void setObjectPosition(String objectId, float x, float y, float z) {}
        @Override public void setObjectRotation(String objectId, float x, float y, float z) {}
        @Override public void setObjectScale(String objectId, float x, float y, float z) {}
        @Override public void setObjectVisible(String objectId, boolean visible) {}
        @Override public void setObjectColor(String objectId, float r, float g, float b, float a) {}
        @Override public void setObjectTexture(String objectId, String texture) {}
        @Override public void setObjectText(String objectId, String text) {}
        @Override public void setObjectProgress(String objectId, float value) {}
        @Override public boolean destroyObject(String objectId) { return false; }
        @Override public float getObjectProgress(String objectId) { return 0.0f; }
        @Override public float[] getObjectPosition(String objectId) { return new float[] {0.0f, 0.0f, 0.0f}; }
        @Override public float[] getObjectRotation(String objectId) { return new float[] {0.0f, 0.0f, 0.0f}; }
        @Override public float[] getObjectScale(String objectId) { return new float[] {1.0f, 1.0f, 1.0f}; }
        @Override public boolean getObjectVisible(String objectId) { return false; }
        @Override public float[] getObjectColor(String objectId) { return new float[] {1.0f, 1.0f, 1.0f, 1.0f}; }
        @Override public String getObjectTexture(String objectId) { return ""; }
        @Override public String instantiatePrefab(String prefabPath, String parentId, float x, float y, float z) { return ""; }
        @Override public void log(String message) {}
    }
}
