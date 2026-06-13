package android.util;

public final class Log {
    private Log() {
    }

    public static int i(String tag, String message) {
        System.err.println("[java][I][" + tag + "] " + message);
        return 0;
    }

    public static int d(String tag, String message) {
        System.err.println("[java][D][" + tag + "] " + message);
        return 0;
    }

    public static int w(String tag, String message) {
        System.err.println("[java][W][" + tag + "] " + message);
        return 0;
    }

    public static int e(String tag, String message) {
        System.err.println("[java][E][" + tag + "] " + message);
        return 0;
    }

    public static int e(String tag, String message, Throwable error) {
        System.err.println("[java][E][" + tag + "] " + message);
        error.printStackTrace(System.err);
        return 0;
    }
}
