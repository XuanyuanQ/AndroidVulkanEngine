#include "ave/render/RenderPasses.h"

#include "ave/project/SharedDataContract.h"
#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkContext.hpp"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "VkSwapchain.hpp"
#include <android/log.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ave::render {
namespace {
// --- GPU Compute Culling Global Shared State ---
static std::vector<uint32_t> g_culling_visibility;
static uint32_t g_culling_shader_id = 0;

static const std::vector<uint32_t> g_culling_shader_spirv = {
    0x07230203u, 0x00010000u, 0x000d000bu, 0x000000a3u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0006000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000bu, 0x00060010u, 0x00000004u,
    0x00000011u, 0x00000010u, 0x00000001u, 0x00000001u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x000a0004u,
    0x475f4c47u, 0x4c474f4fu, 0x70635f45u, 0x74735f70u, 0x5f656c79u, 0x656e696cu, 0x7269645fu, 0x69746365u,
    0x00006576u, 0x00080004u, 0x475f4c47u, 0x4c474f4fu, 0x6e695f45u, 0x64756c63u, 0x69645f65u, 0x74636572u,
    0x00657669u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00040005u, 0x00000008u, 0x64695f67u,
    0x00000000u, 0x00080005u, 0x0000000bu, 0x475f6c67u, 0x61626f6cu, 0x766e496cu, 0x7461636fu, 0x496e6f69u,
    0x00000044u, 0x00060005u, 0x00000015u, 0x68737550u, 0x736e6f43u, 0x746e6174u, 0x00000073u, 0x00050006u,
    0x00000015u, 0x00000000u, 0x6e616c70u, 0x00007365u, 0x00070006u, 0x00000015u, 0x00000001u, 0x61746f74u,
    0x6e695f6cu, 0x6e617473u, 0x00736563u, 0x00040005u, 0x00000017u, 0x68737570u, 0x00000000u, 0x00040005u,
    0x00000023u, 0x5f736f70u, 0x00646172u, 0x00050005u, 0x00000024u, 0x74736e49u, 0x65636e61u, 0x00000000u,
    0x00070006u, 0x00000024u, 0x00000000u, 0x69736f70u, 0x6e6f6974u, 0x6461725fu, 0x00737569u, 0x00050005u,
    0x00000026u, 0x75706e49u, 0x66754274u, 0x00726566u, 0x00060006u, 0x00000026u, 0x00000000u, 0x74736e69u,
    0x65636e61u, 0x00000073u, 0x00050005u, 0x00000028u, 0x75706e69u, 0x61645f74u, 0x00006174u, 0x00040005u,
    0x00000030u, 0x746e6563u, 0x00007265u, 0x00040005u, 0x00000034u, 0x69646172u, 0x00007375u, 0x00050005u,
    0x00000038u, 0x5f6e696du, 0x6e756f62u, 0x00007364u, 0x00050005u, 0x0000003du, 0x5f78616du, 0x6e756f62u,
    0x00007364u, 0x00040005u, 0x00000043u, 0x69736976u, 0x00656c62u, 0x00030005u, 0x00000046u, 0x00000069u,
    0x00040005u, 0x0000004fu, 0x6e616c70u, 0x00000065u, 0x00030005u, 0x00000054u, 0x00007870u, 0x00030005u,
    0x00000062u, 0x00007970u, 0x00030005u, 0x00000070u, 0x00007a70u, 0x00040005u, 0x0000007eu, 0x74736964u,
    0x00000000u, 0x00060005u, 0x00000099u, 0x7074754fu, 0x75427475u, 0x72656666u, 0x00000000u, 0x00060006u,
    0x00000099u, 0x00000000u, 0x69736976u, 0x696c6962u, 0x00007974u, 0x00050005u, 0x0000009bu, 0x7074756fu,
    0x645f7475u, 0x00617461u, 0x00040047u, 0x0000000bu, 0x0000000bu, 0x0000001cu, 0x00040047u, 0x00000014u,
    0x00000006u, 0x00000010u, 0x00030047u, 0x00000015u, 0x00000002u, 0x00050048u, 0x00000015u, 0x00000000u,
    0x00000023u, 0x00000000u, 0x00050048u, 0x00000015u, 0x00000001u, 0x00000023u, 0x00000060u, 0x00050048u,
    0x00000024u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00040047u, 0x00000025u, 0x00000006u, 0x00000010u,
    0x00030047u, 0x00000026u, 0x00000003u, 0x00040048u, 0x00000026u, 0x00000000u, 0x00000018u, 0x00050048u,
    0x00000026u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u, 0x00000028u, 0x00000018u, 0x00040047u,
    0x00000028u, 0x00000021u, 0x00000000u, 0x00040047u, 0x00000028u, 0x00000022u, 0x00000000u, 0x00040047u,
    0x00000098u, 0x00000006u, 0x00000004u, 0x00030047u, 0x00000099u, 0x00000003u, 0x00040048u, 0x00000099u,
    0x00000000u, 0x00000019u, 0x00050048u, 0x00000099u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u,
    0x0000009bu, 0x00000019u, 0x00040047u, 0x0000009bu, 0x00000021u, 0x00000001u, 0x00040047u, 0x0000009bu,
    0x00000022u, 0x00000000u, 0x00040047u, 0x000000a2u, 0x0000000bu, 0x00000019u, 0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u, 0x00040020u,
    0x00000007u, 0x00000007u, 0x00000006u, 0x00040017u, 0x00000009u, 0x00000006u, 0x00000003u, 0x00040020u,
    0x0000000au, 0x00000001u, 0x00000009u, 0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u, 0x0004002bu,
    0x00000006u, 0x0000000cu, 0x00000000u, 0x00040020u, 0x0000000du, 0x00000001u, 0x00000006u, 0x00030016u,
    0x00000011u, 0x00000020u, 0x00040017u, 0x00000012u, 0x00000011u, 0x00000004u, 0x0004002bu, 0x00000006u,
    0x00000013u, 0x00000006u, 0x0004001cu, 0x00000014u, 0x00000012u, 0x00000013u, 0x0004001eu, 0x00000015u,
    0x00000014u, 0x00000006u, 0x00040020u, 0x00000016u, 0x00000009u, 0x00000015u, 0x0004003bu, 0x00000016u,
    0x00000017u, 0x00000009u, 0x00040015u, 0x00000018u, 0x00000020u, 0x00000001u, 0x0004002bu, 0x00000018u,
    0x00000019u, 0x00000001u, 0x00040020u, 0x0000001au, 0x00000009u, 0x00000006u, 0x00020014u, 0x0000001du,
    0x00040020u, 0x00000022u, 0x00000007u, 0x00000012u, 0x0003001eu, 0x00000024u, 0x00000012u, 0x0003001du,
    0x00000025u, 0x00000024u, 0x0003001eu, 0x00000026u, 0x00000025u, 0x00040020u, 0x00000027u, 0x00000002u,
    0x00000026u, 0x0004003bu, 0x00000027u, 0x00000028u, 0x00000002u, 0x0004002bu, 0x00000018u, 0x00000029u,
    0x00000000u, 0x00040020u, 0x0000002bu, 0x00000002u, 0x00000012u, 0x00040017u, 0x0000002eu, 0x00000011u,
    0x00000003u, 0x00040020u, 0x0000002fu, 0x00000007u, 0x0000002eu, 0x00040020u, 0x00000033u, 0x00000007u,
    0x00000011u, 0x0004002bu, 0x00000006u, 0x00000035u, 0x00000003u, 0x00040020u, 0x00000042u, 0x00000007u,
    0x0000001du, 0x00030029u, 0x0000001du, 0x00000044u, 0x00040020u, 0x00000045u, 0x00000007u, 0x00000018u,
    0x0004002bu, 0x00000018u, 0x0000004du, 0x00000006u, 0x00040020u, 0x00000051u, 0x00000009u, 0x00000012u,
    0x0004002bu, 0x00000011u, 0x00000057u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000063u, 0x00000001u,
    0x0004002bu, 0x00000006u, 0x00000071u, 0x00000002u, 0x0003002au, 0x0000001du, 0x00000094u, 0x0003001du,
    0x00000098u, 0x00000006u, 0x0003001eu, 0x00000099u, 0x00000098u, 0x00040020u, 0x0000009au, 0x00000002u,
    0x00000099u, 0x0004003bu, 0x0000009au, 0x0000009bu, 0x00000002u, 0x00040020u, 0x0000009fu, 0x00000002u,
    0x00000006u, 0x0004002bu, 0x00000006u, 0x000000a1u, 0x00000010u, 0x0006002cu, 0x00000009u, 0x000000a2u,
    0x000000a1u, 0x00000063u, 0x00000063u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000005u, 0x0004003bu, 0x00000007u, 0x00000008u, 0x00000007u, 0x0004003bu, 0x00000022u,
    0x00000023u, 0x00000007u, 0x0004003bu, 0x0000002fu, 0x00000030u, 0x00000007u, 0x0004003bu, 0x00000033u,
    0x00000034u, 0x00000007u, 0x0004003bu, 0x0000002fu, 0x00000038u, 0x00000007u, 0x0004003bu, 0x0000002fu,
    0x0000003du, 0x00000007u, 0x0004003bu, 0x00000042u, 0x00000043u, 0x00000007u, 0x0004003bu, 0x00000045u,
    0x00000046u, 0x00000007u, 0x0004003bu, 0x00000022u, 0x0000004fu, 0x00000007u, 0x0004003bu, 0x00000033u,
    0x00000054u, 0x00000007u, 0x0004003bu, 0x00000033u, 0x00000059u, 0x00000007u, 0x0004003bu, 0x00000033u,
    0x00000062u, 0x00000007u, 0x0004003bu, 0x00000033u, 0x00000067u, 0x00000007u, 0x0004003bu, 0x00000033u,
    0x00000070u, 0x00000007u, 0x0004003bu, 0x00000033u, 0x00000075u, 0x00000007u, 0x0004003bu, 0x00000033u,
    0x0000007eu, 0x00000007u, 0x00050041u, 0x0000000du, 0x0000000eu, 0x0000000bu, 0x0000000cu, 0x0004003du,
    0x00000006u, 0x0000000fu, 0x0000000eu, 0x0003003eu, 0x00000008u, 0x0000000fu, 0x0004003du, 0x00000006u,
    0x00000010u, 0x00000008u, 0x00050041u, 0x0000001au, 0x0000001bu, 0x00000017u, 0x00000019u, 0x0004003du,
    0x00000006u, 0x0000001cu, 0x0000001bu, 0x000500aeu, 0x0000001du, 0x0000001eu, 0x00000010u, 0x0000001cu,
    0x000300f7u, 0x00000020u, 0x00000000u, 0x000400fau, 0x0000001eu, 0x0000001fu, 0x00000020u, 0x000200f8u,
    0x0000001fu, 0x000100fdu, 0x000200f8u, 0x00000020u, 0x0004003du, 0x00000006u, 0x0000002au, 0x00000008u,
    0x00070041u, 0x0000002bu, 0x0000002cu, 0x00000028u, 0x00000029u, 0x0000002au, 0x00000029u, 0x0004003du,
    0x00000012u, 0x0000002du, 0x0000002cu, 0x0003003eu, 0x00000023u, 0x0000002du, 0x0004003du, 0x00000012u,
    0x00000031u, 0x00000023u, 0x0008004fu, 0x0000002eu, 0x00000032u, 0x00000031u, 0x00000031u, 0x00000000u,
    0x00000001u, 0x00000002u, 0x0003003eu, 0x00000030u, 0x00000032u, 0x00050041u, 0x00000033u, 0x00000036u,
    0x00000023u, 0x00000035u, 0x0004003du, 0x00000011u, 0x00000037u, 0x00000036u, 0x0003003eu, 0x00000034u,
    0x00000037u, 0x0004003du, 0x0000002eu, 0x00000039u, 0x00000030u, 0x0004003du, 0x00000011u, 0x0000003au,
    0x00000034u, 0x00060050u, 0x0000002eu, 0x0000003bu, 0x0000003au, 0x0000003au, 0x0000003au, 0x00050083u,
    0x0000002eu, 0x0000003cu, 0x00000039u, 0x0000003bu, 0x0003003eu, 0x00000038u, 0x0000003cu, 0x0004003du,
    0x0000002eu, 0x0000003eu, 0x00000030u, 0x0004003du, 0x00000011u, 0x0000003fu, 0x00000034u, 0x00060050u,
    0x0000002eu, 0x00000040u, 0x0000003fu, 0x0000003fu, 0x0000003fu, 0x00050081u, 0x0000002eu, 0x00000041u,
    0x0000003eu, 0x00000040u, 0x0003003eu, 0x0000003du, 0x00000041u, 0x0003003eu, 0x00000043u, 0x00000044u,
    0x0003003eu, 0x00000046u, 0x00000029u, 0x000200f9u, 0x00000047u, 0x000200f8u, 0x00000047u, 0x000400f6u,
    0x00000049u, 0x0000004au, 0x00000000u, 0x000200f9u, 0x0000004bu, 0x000200f8u, 0x0000004bu, 0x0004003du,
    0x00000018u, 0x0000004cu, 0x00000046u, 0x000500b1u, 0x0000001du, 0x0000004eu, 0x0000004cu, 0x0000004du,
    0x000400fau, 0x0000004eu, 0x00000048u, 0x00000049u, 0x000200f8u, 0x00000048u, 0x0004003du, 0x00000018u,
    0x00000050u, 0x00000046u, 0x00060041u, 0x00000051u, 0x00000052u, 0x00000017u, 0x00000029u, 0x00000050u,
    0x0004003du, 0x00000012u, 0x00000053u, 0x00000052u, 0x0003003eu, 0x0000004fu, 0x00000053u, 0x00050041u,
    0x00000033u, 0x00000055u, 0x0000004fu, 0x0000000cu, 0x0004003du, 0x00000011u, 0x00000056u, 0x00000055u,
    0x000500bau, 0x0000001du, 0x00000058u, 0x00000056u, 0x00000057u, 0x000300f7u, 0x0000005bu, 0x00000000u,
    0x000400fau, 0x00000058u, 0x0000005au, 0x0000005eu, 0x000200f8u, 0x0000005au, 0x00050041u, 0x00000033u,
    0x0000005cu, 0x0000003du, 0x0000000cu, 0x0004003du, 0x00000011u, 0x0000005du, 0x0000005cu, 0x0003003eu,
    0x00000059u, 0x0000005du, 0x000200f9u, 0x0000005bu, 0x000200f8u, 0x0000005eu, 0x00050041u, 0x00000033u,
    0x0000005fu, 0x00000038u, 0x0000000cu, 0x0004003du, 0x00000011u, 0x00000060u, 0x0000005fu, 0x0003003eu,
    0x00000059u, 0x00000060u, 0x000200f9u, 0x0000005bu, 0x000200f8u, 0x0000005bu, 0x0004003du, 0x00000011u,
    0x00000061u, 0x00000059u, 0x0003003eu, 0x00000054u, 0x00000061u, 0x00050041u, 0x00000033u, 0x00000064u,
    0x0000004fu, 0x00000063u, 0x0004003du, 0x00000011u, 0x00000065u, 0x00000064u, 0x000500bau, 0x0000001du,
    0x00000066u, 0x00000065u, 0x00000057u, 0x000300f7u, 0x00000069u, 0x00000000u, 0x000400fau, 0x00000066u,
    0x00000068u, 0x0000006cu, 0x000200f8u, 0x00000068u, 0x00050041u, 0x00000033u, 0x0000006au, 0x0000003du,
    0x00000063u, 0x0004003du, 0x00000011u, 0x0000006bu, 0x0000006au, 0x0003003eu, 0x00000067u, 0x0000006bu,
    0x000200f9u, 0x00000069u, 0x000200f8u, 0x0000006cu, 0x00050041u, 0x00000033u, 0x0000006du, 0x00000038u,
    0x00000063u, 0x0004003du, 0x00000011u, 0x0000006eu, 0x0000006du, 0x0003003eu, 0x00000067u, 0x0000006eu,
    0x000200f9u, 0x00000069u, 0x000200f8u, 0x00000069u, 0x0004003du, 0x00000011u, 0x0000006fu, 0x00000067u,
    0x0003003eu, 0x00000062u, 0x0000006fu, 0x00050041u, 0x00000033u, 0x00000072u, 0x0000004fu, 0x00000071u,
    0x0004003du, 0x00000011u, 0x00000073u, 0x00000072u, 0x000500bau, 0x0000001du, 0x00000074u, 0x00000073u,
    0x00000057u, 0x000300f7u, 0x00000077u, 0x00000000u, 0x000400fau, 0x00000074u, 0x00000076u, 0x0000007au,
    0x000200f8u, 0x00000076u, 0x00050041u, 0x00000033u, 0x00000078u, 0x0000003du, 0x00000071u, 0x0004003du,
    0x00000011u, 0x00000079u, 0x00000078u, 0x0003003eu, 0x00000075u, 0x00000079u, 0x000200f9u, 0x00000077u,
    0x000200f8u, 0x0000007au, 0x00050041u, 0x00000033u, 0x0000007bu, 0x00000038u, 0x00000071u, 0x0004003du,
    0x00000011u, 0x0000007cu, 0x0000007bu, 0x0003003eu, 0x00000075u, 0x0000007cu, 0x000200f9u, 0x00000077u,
    0x000200f8u, 0x00000077u, 0x0004003du, 0x00000011u, 0x0000007du, 0x00000075u, 0x0003003eu, 0x00000070u,
    0x0000007du, 0x00050041u, 0x00000033u, 0x0000007fu, 0x0000004fu, 0x0000000cu, 0x0004003du, 0x00000011u,
    0x00000080u, 0x0000007fu, 0x0004003du, 0x00000011u, 0x00000081u, 0x00000054u, 0x00050085u, 0x00000011u,
    0x00000082u, 0x00000080u, 0x00000081u, 0x00050041u, 0x00000033u, 0x00000083u, 0x0000004fu, 0x00000063u,
    0x0004003du, 0x00000011u, 0x00000084u, 0x00000083u, 0x0004003du, 0x00000011u, 0x00000085u, 0x00000062u,
    0x00050085u, 0x00000011u, 0x00000086u, 0x00000084u, 0x00000085u, 0x00050081u, 0x00000011u, 0x00000087u,
    0x00000082u, 0x00000086u, 0x00050041u, 0x00000033u, 0x00000088u, 0x0000004fu, 0x00000071u, 0x0004003du,
    0x00000011u, 0x00000089u, 0x00000088u, 0x0004003du, 0x00000011u, 0x0000008au, 0x00000070u, 0x00050085u,
    0x00000011u, 0x0000008bu, 0x00000089u, 0x0000008au, 0x00050081u, 0x00000011u, 0x0000008cu, 0x00000087u,
    0x0000008bu, 0x00050041u, 0x00000033u, 0x0000008du, 0x0000004fu, 0x00000035u, 0x0004003du, 0x00000011u,
    0x0000008eu, 0x0000008du, 0x00050081u, 0x00000011u, 0x0000008fu, 0x0000008cu, 0x0000008eu, 0x0003003eu,
    0x0000007eu, 0x0000008fu, 0x0004003du, 0x00000011u, 0x00000090u, 0x0000007eu, 0x000500b8u, 0x0000001du,
    0x00000091u, 0x00000090u, 0x00000057u, 0x000300f7u, 0x00000093u, 0x00000000u, 0x000400fau, 0x00000091u,
    0x00000092u, 0x00000093u, 0x000200f8u, 0x00000092u, 0x0003003eu, 0x00000043u, 0x00000094u, 0x000200f9u,
    0x00000049u, 0x000200f8u, 0x00000093u, 0x000200f9u, 0x0000004au, 0x000200f8u, 0x0000004au, 0x0004003du,
    0x00000018u, 0x00000096u, 0x00000046u, 0x00050080u, 0x00000018u, 0x00000097u, 0x00000096u, 0x00000019u,
    0x0003003eu, 0x00000046u, 0x00000097u, 0x000200f9u, 0x00000047u, 0x000200f8u, 0x00000049u, 0x0004003du,
    0x00000006u, 0x0000009cu, 0x00000008u, 0x0004003du, 0x0000001du, 0x0000009du, 0x00000043u, 0x000600a9u,
    0x00000006u, 0x0000009eu, 0x0000009du, 0x00000063u, 0x0000000cu, 0x00060041u, 0x0000009fu, 0x000000a0u,
    0x0000009bu, 0x00000029u, 0x0000009cu, 0x0003003eu, 0x000000a0u, 0x0000009eu, 0x000100fdu, 0x00010038u
};
// ----------------------------------------------


vk::Sampler GetCommonSampler(vkfw::VkContext& ctx)
{
    static std::unique_ptr<vk::raii::Sampler> sampler;
    if (!sampler) {
        vk::SamplerCreateInfo create_info{};
        create_info.magFilter = vk::Filter::eLinear;
        create_info.minFilter = vk::Filter::eLinear;
        create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeW = vk::SamplerAddressMode::eRepeat;
        create_info.maxLod = VK_LOD_CLAMP_NONE;
        sampler = std::make_unique<vk::raii::Sampler>(ctx.Device(), create_info);
    }
    return **sampler;
}

void EnsureFallbackWhiteTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture)
{
    if (texture.IsInitialized()) {
        return;
    }

    uint32_t const white_pixel = 0xFFFFFFFFu;
    texture.Init(ctx, vkfw::TextureInfo{
                          .width = 1,
                          .height = 1,
                          .mip_levels = 1,
                          .format = vkfw::TextureFormat::R8G8B8A8_UNORM,
                          .usage = vkfw::TextureUsage::Sampled,
                          .mipmap = false,
                      });
    texture.UpdateData(ctx, &white_pixel, sizeof(white_pixel));
}

vkfw::VkTexture const* ResolveTextureOrFallback(vkfw::VkContext& ctx,
                                                ave::resource::TextureManager& texture_mgr,
                                                uint32_t texture_id,
                                                vkfw::VkTexture& fallback_texture)
{
    if (texture_id != 0) {
        if (auto const* runtime = texture_mgr.GetTexture(texture_id)) {
            if (runtime->texture && runtime->texture->IsInitialized()) {
                return runtime->texture.get();
            }
        }
    }

    EnsureFallbackWhiteTexture(ctx, fallback_texture);
    return fallback_texture.IsInitialized() ? &fallback_texture : nullptr;
}

uint32_t VertexLayoutIdFromMesh(ave::resource::MeshRuntime const& mesh)
{
    if (mesh.vertex_stride == 7u * sizeof(float)) {
        return 1;
    }
    if (mesh.vertex_stride == sizeof(ave::project::VertexData)) {
        return 2;
    }
    return 0;
}

DescriptorSetLayoutKey MakeFrameSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
    };
    return key;
}

DescriptorSetLayoutKey MakeMaterialSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 2,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 3,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

PipelineKey MakePipelineKey(uint32_t pass_id,
                            uint32_t shader_id,
                            ave::resource::MeshRuntime const& mesh)
{
    PipelineKey key{};
    key.pass_id = pass_id;
    key.shader_id = shader_id;
    key.vertex_layout_id = VertexLayoutIdFromMesh(mesh);
    key.render_state_id = 1;
    key.layout_profile = 0;
    key.rt_format = 0;
    key.depth_format = 0;
    key.stencil_format = 0;
    key.sample_count = 1;
    key.viewport_width = 0;
    key.viewport_height = 0;
    return key;
}

bool BeginSwapchainRendering(RenderPassContext const& context, vk::ClearValue const& clear_value, bool clear_color)
{
    if (context.vk == nullptr || context.swapchain == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return false;
    }

    auto const extent = context.swapchain->Extent();
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;

    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;

            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;

            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    if (context.compatibility_render_pass == vk::RenderPass{} ||
        context.compatibility_framebuffer == vk::Framebuffer{}) {
        return false;
    }

    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = context.compatibility_render_pass;
    render_pass_begin.framebuffer = context.compatibility_framebuffer;
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, extent};
    render_pass_begin.clearValueCount = 1;
    render_pass_begin.pClearValues = &clear_value;
    context.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    return true;
}

void EndSwapchainRendering(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            context.command_buffer.endRendering();
        } else {
            context.command_buffer.endRenderingKHR();
        }
    } else {
        context.command_buffer.endRenderPass();
    }
}

} // namespace

PassDataFilter DepthPrepass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::DepthPrepass;
    filter.opaque_only = true;
    return filter;
}

void DepthPrepass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: DepthPrepass");
    (void)context;
    (void)view;
}

PassDataFilter ShadowPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Shadow;
    filter.opaque_only = true;
    filter.shadow_casters_only = true;
    return filter;
}

void ShadowPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ShadowPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();

    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    bool began_rendering = false;
    if (has_vk) {
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.03f;
        clear.color.float32[1] = 0.04f;
        clear.color.float32[2] = 0.06f;
        clear.color.float32[3] = 1.0f;
        began_rendering = BeginSwapchainRendering(context, clear, false);
    }

    // For shadow pass we only need material (if any) and mesh data.
    // No frame UBO is required.

    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }

        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            continue;
        }

        auto const* shader = material->shader_id != 0 ? shader_mgr.GetShader(material->shader_id) : nullptr;
        if (!shader) {
            continue;
        }

        PipelineKey key = MakePipelineKey(1, shader->id, *mesh);
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0 || !has_vk) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        vk::DeviceSize offset = 0;
        context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
        if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
            context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
            uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
            context.command_buffer.drawIndexed(index_count, 1, renderable->first_index, static_cast<int32_t>(renderable->first_vertex), 0);
        } else {
            uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
            context.command_buffer.draw(vertex_count, 1, renderable->first_vertex, 0);
        }
    }

    if (began_rendering) {
        EndSwapchainRendering(context);
    }
}

PassDataFilter PBRPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ForwardOpaque;
    filter.layer_mask = 0xFFFFFFFFu;
    return filter;
}

void PBRPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: PBRPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct FrameUbo {
        glm::mat4 view_projection{1.0f};
    };

    struct MaterialUbo {
        glm::vec4 base_color{1.0f};
        glm::vec4 params{0.0f};
    };

    bool began_rendering = false;
    if (has_vk) {
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.03f;
        clear.color.float32[1] = 0.04f;
        clear.color.float32[2] = 0.06f;
        clear.color.float32[3] = 1.0f;
        began_rendering = BeginSwapchainRendering(context, clear, true);
        if (!began_rendering) {
            __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "PBRPass failed to begin rendering");
            return;
        }
    }

    if (has_vk) {
        FrameUbo frame_ubo{};
        if (context.frame != nullptr) {
            frame_ubo.view_projection = context.frame->view.view_projection;
        }

        if (!frame_ubo_.IsInitialized()) {
            frame_ubo_.Init(*context.vk, vkfw::BufferInfo{
                                            .size = static_cast<uint32_t>(sizeof(FrameUbo)),
                                            .usage = vkfw::BufferUsage::Uniform,
                                            .mappable = true,
                                        });
        }
        frame_ubo_.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(FrameUbo)));

        if (frame_set_id_ == 0) {
            uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
            frame_set_id_ = desc_alloc.AllocateDescriptorSet(frame_layout_id);
        }
        if (frame_set_id_ != 0) {
            desc_alloc.UpdateUniformBuffer(frame_set_id_, 0, frame_ubo_.Handle(), 0, sizeof(FrameUbo));
        }
    }
    uint32_t renderable_index = 0;
    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            renderable_index++;
            continue;
        }

        // Apply GPU-based/CPU-fallback Frustum Culling
        if (renderable_index < g_culling_visibility.size() && g_culling_visibility[renderable_index] == 0) {
            __android_log_print(ANDROID_LOG_INFO, "CullingSystem", "  Skip draw call (culled): %s", renderable->debug_name.c_str());
            renderable_index++;
            continue;
        }
        __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "frame_index: %llu", context.frame->frame_index);
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip material: %s", renderable->material_id.c_str());
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip mesh: %s", renderable->mesh_id.c_str());
            continue;
        }

        auto const* shader = material->shader_id != 0 ? shader_mgr.GetShader(material->shader_id) : nullptr;
        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip shader for material: %s", material->name.c_str());
            continue;
        }

        PipelineKey key = MakePipelineKey(0, shader->id, *mesh);
        key.layout_profile = 2;
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  pipeline create failed: %s", renderable->debug_name.c_str());
            continue;
        }

        if (!has_vk) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        auto& material_binding = material_bindings_[material->id];
        if (!material_binding.ubo.IsInitialized()) {
            material_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                       .size = static_cast<uint32_t>(sizeof(MaterialUbo)),
                                                       .usage = vkfw::BufferUsage::Uniform,
                                                       .mappable = true,
                                                   });
        }
        if (material_binding.descriptor_set_id == 0) {
            uint32_t const material_layout_id = desc_cache.GetOrCreateLayout(MakeMaterialSetLayoutKey());
            material_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(material_layout_id);
        }

        MaterialUbo material_ubo{};
        material_ubo.base_color = material->base_color;
        material_ubo.params = glm::vec4(material->metallic, material->roughness, 0.0f, 0.0f);
        material_binding.ubo.UpdateData(*context.vk, &material_ubo, static_cast<uint32_t>(sizeof(MaterialUbo)));

        if (material_binding.descriptor_set_id != 0) {
            desc_alloc.UpdateUniformBuffer(material_binding.descriptor_set_id,
                                           0,
                                           material_binding.ubo.Handle(),
                                           0,
                                           sizeof(MaterialUbo));

            vk::Sampler const sampler = GetCommonSampler(*context.vk);
            if (auto const* base_color_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->base_color_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              1,
                                              sampler,
                                              base_color_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* normal_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->normal_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              2,
                                              sampler,
                                              normal_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* mr_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->metallic_roughness_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              3,
                                              sampler,
                                              mr_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        vk::DescriptorSet sets[2]{};
        uint32_t set_count = 0;
        if (frame_set_id_ != 0) {
            vk::DescriptorSet const frame_set = desc_alloc.GetHandle(frame_set_id_);
            if (frame_set) {
                sets[set_count++] = frame_set;
            }
        }
        if (material_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const material_set = desc_alloc.GetHandle(material_binding.descriptor_set_id);
            if (material_set) {
                sets[set_count++] = material_set;
            }
        }
        if (set_count > 0) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      set_count,
                                                      sets,
                                                      0,
                                                      nullptr);
        }

        vk::DeviceSize offset = 0;
        context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
        if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
            context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
            uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
            context.command_buffer.drawIndexed(index_count, 1, renderable->first_index, static_cast<int32_t>(renderable->first_vertex), 0);
        } else {
            uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
            context.command_buffer.draw(vertex_count, 1, renderable->first_vertex, 0);
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }

    if (began_rendering) {
        EndSwapchainRendering(context);
    }
}

PassDataFilter ComputePass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Compute;
    return filter;
}

void ComputePass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ComputePass");
    (void)view;
}

PassDataFilter UIPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::UI;
    filter.layer_mask = core::ToMask(core::RenderLayer::UI);
    return filter;
}

void UIPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: UIPass");
    for (auto const* item : view.ui_items) {
        if (item) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  ui: %s", item->debug_name.c_str());
        }
    }
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    if (has_vk) {
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.0f;
        clear.color.float32[1] = 0.0f;
        clear.color.float32[2] = 0.0f;
        clear.color.float32[3] = 0.0f;
        if (BeginSwapchainRendering(context, clear, false)) {
            EndSwapchainRendering(context);
        }
    }
}

PassDataFilter ToneMappingPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ToneMapping;
    return filter;
}

void ToneMappingPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ToneMappingPass");
    (void)context;
    (void)view;
}

} // namespace ave::render
