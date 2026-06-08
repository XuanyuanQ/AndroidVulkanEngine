# Minimal XML Triangle Project

This document describes the smallest user-facing project supported by the current architecture scaffold.

## User Project

```text
TriangleGame/
  project.xml
  scenes/main.scene.xml
  materials/triangle.material.xml
  scripts/PlayerController.java
  shaders/default_pbr.vert
  shaders/default_pbr.frag
```

## Build

Generate the Android project without invoking Gradle:

```powershell
python tools/ave.py build android sample/TriangleGame --no-gradle
```

Generate and build the APK when Android SDK, NDK, CMake, Gradle, and Vulkan shader tools are installed:

```powershell
python tools/ave.py build android sample/TriangleGame
```

The generated Android project is written to:

```text
sample/TriangleGame/build/android
```

## Current Runtime Boundary

The generated APK template contains:

- `AveActivity`: Android lifecycle, `SurfaceView`, and native surface handoff.
- `AveScript`: base Java gameplay script class.
- JNI entry points for creating/destroying the native engine runtime.
- `MinimalVulkanTriangle`: the native renderer integration point linked against Vulkan.

The XML loader and project validation are implemented on the PC side. The next implementation step is to move the same loader into the Android asset runtime and replace `MinimalVulkanTriangle` with a real Vulkan swapchain + graphics pipeline that draws the triangle declared in `main.scene.xml`.
