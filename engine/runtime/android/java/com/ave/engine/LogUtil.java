package com.ave.engine;

import android.util.Log;

/**
 * Centralized logging utility that prints the originating file, line number,
 * class name and method name together with the provided message.
 *
 * Example output (Logcat tag is the simple class name of the caller):
 *   [PlayerController.java:16] com.example.trianglegame.PlayerController.update - Triangle script updated
 */
public class LogUtil {
    /**
     * Logs a message with detailed context information.
     *
     * @param message The message to be logged.
     */
    public static void log(String message) {
        // Retrieve the stack trace elements. Index 0 is getStackTrace, 1 is this method,
        // 2 is the caller of LogUtil.log (e.g., AveScript.log), 3 is the actual user code caller.
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        // Guard against unexpected stack depth.
        if (stack.length < 4) {
            Log.i("LogUtil", message);
            return;
        }
        StackTraceElement caller = stack[3];
        String fileName = caller.getFileName();
        int lineNumber = caller.getLineNumber();
        String className = caller.getClassName();
        String methodName = caller.getMethodName();
        // Use simple class name as Logcat tag for easier filtering.
        String tag = className.substring(className.lastIndexOf('.') + 1);
        String formatted = String.format("[%s:%d] %s.%s - %s", fileName, lineNumber, className, methodName, message);
        Log.i(tag, formatted);
    }
}
