package com.ave.preview;

import com.ave.engine.AveScript;
import com.ave.engine.AveActivity;
import com.ave.engine.AveActivityEventHandler;
import com.ave.engine.AveRuntime;
import com.ave.engine.PreviewAveRuntimeBridge;
import com.ave.engine.PreviewBridge;
import android.view.MotionEvent;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class PreviewScriptMain {
    private final Map<String, AveScript> scripts = new ConcurrentHashMap<>();
    private final AveActivity activity = new AveActivity();

    public static void main(String[] args) throws Exception {
        new PreviewScriptMain().run();
    }

    public PreviewScriptMain() {
        AveRuntime.setBridge(new PreviewAveRuntimeBridge());
    }

    private void run() throws Exception {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(System.in))) {
            String line;
            while ((line = reader.readLine()) != null) {
                handle(line);
            }
        }
    }

    public void handle(String line) {
        try {
            String[] parts = split(line);
            if (parts.length == 0) {
                return;
            }
            switch (parts[0]) {
            case "clear":
                scripts.clear();
                PreviewBridge.clearObjects();
                break;
            case "object":
                if (parts.length >= 11) {
                    PreviewBridge.defineObject(parts[1],
                            parseFloat(parts[2]), parseFloat(parts[3]), parseFloat(parts[4]),
                            parseFloat(parts[5]), parseFloat(parts[6]), parseFloat(parts[7]),
                            parseFloat(parts[8]), parseFloat(parts[9]), parseFloat(parts[10]));
                }
                break;
            case "script":
                instantiate(parts);
                break;
            case "update":
                update(parts.length >= 2 ? parseFloat(parts[1]) : 0.0f);
                break;
            case "click":
                if (parts.length >= 3) {
                    triggerClick(parts[1], parts[2]);
                }
                break;
            case "value":
                if (parts.length >= 5) {
                    triggerValue(parts[1], parts[2], parts[3], parseFloat(parts[4]));
                }
                break;
            case "touch":
                if (parts.length >= 4) {
                    dispatchTouch(parseAction(parts[1]), parseFloat(parts[2]), parseFloat(parts[3]));
                }
                break;
            default:
                PreviewBridge.log("unknown command: " + line);
                break;
            }
        } catch (Throwable error) {
            error.printStackTrace(System.err);
        }
    }

    private void instantiate(String[] parts) throws Exception {
        if (parts.length < 4) {
            return;
        }
        String objectId = parts[1];
        String className = parts[2];
        String target = parts[3];
        Map<String, String> params = new HashMap<>();
        for (int i = 4; i < parts.length; ++i) {
            int eq = parts[i].indexOf('=');
            if (eq > 0) {
                params.put(parts[i].substring(0, eq), parts[i].substring(eq + 1));
            }
        }
        Class<?> clazz = Class.forName(className);
        AveScript script = (AveScript) clazz.getDeclaredConstructor().newInstance();
        script.__bindObject(objectId);
        script.__bindParams(target, params);
        scripts.put(objectId, script);
        script.start();
        PreviewBridge.log("loaded script " + className + " for " + objectId);
    }

    private void update(float dt) {
        for (Map.Entry<String, AveScript> entry : scripts.entrySet()) {
            AveScript script = entry.getValue();
            try {
                script.update(dt);
                if (script.__isDestroyed()) {
                    scripts.remove(entry.getKey());
                }
            } catch (Throwable error) {
                error.printStackTrace(System.err);
            }
        }
    }

    private void triggerClick(String target, String method) {
        for (Map.Entry<String, AveScript> entry : scripts.entrySet()) {
            AveScript script = entry.getValue();
            if (!matches(entry.getKey(), script, target)) {
                continue;
            }
            try {
                try {
                    Method direct = script.getClass().getMethod(method);
                    direct.invoke(script);
                } catch (NoSuchMethodException missing) {
                    script.onClick(method);
                }
            } catch (Throwable error) {
                error.printStackTrace(System.err);
            }
        }
    }

    private void triggerValue(String target, String method, String sourceId, float value) {
        for (Map.Entry<String, AveScript> entry : scripts.entrySet()) {
            AveScript script = entry.getValue();
            if (!matches(entry.getKey(), script, target)) {
                continue;
            }
            try {
                try {
                    Method direct = script.getClass().getMethod(method, String.class, float.class);
                    direct.invoke(script, sourceId, value);
                } catch (NoSuchMethodException missingStringFloat) {
                    try {
                        Method floatOnly = script.getClass().getMethod(method, float.class);
                        floatOnly.invoke(script, value);
                    } catch (NoSuchMethodException missingFloatOnly) {
                        script.onValueChanged(sourceId, value);
                    }
                }
            } catch (Throwable error) {
                error.printStackTrace(System.err);
            }
        }
    }

    private void dispatchTouch(int action, float x, float y) {
        MotionEvent event = MotionEvent.obtain(action, x, y);
        for (AveScript script : scripts.values()) {
            if (!(script instanceof AveActivityEventHandler)) {
                continue;
            }
            try {
                ((AveActivityEventHandler) script).dispatchTouchEvent(activity, event);
            } catch (Throwable error) {
                error.printStackTrace(System.err);
            }
        }
    }

    private boolean matches(String objectId, AveScript script, String target) {
        return objectId.equals(target) ||
                script.getClass().getSimpleName().equals(target) ||
                script.getClass().getName().equals(target);
    }

    private static float parseFloat(String text) {
        try {
            return Float.parseFloat(text);
        } catch (Exception ignored) {
            return 0.0f;
        }
    }

    private static int parseAction(String text) {
        try {
            return Integer.parseInt(text);
        } catch (Exception ignored) {
            return MotionEvent.ACTION_CANCEL;
        }
    }

    private static String[] split(String line) {
        String[] raw = line.split("\\|", -1);
        for (int i = 0; i < raw.length; ++i) {
            raw[i] = unescape(raw[i]);
        }
        return raw;
    }

    private static String unescape(String text) {
        StringBuilder out = new StringBuilder();
        boolean escaped = false;
        for (int i = 0; i < text.length(); ++i) {
            char ch = text.charAt(i);
            if (!escaped) {
                if (ch == '\\') {
                    escaped = true;
                } else {
                    out.append(ch);
                }
                continue;
            }
            switch (ch) {
            case 'p':
                out.append('|');
                break;
            case 'n':
                out.append('\n');
                break;
            case 'r':
                out.append('\r');
                break;
            default:
                out.append(ch);
                break;
            }
            escaped = false;
        }
        if (escaped) {
            out.append('\\');
        }
        return out.toString();
    }
}
