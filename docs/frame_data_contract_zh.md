# Ave 层间数据结构约定：XML Components → 资源系统 → FrameData（渲染提交）

这份文档只做一件事：**把层与层之间传递的数据结构定义清楚**，让后续实现（XML 解析/资源管理/渲染通道）有统一目标。

---

## 0. 三层输出物（你关心的“是什么结构”）

1) **XML 解析层输出（Authoring 数据）**  
   - 结构来源：`include/ave/project/SharedDataContract.h`  
   - 核心：`project::SceneData / GameObjectData / ComponentData`（所有组件都是 `optional`）

2) **资源系统输出（Runtime 资源句柄/表）**  
   - 结构目标：把 `mesh_id/material_id/texture_id/shader_id` 解析成“可复用、可缓存”的 runtime 句柄  
   - 关键：**渲染提交只携带 id/句柄，不携带 XML 文本或 Vulkan 句柄**

3) **渲染提交输出（每帧 FrameData）**  
   - 结构来源：`include/ave/core/FrameData.h`  
   - 核心：`core::FrameData`（view + renderables + lights + ui_items + resources）

---

## 1. XML → Components（解析层：参照 SharedDataContract.h）

### 1.1 场景与对象

- `project::SceneData`
  - `environment`（clear/ambient）
  - `objects: vector<GameObjectData>`

- `project::GameObjectData`
  - `id / name`
  - `hierarchy`（parent/children）
  - `components: ComponentData`

### 1.2 组件容器（你给的形式）

组件定义以 `include/ave/project/SharedDataContract.h` 为准：

```cpp
struct ComponentData {
    std::optional<TransformData> transform;
    std::optional<TriangleRendererData> triangle_renderer;
    std::optional<MeshRendererData> mesh_renderer;
    std::optional<CameraData> camera;
    std::optional<LightData> light;
    std::optional<ScriptBindingData> script;
    std::optional<ImageComponentData> image;
    std::optional<ButtonComponentData> button;
    std::optional<ProgressBarComponentData> progress_bar;
};
```

### 1.3 解析层的“引用规则”（必须先约定）

解析层对渲染最关键的是这些字符串引用：

- `MeshRendererData.mesh`：mesh 的 **字符串 id**（或 source/path）
- `MeshRendererData.material`：material 的 **字符串 id**（或 source/path）
- `ImageComponentData.texture`：texture 的 **字符串 id**（或 source/path）
- `MaterialData.shader` / `AssetReferenceData.shader`：shader 的 **字符串 id**（或 source/path）

约定：

- **XML 里一律用字符串 id/相对路径引用资源**（Authoring 友好）
- **解析层不创建 GPU 资源**，只保证字段齐全、默认值补齐、引用能被装配器解析

---

## 2. Components → Runtime 资源系统（复用/缓存到底怎么做）

你提到的核心问题是：

- 渲染时怎么知道用哪个资源（mesh/material/texture/shader）？
- 顶点来自哪里？（内嵌 vertices/indices vs 外部模型文件）
- 怎么做复用？（同一个 `mesh_id` 不应重复创建）

### 2.1 必须引入“资源管理三件套”

推荐拆成 3 个 manager（可以合并成一个 ResourceManager，但概念别混）：

1) **ShaderManager**
   - 输入：`shader_id` 或 shader 路径
   - 输出：`ShaderHandle`（pipeline/bytecode 的句柄）

2) **Mesh/VertexManager（或 ModelManager）**
   - 输入：`mesh_id`（+ source/path 或内嵌 vertices/indices）
   - 输出：`MeshHandle`
   - 内部持有：GPU vertex/index buffer + layout（或 CPU staging + GPU handle）

3) **MaterialManager（可以复用现有 MaterialSystem 思路）**
   - 输入：`material_id`（+ shader_id + parameters + textures + render_state）
   - 输出：`MaterialHandle`

> 复用的关键：**manager 必须有“字符串 id → 句柄”的缓存表**。同 id 多次请求返回同句柄（除非热重载/版本变更）。

补充约定（性能演进）：

- 近期：`FrameData` 里保留 `mesh_id/material_id/texture_id` 为字符串，方便调试与快速迭代
- 远期：可在 Build 阶段把字符串映射成 `uint32_t` 句柄，并在 `FrameData` 中改为传句柄（避免每帧哈希查表）

### 2.2 Mesh 的来源约定（解决“用什么顶点”）

`MeshRendererData` 同时支持两种来源（SharedDataContract 里已经有字段）：

1) **外部 mesh**：`mesh` 字段给出 id/source，`vertices/indices` 为空  
2) **内嵌 mesh**：`vertices/indices` 非空（直接从 scene.xml 带数据）

约定：

- Build 阶段将两种来源统一注册为 `mesh_id` 对应的一份 runtime mesh（GPU buffers）
- Frame 提交阶段不再携带大块顶点数据，只携带 `mesh_id`（或 mesh handle）

---

## 3. Runtime → FrameData（渲染接口：参照 FrameData.h）

### 3.1 FrameData（你给的结构，工程已存在）

`include/ave/core/FrameData.h`：

```cpp
struct FrameData {
    uint64_t frame_index = 0;
    FrameViewData view{};
    std::vector<FrameRenderableData> renderables;
    std::vector<FrameLightData> lights;
    std::vector<FrameUiData> ui_items;
    FrameResourceTable resources{};
};
```

其中资源引用的关键字段：

- `FrameRenderableData.mesh_id / material_id`
- `FrameUiData.texture_id / material_id`
- `FrameResourceTable.meshes/materials/textures`（本帧需要的资源 id 列表）

以及“到底画哪段顶点”的关键字段（工程已存在）：

- `FrameRenderableData.index_count / vertex_count`
- `FrameRenderableData.first_index / first_vertex`

约定：

- 如果一个 `mesh_id` 对应多个 submesh，可以通过上述范围字段指定本次 draw 的区间
- 如果范围字段全为 0，则渲染器按 `mesh_id` 对应 mesh 的默认范围绘制（由 MeshManager 提供）

### 3.2 “每个通道画什么”——需要在 FrameData 里表达路由信息

目前的 `FrameRenderableData` **没有直接的“通道/Pass”字段**，只有：

- `visible`
- `casts_shadow / receives_shadow`
- `sort_key`

要让渲染器（或 FrameGraph/RenderPass）能稳定地把 renderables 分配到不同通道，建议在 **FrameRenderableData 增加一种明确的路由方式**（二选一）：

**方案 A（推荐）：位掩码 + 队列**

- `uint32_t layer_mask`：层（世界/角色/UI/特效…）
- `uint32_t pass_mask`：该物体参与哪些 pass（Depth/Shadow/Forward/Transparent/Ui…）
- `uint32_t render_queue`：排序队列（Opaque 2000、AlphaTest 2450、Transparent 3000…）

渲染器按 pass 遍历时做：

- `if (!(pass_mask & kShadowPass)) continue;`
- `if ((layer_mask & pass.layer_mask) == 0) continue;`
- 结合 material 的 `render_state.blend/depth_*` 决定进入 Opaque/Transparent 分支

**方案 B：按通道预分桶**

在 `FrameData` 里不只给 `renderables`，而是给：

- `renderables_depth`
- `renderables_shadow`
- `renderables_forward_opaque`
- `renderables_forward_transparent`
- `ui_items`

优点：pass 逻辑最简单；缺点：FrameData 结构膨胀，通道扩展要改结构。

> 结论：为了后续可扩展（更多 pass、更复杂过滤），建议选 **方案 A**。

建议的 `pass_mask` 最小枚举（文档约定，后续再落到代码 enum/bitflag）：

- `Depth`：深度预通道
- `Shadow`：阴影贴图
- `ForwardOpaque`：前向不透明
- `ForwardTransparent`：前向半透明
- `Ui`：UI 叠加

---

## 3.4 通用渲染数据结构（建议新增/统一的结构体清单）

这一节把“通用、可扩展”的数据结构列出来（字段含义 + 怎么用）。你可以把它们理解为：

- XML/Components 里“作者想要什么”
- Build 阶段把它们规整成 runtime 句柄与派生字段
- Frame 提交时用**统一的路由与状态字段**让各个 pass 过滤

> 说明：下面这些结构体是**设计约定**（目前代码里未必都存在）。你可以逐步落地：先把字段加到 `SharedDataContract` 或 `FrameData`，再补 manager 与 pass 过滤。

### A) 资源标识与句柄（跨层通用）

解析层（XML/Components）建议使用字符串 id：

```cpp
using ResourceId = std::string; // e.g. "meshes/quad", "mat/metal", "tex/ui_atlas"
```

Build/渲染层建议逐步演进到整数句柄（性能更好）：

```cpp
using ResourceHandle = uint32_t; // 0 = invalid
```

使用约定：

- 短期：`FrameData` 里继续传 `ResourceId`（便于调试）
- 长期：Build 阶段把 `ResourceId -> ResourceHandle` 固化，`FrameData` 改传 handle，资源表/查找表在 resource manager 内部

### B) 通用渲染状态 RenderState（决定 Opaque/Transparent、深度、剔除等）

建议用一个“通用状态”承载材质/物体的渲染开关，避免把阴影/透明之类散落到多个地方：

```cpp
enum class CullMode { None, Front, Back };
enum class BlendMode { Opaque, AlphaTest, AlphaBlend, Additive };
enum class DepthFunc { Less, LessEqual, Equal, Greater, Always };

struct RenderState {
    CullMode cull = CullMode::Back;
    bool depth_test = true;
    bool depth_write = true;
    DepthFunc depth_func = DepthFunc::LessEqual;

    BlendMode blend = BlendMode::Opaque; // 决定透明/不透明分队列
    float alpha_cutoff = 0.5f;           // blend==AlphaTest 时生效

    bool cast_shadows = true;            // 是否进入 ShadowPass
    bool receive_shadows = true;         // shading 时是否采样 shadow map

    bool double_sided = false;           // 常用于 foliage/布料
};
```

字段如何使用：

- **是否透明**：由 `blend` 决定
  - `Opaque/AlphaTest` → 进入不透明队列（可进 DepthPrepass）
  - `AlphaBlend/Additive` → 进入透明队列（通常不写深度）
- **阴影**：
  - `cast_shadows=false` → 不进入 ShadowPass（你问的“有的有阴影有的没有阴影”就靠它）
  - `receive_shadows=false` → 仍可渲染，但 shading 不采样阴影
- **深度/剔除**：由 `depth_*` 与 `cull` 决定渲染管线状态（PSO key 的一部分）

来源约定（谁来决定这些字段）：

- **MaterialData.render_state**：给默认值（材质层面的状态）
- **MeshRendererData/对象级 override**：允许对象覆写（例如某个实例不投影）

### C) 通道路由 RenderTags（决定“每个 pass 画哪些对象”）

除了 `RenderState`，还需要一个“路由/分类”结构把 renderable 分到不同 pass / layer / queue：

```cpp
enum class RenderLayer : uint32_t {
    World = 1u << 0,
    UI    = 1u << 1,
    FX    = 1u << 2,
};

enum class RenderPassBits : uint32_t {
    None              = 0,
    Shadow            = 1u << 0,
    DepthPrepass      = 1u << 1,
    ForwardOpaque     = 1u << 2,
    ForwardTransparent= 1u << 3,
    UI                = 1u << 4,
};

struct RenderTags {
    uint32_t layer_mask = static_cast<uint32_t>(RenderLayer::World);
    uint32_t pass_mask  = static_cast<uint32_t>(RenderPassBits::ForwardOpaque)
                        | static_cast<uint32_t>(RenderPassBits::DepthPrepass)
                        | static_cast<uint32_t>(RenderPassBits::Shadow);

    // 排序：常见约定 Opaque=2000, AlphaTest=2450, Transparent=3000, Overlay=4000
    uint32_t render_queue = 2000;
};
```

字段如何使用（核心回答“FrameData 怎么知道每个通道画什么”）：

- ShadowPass：筛 `pass_mask & Shadow` 且 `render_state.cast_shadows==true`
- DepthPrepass：筛 `pass_mask & DepthPrepass` 且 `blend != AlphaBlend/Additive`
- ForwardOpaque：筛 `pass_mask & ForwardOpaque` 且 `render_queue < 3000`
- ForwardTransparent：筛 `pass_mask & ForwardTransparent` 或 `render_queue >= 3000`
- UIPass：筛 `layer_mask & UI` 或 `pass_mask & UI`

`render_queue` 的来源约定：

- 默认由 `RenderState.blend` 推导：
  - Opaque → 2000
  - AlphaTest → 2450
  - AlphaBlend/Additive → 3000
- 允许手动 override（比如 decals/天空盒/后处理叠加等）

### D) MeshDrawRange（一个 mesh 多段绘制：submesh/实例化/合批）

你在 `FrameRenderableData` 里已经有范围字段。建议抽成一个概念（方便在不同地方复用）：

```cpp
struct MeshDrawRange {
    uint32_t index_count = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t first_vertex = 0;
};
```

使用约定：

- `index_count>0`：走 indexed draw
- `index_count==0 && vertex_count>0`：走 non-indexed draw
- 都为 0：用 mesh 的默认范围（由 MeshManager 维护）

### E) FrameRenderableData（最终提交给渲染器的“统一形态”）

现有 `FrameRenderableData` 已经包含 `mesh_id/material_id/world/draw_range` 等关键字段，但缺少通道路由字段。建议最终形态如下（在现有基础上加 `tags`/`state` 的投影字段即可）：

```cpp
struct FrameRenderableData {
    std::string object_id;
    std::string debug_name;

    ResourceId mesh_id;
    ResourceId material_id;

    float world[16]{};
    MeshDrawRange range{};

    RenderTags tags{};     // 新增：通道路由/排序
    RenderState state{};   // 新增：渲染状态（可从 material + override 合成）

    uint64_t sort_key = 0; // 最终排序 key（由 tags/state/camera 距离推导）
    bool visible = true;
};
```

怎么使用（从 Components 到 Frame 的推导规则）：

- `mesh_id`：来自 `MeshRendererData.mesh`（或内嵌顶点生成的 id）
- `material_id`：来自 `MeshRendererData.material`
- `state`：
  - 先从 `MaterialData.render_state` 填默认值
  - 再叠加对象级 override（例如某个物体 `cast_shadows=false`）
- `tags`：
  - 由 `state.blend` 推导 `render_queue`
  - 由组件类型推导 `layer_mask`（UI 组件 → UI layer）
  - `pass_mask` 默认包含 Forward/Depth，若 `state.cast_shadows` 则包含 Shadow

### F) MaterialData 的通用字段建议（补齐“很多场景没考虑到”）

你已经有：

- shader
- parameters（variant）
- textures（slot->texture）
- render_state（目前较简化：cull/depth/blend）

建议把 `MaterialData.render_state` 最终扩展/映射到上面的 `RenderState` 能表达的字段（尤其是：blend 模式、alpha_cutoff、cast/receive shadows、double_sided）。

### 3.3 FrameResourceTable 的定位（“本帧会用到什么资源”）

`FrameResourceTable` 目前是字符串列表：

```cpp
struct FrameResourceTable {
    std::vector<std::string> meshes;
    std::vector<std::string> materials;
    std::vector<std::string> textures;
};
```

约定：

- `FrameResourceTable` 是 **“声明式需求表”**：告诉资源系统“本帧需要这些 id”
- ResourceManager 可以在帧开始时：
  - 预取/预编译缺失资源
  - 做引用计数/逐帧淘汰策略
  - 做异步加载占位（fallback material/mesh/texture）

---

## 4. 实现时的最小流水线（对齐你问的层间结构）

1) XML 解析：
   - `project.xml` → `ProjectConfig`
   - `*.scene.xml` → `SceneDocument(SceneData)`
   - （后续）`*.material.xml` → `MaterialData` 等，汇总到 `SharedDataContract`

2) Build（内容变化/加载场景时）：
   - 遍历 `SceneData.objects`
   - 将 `MeshRendererData/ImageComponentData/MaterialData` 引用到 ResourceManager
   - 建立 `string id → runtime handle` 的缓存（mesh/material/texture/shader）

3) Submit（每帧）：
   - 生成 `core::FrameData`
     - `renderables`：每个对象填 `object_id/world/mesh_id/material_id + 路由信息(pass/layer/queue)`
     - `lights`
     - `ui_items`
     - `resources`：汇总本帧用到的 id

---

## 6. 你问的典型场景：同 mesh 不同 material + 有无阴影（数据流到底长什么样）

### 6.1 XML/Components 层（输入长这样）

假设场景里有 2 个物体：

- `ObjA`：`MeshRenderer(mesh="meshes/quad", material="mat/metal")`，投影 `casts_shadow=true`
- `ObjB`：`MeshRenderer(mesh="meshes/quad", material="mat/plastic")`，不投影 `casts_shadow=false`

它们 **mesh 一样**，material 不同，阴影属性不同。

在 `SharedDataContract` / `SceneData` 里表现为：

- 两个 `GameObjectData`
- 都有 `components.mesh_renderer`
- `mesh_renderer.mesh` 相同（或指向同一份 source/path）
- `mesh_renderer.material` 不同
- 阴影开关建议来自：
  - `MeshRendererData` 的扩展字段（后续可加），或
  - `MaterialData.render_state` / 或独立 `RenderState`（更合理）

### 6.2 Build 阶段（一次/内容变更时）：解决“复用”和“用哪个顶点”

Build 的职责是把字符串 id 变成可复用的 runtime 资源，并建立缓存映射：

- `mesh_id(string) -> MeshHandle`
- `material_id(string) -> MaterialHandle`
- `texture_id(string) -> TextureHandle`
- `shader_id(string) -> ShaderHandle`

关键点：

1) **同一份顶点/索引只创建一次**  
   - `MeshManager.GetOrCreate(mesh_key)`  
   - `mesh_key` 的来源规则：
     - 外部模型：`mesh_key = mesh_id 或 source/path`
     - 内嵌顶点：`mesh_key = hash(vertices, indices, topology)`（或生成稳定 id）

2) **同名材质只创建一次**  
   - `MaterialManager.GetOrCreate(material_id)`  
   - 内部会引用 shader/texture（由 Shader/Texture manager 继续复用）

对上面的例子：

- `meshes/quad` 只会在 `MeshManager` 中生成 **1** 份 mesh（1 份 vertex/index buffer）
- `mat/metal` 与 `mat/plastic` 会生成 **2** 份 material（通常对应不同参数/纹理，shader 可相同也可不同）

### 6.3 Submit 阶段（每帧）：只提交“要画什么 + 用什么资源 + 进哪些通道”

Submit 的产物是 `core::FrameData`。每个可渲染物体生成一个 `FrameRenderableData`：

- **ObjA 的 renderable**
  - `mesh_id = "meshes/quad"`（或 mesh handle）
  - `material_id = "mat/metal"`（或 material handle）
  - `casts_shadow = true`
  - `pass_mask`（建议新增）：包含 `Shadow | Depth | ForwardOpaque`

- **ObjB 的 renderable**
  - `mesh_id = "meshes/quad"`（同一个 mesh id，表示复用同顶点资源）
  - `material_id = "mat/plastic"`
  - `casts_shadow = false`
  - `pass_mask`：包含 `Depth | ForwardOpaque`（不包含 Shadow）

> 注意：此时 FrameData 里**不会出现 vertices/indices 大数组**。顶点数据已经在 Build 阶段进入 MeshManager 并被复用。

### 6.4 Pass/通道阶段：通过过滤规则决定“每个通道画哪些东西”

渲染器（或 FrameGraph/RenderPass）按 pass 运行，每个 pass 做过滤：

- ShadowPass：
  - 只画 `casts_shadow==true` 且 `pass_mask` 包含 Shadow 的 renderables
  - 结果：只画 ObjA

- DepthPrepass：
  - 只画 Opaque（通常 `render_queue < Transparent`）且 `pass_mask` 包含 Depth
  - 结果：画 ObjA + ObjB

- ForwardOpaque：
  - 画 `pass_mask` 包含 ForwardOpaque 的 renderables（再按 sort_key/queue 排序）
  - 结果：画 ObjA + ObjB（两次 draw call，mesh 相同但 material 不同）

总结一句话：

- **“用什么顶点”由 mesh_id/mesh_handle 指定（Build 负责准备/复用）**
- **“用哪个材质”由 material_id/material_handle 指定（Build 负责准备/复用）**
- **“进哪些通道”由 pass_mask/casts_shadow/layer/render_queue 决定（Frame 提交负责表达，Pass 负责过滤）**

---

## 5. 关联文件（便于落地）

- Components/契约：`include/ave/project/SharedDataContract.h`
- FrameData：`include/ave/core/FrameData.h`
- 现有 FrameData 生产示例：`include/ave/scene/SceneWorld.h`、`src/scene/SceneWorld.cpp`
