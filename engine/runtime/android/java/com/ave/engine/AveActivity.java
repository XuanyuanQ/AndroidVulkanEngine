package com.ave.engine;

import android.app.Activity;
import android.os.Debug;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.util.Log;

public class AveActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "AveActivity";
    private SurfaceView surfaceView;
    private boolean isResumed = false;
    private boolean surfaceAvailable = false;
    private boolean nativeSurfaceAttached = false;
    private int nativeSurfaceWidth = 0;
    private int nativeSurfaceHeight = 0;
    private final Handler memoryLogHandler = new Handler(Looper.getMainLooper());
    private final Runnable memoryLogRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isResumed) {
                return;
            }
            logMemorySnapshot();
            memoryLogHandler.postDelayed(this, 5000L);
        }
    };

    static {
        System.loadLibrary("ave_runtime");
    }
    private static final AveScriptManager scriptManager = new AveScriptManager();

    protected final SurfaceView getAveSurfaceView() {
        return surfaceView;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        setContentView(surfaceView);
        nativeCreate(getAssets(), "project.xml");
        surfaceView.setFocusable(true);
        surfaceView.setFocusableInTouchMode(true);
        surfaceView.requestFocus();
    }

    @Override
    protected void onDestroy() {
        stopMemoryLogging();
        detachNativeSurfaceIfAttached();
        nativeDestroy();
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        isResumed = true;
        Log.i(TAG, "onResume -> setForeground(true), isResumed=" + isResumed + ", surfaceAvailable=" + surfaceAvailable + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
        nativeSetForeground(true);
        attachNativeSurfaceIfReady();
        pushSurfaceSizeIfReady();
        startMemoryLogging();
    }

    @Override
    protected void onPause() {
        Log.i(TAG, "onPause -> setForeground(false), isResumed=" + isResumed + ", surfaceAvailable=" + surfaceAvailable + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
        nativeSetForeground(false);
        stopMemoryLogging();
        detachNativeSurfaceIfAttached();
        isResumed = false;
        super.onPause();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surfaceAvailable = true;
        Log.i(TAG, "surfaceCreated -> surfaceAvailable=true, isResumed=" + isResumed + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
        attachNativeSurfaceIfReady();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged -> " + width + "x" + height + ", isResumed=" + isResumed + ", surfaceAvailable=" + surfaceAvailable + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
        attachNativeSurfaceIfReady();
        pushSurfaceSizeIfReady(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceAvailable = false;
        Log.i(TAG, "surfaceDestroyed -> surfaceAvailable=false, isResumed=" + isResumed + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
        detachNativeSurfaceIfAttached();
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (handleEngineTouchEvent(event)) {
            return true;
        }
        if (scriptManager.dispatchTouchEvent(this, event)) {
            return true;
        }
        return super.dispatchTouchEvent(event);
    }

    protected final boolean handleEngineTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN ||
                action == MotionEvent.ACTION_MOVE ||
                action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_CANCEL) {
            return nativeTouchEvent(
                    event.getX(),
                    event.getY(),
                    event.getActionMasked(),
                    surfaceView.getWidth(),
                    surfaceView.getHeight(),
                    getDisplayRotation());
        }
        return false;
    }

    private int getDisplayRotation() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            return getDisplay() != null ? getDisplay().getRotation() : Surface.ROTATION_0;
        }
        return getWindowManager().getDefaultDisplay().getRotation();
    }

    public static void jniInstantiateScript(String objectId, String className, String targetObjectId, String[] paramKeys, String[] paramValues) {
        scriptManager.instantiateScript(objectId, className, targetObjectId, paramKeys, paramValues);
    }

    public static void jniUpdateScripts(float dt) {
        scriptManager.update(dt);
    }

    public static void jniTriggerScriptMethod(String target, String method) {
        scriptManager.triggerScriptMethod(target, method);
    }

    public static void jniTriggerScriptValueMethod(String target, String method, String sourceId, float value) {
        scriptManager.triggerScriptValueMethod(target, method, sourceId, value);
    }

    public static void jniClearScripts() {
        scriptManager.clear();
    }

    public static void jniDestroyScript(String objectId) {
        scriptManager.destroyScript(objectId);
    }

    static void jniSetObjectPosition(String objectId, float x, float y, float z) {
        nativeSetObjectPosition(objectId, x, y, z);
    }

    static void jniSetObjectRotation(String objectId, float x, float y, float z) {
        nativeSetObjectRotation(objectId, x, y, z);
    }

    static void jniSetObjectScale(String objectId, float x, float y, float z) {
        nativeSetObjectScale(objectId, x, y, z);
    }

    static void jniSetObjectVisible(String objectId, boolean visible) {
        nativeSetObjectVisible(objectId, visible);
    }

    static void jniSetObjectColor(String objectId, float r, float g, float b, float a) {
        nativeSetObjectColor(objectId, r, g, b, a);
    }

    static void jniSetObjectTexture(String objectId, String texture) {
        nativeSetObjectTexture(objectId, texture);
    }

    static void jniSetObjectText(String objectId, String text) {
        nativeSetObjectText(objectId, text);
    }

    static void jniSetObjectProgress(String objectId, float value) {
        nativeSetObjectProgress(objectId, value);
    }

    static boolean jniDestroyObject(String objectId) {
        return nativeDestroyObject(objectId);
    }

    static float jniGetObjectProgress(String objectId) {
        return nativeGetObjectProgress(objectId);
    }

    static float[] jniGetObjectPosition(String objectId) {
        return nativeGetObjectPosition(objectId);
    }

    static float[] jniGetObjectRotation(String objectId) {
        return nativeGetObjectRotation(objectId);
    }

    static float[] jniGetObjectScale(String objectId) {
        return nativeGetObjectScale(objectId);
    }

    static boolean jniGetObjectVisible(String objectId) {
        return nativeGetObjectVisible(objectId);
    }

    static float[] jniGetObjectColor(String objectId) {
        return nativeGetObjectColor(objectId);
    }

    static String jniGetObjectTexture(String objectId) {
        return nativeGetObjectTexture(objectId);
    }

    public static void jniGenerateFontAtlas() {
        int cellWidth = 64;
        int cellHeight = 64;
        int cols = 16;
        int rows = 8;
        int w = cols * cellWidth;
        int h = rows * cellHeight;

        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(bitmap);
        android.graphics.Paint paint = new android.graphics.Paint();
        paint.setTextSize(44); // 44px text size fits beautifully in 40x64 cell
        paint.setAntiAlias(true);
        paint.setColor(android.graphics.Color.WHITE);
        paint.setTextAlign(android.graphics.Paint.Align.CENTER);
        paint.setTypeface(android.graphics.Typeface.create(android.graphics.Typeface.SANS_SERIF, android.graphics.Typeface.NORMAL));

        android.graphics.Paint.FontMetrics fm = paint.getFontMetrics();
        float baselineOffset = (cellHeight - (fm.bottom - fm.top)) / 2f - fm.top;

        for (int c = 0; c < 128; c++) {
            int col = c % 16;
            int row = c / 16;
            float x = col * cellWidth + cellWidth / 2f;
            float y = row * cellHeight + baselineOffset;

            if (c >= 32 && c <= 126) {
                canvas.drawText(String.valueOf((char) c), x, y, paint);
            }
        }

        int[] pixels = new int[w * h];
        bitmap.getPixels(pixels, 0, w, 0, 0, w, h);

        nativeRegisterFontAtlas(w, h, pixels);

        bitmap.recycle();
    }

    public static String jniInstantiatePrefab(String prefabPath, String parentId, float x, float y, float z) {
        return nativeInstantiatePrefab(prefabPath, parentId, x, y, z);
    }

    private void attachNativeSurfaceIfReady() {
        if (!isResumed || !surfaceAvailable || nativeSurfaceAttached || surfaceView == null) {
            return;
        }

        SurfaceHolder holder = surfaceView.getHolder();
        if (holder == null) {
            return;
        }

        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            return;
        }

        nativeSetSurface(surface);
        nativeSurfaceAttached = true;
        nativeSurfaceWidth = surfaceView.getWidth();
        nativeSurfaceHeight = surfaceView.getHeight();
        Log.i(TAG, "nativeSetSurface attached, isResumed=" + isResumed + ", surfaceAvailable=" + surfaceAvailable + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
    }

    private void detachNativeSurfaceIfAttached() {
        if (!nativeSurfaceAttached) {
            return;
        }
        nativeClearSurface();
        nativeSurfaceAttached = false;
        nativeSurfaceWidth = 0;
        nativeSurfaceHeight = 0;
        Log.i(TAG, "nativeClearSurface detached, isResumed=" + isResumed + ", surfaceAvailable=" + surfaceAvailable + ", nativeSurfaceAttached=" + nativeSurfaceAttached);
    }

    private void pushSurfaceSizeIfReady() {
        if (!nativeSurfaceAttached || surfaceView == null) {
            return;
        }
        int width = surfaceView.getWidth();
        int height = surfaceView.getHeight();
        pushSurfaceSizeIfReady(width, height);
    }

    private void pushSurfaceSizeIfReady(int width, int height) {
        if (!nativeSurfaceAttached) {
            return;
        }
        if (width > 0 && height > 0) {
            if (width == nativeSurfaceWidth && height == nativeSurfaceHeight) {
                return;
            }
            nativeResize(width, height);
            nativeSurfaceWidth = width;
            nativeSurfaceHeight = height;
        }
    }

    private void startMemoryLogging() {
        memoryLogHandler.removeCallbacks(memoryLogRunnable);
        memoryLogHandler.postDelayed(memoryLogRunnable, 5000L);
    }

    private void stopMemoryLogging() {
        memoryLogHandler.removeCallbacks(memoryLogRunnable);
    }

    private void logMemorySnapshot() {
        long nativeAllocated = Debug.getNativeHeapAllocatedSize() / 1024L;
        long nativeFree = Debug.getNativeHeapFreeSize() / 1024L;
        long nativeSize = Debug.getNativeHeapSize() / 1024L;
        long javaUsed = (Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()) / 1024L;
        long javaTotal = Runtime.getRuntime().totalMemory() / 1024L;
        long javaMax = Runtime.getRuntime().maxMemory() / 1024L;
        long pss = Debug.getPss();
        Log.i(TAG, "MemorySnapshot -> nativeAllocatedKB=" + nativeAllocated +
                ", nativeFreeKB=" + nativeFree +
                ", nativeHeapKB=" + nativeSize +
                ", javaUsedKB=" + javaUsed +
                ", javaTotalKB=" + javaTotal +
                ", javaMaxKB=" + javaMax +
                ", pssKB=" + pss +
                ", isResumed=" + isResumed +
                ", surfaceAvailable=" + surfaceAvailable +
                ", nativeSurfaceAttached=" + nativeSurfaceAttached);
    }

    private static native void nativeCreate(android.content.res.AssetManager assets, String projectPath);
    private static native void nativeDestroy();
    private static native void nativeSetSurface(Surface surface);
    private static native void nativeClearSurface();
    private static native void nativeSetForeground(boolean foreground);
    private static native boolean nativeTouchEvent(float x, float y, int action, int inputWidth, int inputHeight, int inputRotation);
    private static native void nativeSetObjectPosition(String objectId, float x, float y, float z);
    private static native void nativeSetObjectRotation(String objectId, float x, float y, float z);
    private static native void nativeSetObjectScale(String objectId, float x, float y, float z);
    private static native void nativeSetObjectVisible(String objectId, boolean visible);
    private static native void nativeSetObjectColor(String objectId, float r, float g, float b, float a);
    private static native void nativeSetObjectTexture(String objectId, String texture);
    private static native void nativeSetObjectText(String objectId, String text);
    private static native void nativeSetObjectProgress(String objectId, float value);
   
    
    private static native void nativeResize(int width, int height);
    private static native float[] nativeGetObjectPosition(String objectId);
    private static native float nativeGetObjectProgress(String objectId);
    private static native float[] nativeGetObjectRotation(String objectId);
    private static native float[] nativeGetObjectScale(String objectId);
    private static native boolean nativeGetObjectVisible(String objectId);
    private static native float[] nativeGetObjectColor(String objectId);
    private static native String nativeGetObjectTexture(String objectId);
    public static String jniInstantiatePrefab(String prefabPath, String parentId) {
        return jniInstantiatePrefab(prefabPath, parentId, 0.0f, 0.0f, 0.0f);
    }

    private static native void nativeRegisterFontAtlas(int width, int height, int[] pixels);
    private static native String nativeInstantiatePrefab(String prefabPath, String parentId, float x, float y, float z);
    private static native boolean nativeDestroyObject(String objectId);
}
