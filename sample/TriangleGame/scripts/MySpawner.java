package com.example.trianglegame;

import com.ave.engine.AveScript;
import com.ave.engine.AveObjectController;

import java.util.ArrayList;
import java.util.List;

public final class MySpawner extends AveScript {
    private int spawnCount = 0;
    private String prefabPath = "prefabs/my_sphere.prefab.xml";
    private float spawnScale = 2.0f;
    private final List<float[]> spawnPositions = new ArrayList<>();

    @Override
    public void start() {
        log("MySpawner script started on object: " + getObjectId());
        prefabPath = getParam("prefab", prefabPath);
        spawnScale = parseFloat(getParam("spawnScale", "2.0"), spawnScale);
        parseSpawnPositions(getParam("spawnPositions", ""));

        if (spawnPositions.isEmpty()) {
            spawnPositions.add(new float[] {-3.0f, 3.0f, 0.0f});
            spawnPositions.add(new float[] {-1.5f, 3.0f, 0.0f});
            spawnPositions.add(new float[] {0.0f, 3.0f, 0.0f});
            spawnPositions.add(new float[] {1.5f, 3.0f, 0.0f});
            spawnPositions.add(new float[] {3.0f, 3.0f, 0.0f});
        }

        log("MySpawner config prefab=" + prefabPath + ", spawnScale=" + spawnScale + ", spawnPoints=" + spawnPositions.size());
    }

    @Override
    public void update(float dt) {
    }

    public void spawnDynamicObject() {
        spawnCount++;
        int cycle = (spawnCount - 1) / spawnPositions.size();
        float[] spawn = spawnPositions.get((spawnCount - 1) % spawnPositions.size());
        float x = spawn[0];
        float y = spawn[1] + cycle * 1.2f;
        float z = spawn[2];

        log("Spawning dynamic sphere #" + spawnCount + " at position (" + x + ", " + y + ", " + z + ") using " + prefabPath);
        
        String newObjId = AveObjectController.instantiatePrefab(prefabPath, "", x, y, z);
        
        if (newObjId != null && !newObjId.isEmpty()) {
            AveObjectController.setScale(newObjId, spawnScale, spawnScale, spawnScale);
            log("Successfully spawned new object: " + newObjId);
        } else {
            log("Error: Spawning failed!");
        }
    }

    private void parseSpawnPositions(String text) {
        spawnPositions.clear();
        if (text == null || text.trim().isEmpty()) {
            return;
        }

        String[] entries = text.split(";");
        for (String entry : entries) {
            String[] parts = entry.trim().split(",");
            if (parts.length < 3) {
                continue;
            }
            float x = parseFloat(parts[0], 0.0f);
            float y = parseFloat(parts[1], 0.0f);
            float z = parseFloat(parts[2], 0.0f);
            spawnPositions.add(new float[] {x, y, z});
        }
    }

    private float parseFloat(String text, float fallback) {
        try {
            return Float.parseFloat(text.trim());
        } catch (Exception ignored) {
            return fallback;
        }
    }
}
