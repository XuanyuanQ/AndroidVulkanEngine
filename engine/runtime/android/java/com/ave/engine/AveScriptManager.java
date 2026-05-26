package com.ave.engine;

import android.util.Log;
import java.util.HashMap;
import java.util.Map;

public final class AveScriptManager {
    private static final String TAG = "AveScriptManager";
    private final Map<String, AveScript> activeScripts = new HashMap<>(); // objectId -> AveScript instance

    // Called via JNI when C++ loads objects with Script components
    public void instantiateScript(String objectId, String className) {
        try {
            Class<?> clazz = Class.forName(className);
            AveScript script = (AveScript) clazz.getDeclaredConstructor().newInstance();
            activeScripts.put(objectId, script);
            
            // Invoke start hook
            script.start();
            Log.i(TAG, "Successfully loaded script '" + className + "' for GameObject '" + objectId + "'");
        } catch (Exception e) {
            Log.e(TAG, "Failed to instantiate script class: " + className, e);
        }
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
        boolean invoked = false;
        for (Map.Entry<String, AveScript> entry : activeScripts.entrySet()) {
            String objectId = entry.getKey();
            AveScript script = entry.getValue();
            String simpleName = script.getClass().getSimpleName();
            String fullName = script.getClass().getName();
            if (simpleName.equals(targetClassName) || fullName.equals(targetClassName) || objectId.equals(targetClassName)) {
                try {
                    // Try direct method reflection
                    script.getClass().getMethod(methodName).invoke(script);
                    invoked = true;
                } catch (NoSuchMethodException e) {
                    // Fallback to general onClick callback
                    script.onClick(methodName);
                    invoked = true;
                } catch (Exception e) {
                    Log.e(TAG, "Failed to invoke method '" + methodName + "' on " + targetClassName, e);
                }
            }
        }
        if (!invoked) {
            Log.w(TAG, "No active script class matching target: " + targetClassName);
        }
    }

    public void clear() {
        activeScripts.clear();
    }
}
