package android.view;

public final class MotionEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;

    private final int action;
    private final float x;
    private final float y;

    public MotionEvent(int action, float x, float y) {
        this.action = action;
        this.x = x;
        this.y = y;
    }

    public static MotionEvent obtain(int action, float x, float y) {
        return new MotionEvent(action, x, y);
    }

    public int getActionMasked() {
        return action;
    }

    public int getPointerCount() {
        return 1;
    }

    public int getActionIndex() {
        return 0;
    }

    public float getX() {
        return x;
    }

    public float getY() {
        return y;
    }

    public float getX(int index) {
        return index == 0 ? x : 0.0f;
    }

    public float getY(int index) {
        return index == 0 ? y : 0.0f;
    }
}
