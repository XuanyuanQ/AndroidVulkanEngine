package com.example.trianglegame;

import android.view.MotionEvent;

import com.ave.engine.AveActivity;
import com.ave.engine.AveActivityEventHandler;
import com.ave.engine.AveScript;

public final class PlayerController extends AveScript {
    private boolean visible=false;
    private float mVisibleTimer = 0.0f;

    @Override
    public void start() {
        visible = getVisible();
        log("Triangle script started");
    }

    @Override
    public void update(float dt) {
        // 2. 没帧都把过去的时间（dt）累加到计时器中
            mVisibleTimer += dt;

            // 3. 当累加时间达到或超过 2 秒时
            if (mVisibleTimer >= 2.0f) {
                // 切换可见性状态
                visible = !visible;
                log("updata visible status:"+visible);
                setVisible(visible);
                log("visible status:"+getVisible()); 

                // 4. 重置计时器。这里用 -= 2.0f 比直接赋值 0 更好，
                // 可以保留微小的帧时间溢出，让定时更加精准。
                mVisibleTimer -= 2.0f; 
            }
    }
}
