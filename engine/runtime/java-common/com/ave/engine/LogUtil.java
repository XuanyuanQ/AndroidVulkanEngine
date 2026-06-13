package com.ave.engine;

public final class LogUtil {
    private LogUtil() {
    }

    public static void log(String message) {
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        if (stack.length < 4) {
            AveRuntime.bridge().log(message);
            return;
        }
        StackTraceElement caller = stack[3];
        AveRuntime.bridge().log("[" + caller.getFileName() + ":" + caller.getLineNumber() + "] "
                + caller.getClassName() + "." + caller.getMethodName() + " - " + message);
    }
}
