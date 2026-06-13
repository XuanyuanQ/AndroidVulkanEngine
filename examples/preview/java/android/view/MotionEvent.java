package android.view;

public final class MotionEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;

    public int getActionMasked() {
        return ACTION_CANCEL;
    }

    public int getPointerCount() {
        return 0;
    }

    public int getActionIndex() {
        return 0;
    }

    public float getX() {
        return 0.0f;
    }

    public float getY() {
        return 0.0f;
    }

    public float getX(int index) {
        return 0.0f;
    }

    public float getY(int index) {
        return 0.0f;
    }
}
