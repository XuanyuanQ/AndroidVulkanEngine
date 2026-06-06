package com.example.trianglegame;

import com.ave.engine.AveScript;

import java.io.BufferedReader;
import java.io.InputStreamReader;

public final class RenderBackendDebugText extends AveScript {
    private static final String PROPERTY_NAME = "debug.ave.dynamic_rendering";
    private static final float REFRESH_INTERVAL_SECONDS = 0.25f;

    private String lastText = "";
    private String lastPropertyValue = "";
    private float refreshTimer = 0.0f;

    @Override
    public void start() {
        refreshText();
    }

    @Override
    public void update(float dt) {
        refreshTimer -= dt;
        if (refreshTimer > 0.0f) {
            return;
        }
        refreshTimer = REFRESH_INTERVAL_SECONDS;
        refreshText();
    }

    private void refreshText() {
        String value = getAndroidSystemProperty(PROPERTY_NAME, "0");
        boolean dynamicRendering =
                "1".equals(value) ||
                "true".equalsIgnoreCase(value) ||
                "on".equalsIgnoreCase(value);
        String text = dynamicRendering ? "DYNAMIC RENDERING" : "RENDER PASS";
        if (!value.equals(lastPropertyValue) || !text.equals(lastText)) {
            log("Render backend debug text property " + PROPERTY_NAME + "='" + value + "' -> " + text);
            lastPropertyValue = value;
            lastText = text;
        }
        // Scene/UI resources can be rebuilt after script startup, which restores
        // the XML default text. Re-apply the value periodically so the debug
        // label survives those runtime rebuilds.
        setText(text);
    }

    private static String getAndroidSystemProperty(String key, String fallback) {
        try {
            Class<?> systemProperties = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method get = systemProperties.getMethod("get", String.class, String.class);
            Object value = get.invoke(null, key, fallback);
            if (value instanceof String && !((String) value).isEmpty()) {
                return (String) value;
            }
        } catch (Throwable ignored) {
        }
        return getAndroidSystemPropertyFromGetprop(key, fallback);
    }

    private static String getAndroidSystemPropertyFromGetprop(String key, String fallback) {
        Process process = null;
        try {
            process = new ProcessBuilder("/system/bin/getprop", key)
                    .redirectErrorStream(true)
                    .start();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String value = reader.readLine();
                int exitCode = process.waitFor();
                if (exitCode == 0 && value != null && !value.isEmpty()) {
                    return value.trim();
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Throwable ignored) {
        } finally {
            if (process != null) {
                process.destroy();
            }
        }
        return fallback;
    }
}
