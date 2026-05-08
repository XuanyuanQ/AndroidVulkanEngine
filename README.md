# Android Vulkan Mini Game Engine Architecture

## 项目目的

本项目目标是实现一个面向 Android 小型游戏开发者的轻量游戏引擎。开发者在 PC 上通过统一的项目文件和场景文件描述游戏对象、UI、材质、shader 和 Java 逻辑，然后通过构建工具生成 Android APK。手机端运行时由 C++ Engine Runtime 和 Vulkan Renderer 负责加载项目数据、执行脚本、处理输入并完成渲染。

这个项目不是单纯的 Vulkan Demo，而是一个 **PC Authoring + Android Vulkan Runtime** 的迷你 Unity-like 引擎原型。开发者不需要理解 Vulkan、NDK、Android Surface、descriptor、pipeline、同步等底层细节。

## 1. 总体大架构

```mermaid
flowchart LR
    PC["PC Authoring Side<br/>电脑端开发与构建"]
    Contract["Shared Data Contract<br/>统一项目数据协议"]
    APK["Generated APK<br/>打包后的安卓应用"]
    Android["Android Runtime Side<br/>手机端运行时"]
    Vulkan["Vulkan Renderer / RHI<br/>隐藏底层渲染后端"]

    PC --> Contract
    PC --> APK
    Contract --> APK
    APK --> Android
    Android --> Vulkan
```

整体流程：

```text
开发者在 PC 上开发
  -> 生成统一数据
  -> Build Tool 打包成 APK
  -> Android Runtime 加载数据
  -> Vulkan 后端负责渲染
```

## 2. PC Authoring Side

```mermaid
flowchart TB
    Project["Game Project Folder"]

    ProjectJson["project.json<br/>项目配置 / 包名 / 入口场景"]
    SceneJson["main.scene.json<br/>场景树 / UI / 组件 / 脚本绑定"]
    MaterialJson["materials/*.json<br/>PBR 材质参数"]
    Scripts["scripts/*.java<br/>用户游戏逻辑"]
    Assets["assets/*<br/>模型 / 贴图 / UI 图片"]
    Shaders["shaders/*<br/>vertex / fragment / compute"]

    BuildTool["Build Tool<br/>ave build android"]
    Validator["Project Validator<br/>检查引用是否正确"]
    ShaderCompiler["Shader Compiler<br/>GLSL/Slang -> SPIR-V"]
    AssetPipeline["Asset Pipeline<br/>资源拷贝 / 打包"]
    AndroidTemplate["Android Template<br/>Gradle / CMake / NDK / AveActivity"]
    APK["Output APK"]

    Project --> ProjectJson
    Project --> SceneJson
    Project --> MaterialJson
    Project --> Scripts
    Project --> Assets
    Project --> Shaders

    ProjectJson --> BuildTool
    SceneJson --> BuildTool
    MaterialJson --> BuildTool
    Scripts --> BuildTool
    Assets --> BuildTool
    Shaders --> BuildTool

    BuildTool --> Validator
    Validator --> ShaderCompiler
    Validator --> AssetPipeline
    ShaderCompiler --> AssetPipeline
    AssetPipeline --> AndroidTemplate
    AndroidTemplate --> APK
```

| 模块 | 职责 |
|---|---|
| `project.json` | 定义项目名称、包名、入口场景、横竖屏等全局配置 |
| `main.scene.json` | 统一描述场景树、GameObject、组件、UI 和脚本绑定 |
| `materials/*.json` | 描述 PBR 材质参数、shader 引用和贴图引用 |
| `scripts/*.java` | 开发者编写的游戏逻辑 |
| `assets/*` | 模型、贴图、UI 图片等游戏资源 |
| `shaders/*` | vertex、fragment、compute shader 源文件 |
| `Build Tool` | 一键校验项目、编译 shader、打包资源、生成 APK |
| `Android Template` | 固定 Gradle、CMake、NDK、Activity、native runtime 工程模板 |

## 3. Shared Data Contract

```mermaid
flowchart TB
    Contract["Shared Data Contract"]

    ProjectData["Project Data<br/>package / entryScene / orientation"]
    SceneData["Scene Data<br/>Scene / GameObject Tree"]
    HierarchyData["Hierarchy Data<br/>parent / children"]
    ComponentData["Component Data<br/>Transform / MeshRenderer / Camera / Light / Script / Image / Button / ProgressBar"]
    MaterialData["Material Data<br/>shader / baseColor / metallic / roughness / textures"]
    ScriptData["Script Binding Data<br/>Java class / method / target object"]
    AssetRefData["Asset Reference Data<br/>mesh / texture / shader / material"]
    RenderFeatureData["Render Feature Data<br/>PBR / Compute / PostFX"]

    Contract --> ProjectData
    Contract --> SceneData
    SceneData --> HierarchyData
    SceneData --> ComponentData
    Contract --> MaterialData
    Contract --> ScriptData
    Contract --> AssetRefData
    Contract --> RenderFeatureData
```

这层是 PC 端和 Android Runtime 之间的协议。PC 端怎么写，Android 端就按同一套数据结构加载。

场景层级关系属于：

```text
Scene Data
  -> GameObject Tree
  -> parent / children
  -> Component Data
```

UI 不单独拆成独立文件，而是场景树中的 GameObject：

```text
GameObject: FireButton
  -> UI Layout
  -> Image
  -> Button
```

## 4. Android Runtime Side

```mermaid
flowchart TB
    AveActivity["AveActivity<br/>Android lifecycle / surface / touch"]
    JavaAPI["Java Game API<br/>用户脚本入口"]
    JNI["JNI Bridge<br/>Java <-> C++"]
    Engine["C++ Engine Runtime"]

    ProjectLoader["Project Loader"]
    SceneLoader["Scene Loader"]
    SceneWorld["Scene World<br/>GameObject hierarchy"]
    TransformSystem["Transform System"]
    ComponentSystem["Component System"]
    ScriptRuntime["Script Runtime<br/>Java Start / Update / OnClick"]
    UIRuntime["UI Runtime<br/>Image / Button / ProgressBar"]
    AssetRuntime["Asset Runtime"]
    JobSystem["Job System"]
    EventSystem["Event System"]
    Profiler["Profiler / Logcat"]

    AveActivity --> JavaAPI
    AveActivity --> JNI
    AveActivity --> EventSystem
    JNI --> Engine
    JavaAPI --> JNI

    Engine --> ProjectLoader
    Engine --> SceneLoader
    Engine --> SceneWorld
    Engine --> AssetRuntime
    Engine --> JobSystem
    Engine --> Profiler

    ProjectLoader --> SceneLoader
    SceneLoader --> SceneWorld
    SceneWorld --> TransformSystem
    SceneWorld --> ComponentSystem

    ComponentSystem --> ScriptRuntime
    ComponentSystem --> UIRuntime
    EventSystem --> ScriptRuntime
    EventSystem --> UIRuntime
```

| 模块 | 职责 |
|---|---|
| `AveActivity` | 隐藏 Android lifecycle、surface、touch 输入 |
| `Java Game API` | 给开发者提供简单 Java 逻辑入口 |
| `JNI Bridge` | 负责 Java 和 C++ runtime 通信 |
| `C++ Engine Runtime` | 管理 main loop、scene、asset、event、script |
| `Project Loader` | 加载 `project.json` |
| `Scene Loader` | 加载 `main.scene.json` 并创建场景 |
| `Scene World` | 维护 GameObject hierarchy |
| `Transform System` | 更新父子层级和 local/world matrix |
| `Component System` | 管理组件生命周期和 update dispatch |
| `Script Runtime` | 调用 Java `Start`、`Update`、`OnClick` |
| `UI Runtime` | 处理 Image、Button、ProgressBar 和点击命中 |
| `Asset Runtime` | 加载 APK 内的 mesh、texture、material、SPIR-V |
| `Job System` | 支持异步资源加载和多线程任务 |

## 5. Scene / Component 架构

```mermaid
flowchart TB
    Scene["Scene"]
    Root["GameObject: Root"]
    Player["GameObject: Player"]
    Camera["GameObject: MainCamera"]
    FireButton["GameObject: FireButton"]

    PlayerTransform["Transform"]
    MeshRenderer["MeshRenderer"]
    PlayerScript["Script<br/>PlayerController.java"]

    CameraTransform["Transform"]
    CameraComp["Camera"]

    UITransform["UI Layout"]
    Image["Image<br/>fire_button.png"]
    Button["Button<br/>onClick -> Player.shoot"]

    Scene --> Root
    Root --> Player
    Root --> Camera
    Root --> FireButton

    Player --> PlayerTransform
    Player --> MeshRenderer
    Player --> PlayerScript

    Camera --> CameraTransform
    Camera --> CameraComp

    FireButton --> UITransform
    FireButton --> Image
    FireButton --> Button
```

这一层要保持 Unity-like 思路：

```text
所有东西都是 GameObject
UI 也是 GameObject
行为通过 Component 扩展
```

## 6. Render Frontend

```mermaid
flowchart TB
    SceneWorld["Scene World"]
    UIRuntime["UI Runtime"]
    AssetRuntime["Asset Runtime"]

    RenderWorld["Render World<br/>每帧渲染快照"]
    MaterialSystem["Material System"]
    EffectSystem["Effect System<br/>Particle / Bloom"]
    Culling["Culling & Batching"]
    FrameGraph["Frame Graph"]

    Depth["Depth Prepass"]
    Shadow["Shadow Pass"]
    PBR["PBR Pass"]
    Compute["Compute Pass"]
    UI["UI Pass"]
    Tone["Tone Mapping Pass"]

    SceneWorld --> RenderWorld
    UIRuntime --> RenderWorld
    AssetRuntime --> RenderWorld
    AssetRuntime --> MaterialSystem

    RenderWorld --> Culling
    MaterialSystem --> Culling
    EffectSystem --> FrameGraph
    Culling --> FrameGraph

    FrameGraph --> Depth
    FrameGraph --> Shadow
    FrameGraph --> PBR
    FrameGraph --> Compute
    FrameGraph --> UI
    FrameGraph --> Tone
```

Render Frontend 负责把游戏世界变成渲染任务：

```text
GameObject / UI / Material
  -> RenderWorld
  -> FrameGraph
  -> Render Pass / Compute Pass
```

## 7. Material / PBR 架构

```mermaid
flowchart TB
    MaterialJson["material.json"]
    ShaderRef["shader: pbr_lit"]
    Properties["PBR Properties<br/>baseColor / metallic / roughness / normalMap"]
    Textures["Texture Assets"]

    MaterialSystem["Material System"]
    Descriptor["Descriptor Binding"]
    Pipeline["Graphics Pipeline"]
    PBRPass["PBR Pass"]
    Vulkan["Vulkan RHI"]

    MaterialJson --> ShaderRef
    MaterialJson --> Properties
    MaterialJson --> Textures

    ShaderRef --> MaterialSystem
    Properties --> MaterialSystem
    Textures --> MaterialSystem

    MaterialSystem --> Descriptor
    MaterialSystem --> Pipeline
    Descriptor --> PBRPass
    Pipeline --> PBRPass
    PBRPass --> Vulkan
```

开发者看到的是材质配置：

```json
{
  "shader": "pbr_lit",
  "metallic": 0.5,
  "roughness": 0.3
}
```

底层实际完成 descriptor 更新、pipeline 选择、uniform/texture 绑定和 PBR pass 渲染。

## 8. Vulkan RHI 内部架构

```mermaid
flowchart TB
    Renderer["Renderer Frontend"]

    Device["Vulkan Device<br/>Instance / Physical / Logical Device"]
    Surface["Android Surface<br/>ANativeWindow / VkSurfaceKHR"]
    Swapchain["Swapchain"]
    ResourceAllocator["Resource Allocator<br/>Buffer / Image / Memory"]
    DescriptorSystem["Descriptor System"]
    PipelineCache["Pipeline Cache<br/>Graphics / Compute"]
    CommandSystem["Command System<br/>Primary / Secondary Command Buffers"]
    SyncSystem["Sync System<br/>Fence / Semaphore / Barrier"]
    Queues["GPU Queues<br/>Graphics / Compute / Transfer / Present"]

    Renderer --> CommandSystem
    Renderer --> ResourceAllocator
    Renderer --> DescriptorSystem
    Renderer --> PipelineCache

    Surface --> Swapchain
    Swapchain --> Device
    ResourceAllocator --> Device
    DescriptorSystem --> Device
    PipelineCache --> Device
    CommandSystem --> Queues
    SyncSystem --> Queues
    Queues --> Device
```

Vulkan RHI 完全对用户隐藏。用户只需要创建对象、材质、UI 和脚本，不直接接触 Vulkan API。

## 9. Build 到运行完整流程

```mermaid
sequenceDiagram
    participant Dev as Developer PC
    participant Build as Build Tool
    participant APK as Android APK
    participant Activity as AveActivity
    participant Engine as C++ Engine
    participant Scene as Scene Runtime
    participant Java as Java Script
    participant Renderer as Vulkan Renderer
    participant GPU as GPU

    Dev->>Build: ave build android
    Build->>Build: validate project + scene
    Build->>Build: compile shaders to SPIR-V
    Build->>APK: package data + scripts + native runtime
    APK->>Activity: launch
    Activity->>Engine: create runtime
    Engine->>Scene: load project.json + main.scene.json
    Scene->>Java: bind script classes
    Activity->>Engine: touch event
    Engine->>Java: Button.onClick
    Java->>Scene: modify GameObject / Material
    Engine->>Renderer: build render snapshot
    Renderer->>GPU: submit graphics + compute commands
```

## 一个月 MVP 范围

| 模块 | MVP 范围 |
|---|---|
| `Project Format` | `project.json` + `main.scene.json` + `materials/*.json` |
| `Build Tool` | 校验引用、编译 shader、拷贝资源、调用 Gradle |
| `Scene Runtime` | GameObject hierarchy、Transform、MeshRenderer、Camera、Light |
| `UI Runtime` | Image、Button、ProgressBar，不做字体 |
| `Script Runtime` | Java `Start`、`Update`、`OnClick` |
| `Material System` | basic PBR 参数绑定 |
| `Renderer` | PBR mesh、UI pass、tone mapping |
| `Compute` | GPU particle 或 compute bloom 二选一 |
| `Vulkan RHI` | 最小 device、swapchain、resource、descriptor、pipeline、command、sync |
| `Profiler` | logcat 输出 frame time、pass time、draw call |

## 两人纵向分工

不要按底层/上层切分，而是按完整功能链路切分。

| 人员 | 负责方向 | 完整链路 |
|---|---|---|
| A | Scene + Material + PBR | `scene/material data -> runtime load -> MaterialSystem -> PBR Pass -> Vulkan graphics pipeline` |
| B | UI + Script + Compute | `UI component/script binding -> Java callback -> UI/Compute Pass -> Vulkan compute pipeline` |

共同负责：

```text
Project Format
Engine Runtime lifecycle
Vulkan RHI abstraction
Build pipeline
README / architecture / demo video / performance report
```

## 最终 Demo

```text
PC:
  编辑 project.json、main.scene.json、material.json、PlayerController.java
  运行 ave build android

Android:
  APK 启动
  自动加载场景
  显示 3D 物体 + PBR 材质
  显示图片按钮和进度条 UI
  点击按钮调用 Java 脚本
  触发 compute particle 或 bloom 效果
  Vulkan backend 完全隐藏在 engine 内部
```
