package com.ave.engine;

import android.view.MotionEvent;

public interface AveActivityEventHandler {
    boolean dispatchTouchEvent(AveActivity activity, MotionEvent event);
}
