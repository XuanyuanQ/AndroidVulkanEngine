package com.example.trianglegame;

import com.ave.engine.AveScript;

public final class SelfDestruct extends AveScript {
    private float timer = 0.0f;
    private float lifeTime = 3.0f;

    @Override
    public void start() {
        log("SelfDestruct script started on object: " + getObjectId());
        lifeTime = Float.parseFloat(getParam("lifetime", "3.0"));
    }

    @Override
    public void update(float dt) {
        timer += dt;
        if (timer >= lifeTime) {
            // log("SelfDestruct: Time's up! Destroying self: " + getObjectId());
            destroySelf();
        }
    }
}
