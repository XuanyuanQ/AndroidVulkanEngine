package com.example.trianglegame;

import com.ave.engine.AveScript;
import com.ave.engine.AveObjectController;

public final class MySpawner extends AveScript {
    private int spawnCount = 0;

    @Override
    public void start() {
        log("MySpawner script started on object: " + getObjectId());
    }

    @Override
    public void update(float dt) {
    }

    public void spawnDynamicObject() {
        spawnCount++;
        float x = 0.0f;
        float y = 1.0f;
        float z = 3.0f;

        log("Spawning dynamic sphere #" + spawnCount + " at position (" + x + ", " + y + ", " + z + ") using my_sphere.prefab.xml");
        
        // Spawn using the verified working my_sphere prefab
        String newObjId = AveObjectController.instantiatePrefab("prefabs/my_sphere.prefab.xml", "", x, y, z);
        
        if (newObjId != null && !newObjId.isEmpty()) {
            // Make it large so it's impossible to miss!
            AveObjectController.setScale(newObjId, 1.5f, 1.5f, 1.5f);
            log("Successfully spawned new object: " + newObjId);
        } else {
            log("Error: Spawning failed!");
        }
    }
}
