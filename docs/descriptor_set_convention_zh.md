# Descriptor 绑定约定（Set0/Set1/Set2）

目标：在不做 SPIR-V 反射的前提下，先用**固定约定**把 `PipelineLayout` / `DescriptorSetLayout` / `DescriptorSet` 体系跑通，并为后续扩展预留空间。

## 总体约定

- **Set0 = Frame（每帧）**
- **Set1 = Material（每材质）**
- **Set2 = Object（每对象/每 draw）**（可选；优先演进为 SSBO + objectIndex）
- **Set3+** 预留给 bindless / 特殊 pass（后处理、compute 等）

在代码里通过 `ave::render::PipelineKey::layout_profile` 选择布局：

- `0`：空 layout（无 descriptor set）——最小 demo
- `1`：Set0 + Set1 + Set2
- `2`：Set0 + Set1
- `3`：Set0 only

## Set0（Frame）

| binding | type | 含义 | 备注 |
|---:|---|---|---|
| 0 | `UniformBuffer` | `FrameGlobals`（view/proj、时间等） | 必选 |
| 1 | `CombinedImageSampler` | `ShadowMap` / 全局纹理 | 预留（未用也绑定默认纹理） |

## Set1（Material）

| binding | type | 含义 | 备注 |
|---:|---|---|---|
| 0 | `UniformBuffer` | `MaterialParams`（baseColor、metallic、roughness…） | 必选 |
| 1 | `CombinedImageSampler` | BaseColor | 未用绑定白图 |
| 2 | `CombinedImageSampler` | Normal | 预留 |
| 3 | `CombinedImageSampler` | MetallicRoughness | 预留 |

## Set2（Object）

| binding | type | 含义 | 备注 |
|---:|---|---|---|
| 0 | `StorageBuffer` | `ObjectDataBuffer`（数组：worldMatrix 等） | 推荐：push constant 传 objectIndex |

## 未来扩展（预留）

- UI：可以用 `layout_profile=2`（Set0+Set1），或单独定义 UI profile（Set1 的纹理槽不同）
- PostFX/Compute：用 Set3 或单独的 compute profile，避免污染通用 PBR 的 set 结构
- Bindless：Set3 放全局 texture table / sampler table，Set1 只存索引

