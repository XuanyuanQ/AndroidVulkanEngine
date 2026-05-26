package com.example.trianglegame;

import com.ave.engine.AveScript;

public final class PlayerController extends AveScript {
    private float elapsed = 0.0f;
    private boolean visible = true;

    @Override
    public void start() {
        log("Triangle script started");
    }

    @Override
    public void update(float dt) {
         log("Triangle script updated");
        elapsed += dt;
        if (elapsed >= 2.0f) {
            elapsed = 0.0f;
            visible = !visible;
            setVisible(visible);
        }
    }
}
