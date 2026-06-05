package com.ave.engine;

import android.util.Log;
import android.view.MotionEvent;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class AveScriptManager {
    private static final String TAG = "AveScriptManager";
    private final Map<String, AveScript> activeScripts = new ConcurrentHashMap<>(); // objectId -> AveScript instance

    // Called via JNI when C++ loads objects with Script components
    public void instantiateScript(String objectId, String className, String targetObjectId, String[] paramKeys, String[] paramValues) {
        try {
            Class<?> clazz = Class.forName(className);
            AveScript script = (AveScript) clazz.getDeclaredConstructor().newInstance();
            script.__bindObject(objectId);
            script.__bindParams(targetObjectId, makeParams(paramKeys, paramValues));
            activeScripts.put(objectId, script);
            
            // Invoke start hook
            script.start();
            Log.i(TAG, "Successfully loaded script '" + className + "' for GameObject '" + objectId + "'");
        } catch (Exception e) {
            Log.e(TAG, "Failed to instantiate script class: " + className, e);
        }
    }

    public void destroyScript(String objectId) {
        if (activeScripts.remove(objectId) != null) {
            Log.i(TAG, "Unloaded script instance for GameObject '" + objectId + "'");
        }
    }

    private Map<String, String> makeParams(String[] keys, String[] values) {
        Map<String, String> params = new HashMap<>();
        if (keys == null || values == null) {
            return params;
        }

        int count = Math.min(keys.length, values.length);
        for (int index = 0; index < count; index++) {
            if (keys[index] != null) {
                params.put(keys[index], values[index] != null ? values[index] : "");
            }
        }
        return params;
    }

    // Called via JNI every frame update
    public void update(float dt) {
        for (AveScript script : activeScripts.values()) {
            try {
                script.update(dt);
            } catch (Exception e) {
                Log.e(TAG, "Error in script update loop", e);
            }
        }
    }

    // Called via JNI when a button targeting a script is clicked
    public void triggerScriptMethod(String targetClassName, String methodName) {
        Log.i(TAG, "triggerScriptMethod called: target=" + targetClassName + ", method=" + methodName + ", activeScripts=" + activeScripts.size());
        boolean invoked = false;
        for (Map.Entry<String, AveScript> entry : activeScripts.entrySet()) {
            String objectId = entry.getKey();
            AveScript script = entry.getValue();
            String simpleName = script.getClass().getSimpleName();
            String fullName = script.getClass().getName();
            Log.d(TAG, "Checking script: objectId=" + objectId + ", simpleName=" + simpleName + ", fullName=" + fullName);
            if (simpleName.equals(targetClassName) || fullName.equals(targetClassName) || objectId.equals(targetClassName)) {
                Log.i(TAG, "Found matching script: " + objectId + " (" + simpleName + ")");
                try {
                    // Try direct method reflection
                    script.getClass().getMethod(methodName).invoke(script);
                    invoked = true;
                    Log.i(TAG, "Successfully invoked method: " + methodName);
                } catch (NoSuchMethodException e) {
                    // Fallback to general onClick callback
                    Log.i(TAG, "Method not found, falling back to onClick: " + methodName);
                    script.onClick(methodName);
                    invoked = true;
                } catch (Throwable e) {
                    Log.e(TAG, "Failed to invoke method '" + methodName + "' on " + targetClassName, e);
                }
            }
        }
        if (!invoked) {
            Log.w(TAG, "No active script class matching target: " + targetClassName);
        }
    }

    public void triggerScriptValueMethod(String targetClassName, String methodName, String sourceId, float value) {
        Log.i(TAG, "triggerScriptValueMethod called: target=" + targetClassName + ", method=" + methodName + ", source=" + sourceId + ", value=" + value);
        boolean invoked = false;
        for (Map.Entry<String, AveScript> entry : activeScripts.entrySet()) {
            String objectId = entry.getKey();
            AveScript script = entry.getValue();
            String simpleName = script.getClass().getSimpleName();
            String fullName = script.getClass().getName();
            if (simpleName.equals(targetClassName) || fullName.equals(targetClassName) || objectId.equals(targetClassName)) {
                try {
                    script.getClass().getMethod(methodName, String.class, float.class).invoke(script, sourceId, value);
                    invoked = true;
                } catch (NoSuchMethodException e) {
                    try {
                        script.getClass().getMethod(methodName, float.class).invoke(script, value);
                        invoked = true;
                    } catch (NoSuchMethodException missingFloatOnlyMethod) {
                        script.onValueChanged(sourceId, value);
                        invoked = true;
                    } catch (Exception invokeError) {
                        Log.e(TAG, "Failed to invoke value method '" + methodName + "' on " + targetClassName, invokeError);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to invoke value method '" + methodName + "' on " + targetClassName, e);
                }
            }
        }
        if (!invoked) {
            Log.w(TAG, "No active script class matching value target: " + targetClassName);
        }
    }

    public boolean dispatchTouchEvent(AveActivity activity, MotionEvent event) {
        boolean handled = false;
        for (AveScript script : activeScripts.values()) {
            if (!(script instanceof AveActivityEventHandler)) {
                continue;
            }
            try {
                handled |= ((AveActivityEventHandler) script).dispatchTouchEvent(activity, event);
            } catch (Exception e) {
                Log.e(TAG, "Error in script dispatchTouchEvent", e);
            }
        }
        return handled;
    }

    public void clear() {
        activeScripts.clear();
    }
}
