package com.example.trianglegame;

import com.ave.engine.AveScript;

public final class HierarchyDemoController extends AveScript {
    private float angle = 0.0f;
    private float speed = 35.0f;

    @Override
    public void start() {
        speed = Float.parseFloat(getParam("speed", "35"));
        log("Hierarchy demo started for " + getObjectId());
    }

    @Override
    public void update(float dt) {
        angle += speed * dt;
        setRotation(0.0f, angle, 0.0f);
    }
}
