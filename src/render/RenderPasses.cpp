#include "ave/render/RenderPasses.h"

#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "ave/project/SharedDataContract.h"

#include "VkPipeline.hpp"
#include "VkSwapchain.hpp"
#include "VkContext.hpp"
#include <android/log.h>

#include <cstddef>
#include <string>
#include <vector>

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


void Emit(RenderPassContext const& ctx, std::string const& line)
{
    if (ctx.debug_output != nullptr) {
        ctx.debug_output->push_back(line);
    }
}

uint32_t VertexLayoutIdFromMesh(ave::resource::MeshRuntime const& mesh)
{
    // Convention (see README): vertex_layout_id describes attribute layout.
    // For bring-up we key by stride; extend this when multiple layouts share stride.
    if (mesh.vertex_stride == 7 * sizeof(float)) {
        return 1; // RasterColorVertex (pos3 + color4)
    }
    if (mesh.vertex_stride == sizeof(ave::project::VertexData)) {
        return 2; // project::VertexData
    }
    return 0;
}

DescriptorSetLayoutKey MakeFrameSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eUniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
        // Reserved for shadow map / global textures.
        // DescriptorBinding{
        //     .binding = 1,
        //     .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
        //     .descriptor_count = 1,
        //     .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        // },
    };
    return key;
}

DescriptorSetLayoutKey MakeMaterialSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eUniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 2,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 3,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

PipelineKey MakePipelineKey(RenderPassContext const& ctx,
                            uint32_t pass_id,
                            uint32_t shader_id,
                            ave::resource::MeshRuntime const& mesh)
{
    PipelineKey key{};
    key.pass_id = pass_id;
    key.shader_id = shader_id;
    key.vertex_layout_id = VertexLayoutIdFromMesh(mesh);
    key.render_state_id = 1;
    key.layout_profile = 1; // Preview FrameData path: no descriptor sets yet.
    key.rt_format = 0;      // filled by caller when swapchain is present
    key.depth_format = 0;
    key.stencil_format = 0;
    key.sample_count = 1;
    key.viewport_width = 0;
    key.viewport_height = 0;

    (void)ctx;
    return key;
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

    // Vulkan backend detection
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();

    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    // For shadow pass we only need material (if any) and mesh data.
    // No frame UBO is required.

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.03f;
        clear.color.float32[1] = 0.04f;
        clear.color.float32[2] = 0.06f;
        clear.color.float32[3] = 1.0f;

        if (context.vk->SupportsDynamicRendering()) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            cmd.beginRendering(rendering_info);
        } else {
            vk::RenderPassBeginInfo render_pass_begin{};
            render_pass_begin.renderPass = context.compatibility_render_pass;
            render_pass_begin.framebuffer = context.compatibility_framebuffer;
            render_pass_begin.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            render_pass_begin.clearValueCount = 1;
            render_pass_begin.pClearValues = &clear;
            cmd.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
        }
    }

    for (auto const* renderable : view.renderables) {
        if (!renderable) continue;

        // Resolve material
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing material %s", renderable->material_id.c_str());
            continue;
        }

        // Resolve mesh
        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing mesh %s", renderable->mesh_id.c_str());
            continue;
        }

        // Resolve shader (fallback to material's shader)
        ave::resource::ShaderRuntime const* shader = nullptr;
        if (material != nullptr && material->shader_id != 0) {
            shader = shader_mgr.GetShader(material->shader_id);
        }
        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing shader for material %s", material->name.c_str());
            continue;
        }

        // Create pipeline key. Use pass_id = 1 for shadow (arbitrary distinct value).
        PipelineKey key = MakePipelineKey(context, /*pass_id*/ 1, shader->id, *mesh);
        // Shadow pass does not require any descriptor sets, keep layout_profile = 0.
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: pipeline create failed for %s", renderable->debug_name.c_str());
            continue;
        }

        if (has_vk) {
            auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
            if (!pipeline) continue;

            // Bind pipeline
            context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            // Bind vertex and index buffers
            vk::DeviceSize offset = 0;
            context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
            if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
                context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
                uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
                uint32_t const first_index = renderable->first_index;
                int32_t const vertex_offset = static_cast<int32_t>(renderable->first_vertex);
                context.command_buffer.drawIndexed(index_count, 1, first_index, vertex_offset, 0);
            } else {
                uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
                uint32_t const first_vertex = renderable->first_vertex;
                context.command_buffer.draw(vertex_count, 1, first_vertex, 0);
            }
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        if (context.vk->SupportsDynamicRendering()) {
            cmd.endRendering();
        } else {
            cmd.endRenderPass();
        }
    }
}

PassDataFilter PBRPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ForwardOpaque;
    filter.layer_mask = 0xFFFFFFFFu;
    return filter;
}

glm::mat4 last_view_projection = glm::mat4{1.0f};
void PBRPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: PBRPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    // If we're running on the Vulkan backend, record real draw calls.
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();

    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct FrameUbo {
        glm::mat4 view_projection;
    };
    // if (context.frame && context.frame->view.view_projection != last_view_projection) {
    //     const auto& mat = context.frame->view.view_projection;
    //     last_view_projection = context.frame->view.view_projection;

    //     __android_log_print(ANDROID_LOG_INFO, "ViewProjection", "ViewProjection Matrix:");
    //     __android_log_print(ANDROID_LOG_INFO, "ViewProjection", "[ %f, %f, %f, %f ]", mat[0][0], mat[1][0], mat[2][0], mat[3][0]);
    //     __android_log_print(ANDROID_LOG_INFO, "ViewProjection", "[ %f, %f, %f, %f ]", mat[0][1], mat[1][1], mat[2][1], mat[3][1]);
    //     __android_log_print(ANDROID_LOG_INFO, "ViewProjection", "[ %f, %f, %f, %f ]", mat[0][2], mat[1][2], mat[2][2], mat[3][2]);
    //     __android_log_print(ANDROID_LOG_INFO, "ViewProjection", "[ %f, %f, %f, %f ]", mat[0][3], mat[1][3], mat[2][3], mat[3][3]);
    // } 
    FrameUbo frame_ubo{};
    if (context.frame != nullptr) {
        frame_ubo.view_projection = context.frame->view.view_projection;
    }

    if (has_vk) {
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
            desc_alloc.UpdateUniformBuffer(frame_set_id_, /*binding*/ 0, frame_ubo_.Handle(), 0, sizeof(FrameUbo));
        }
    }

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.03f;
        clear.color.float32[1] = 0.04f;
        clear.color.float32[2] = 0.06f;
        clear.color.float32[3] = 1.0f;

        if (context.vk->SupportsDynamicRendering()) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            cmd.beginRendering(rendering_info);
        } else {
            vk::RenderPassBeginInfo render_pass_begin{};
            render_pass_begin.renderPass = context.compatibility_render_pass;
            render_pass_begin.framebuffer = context.compatibility_framebuffer;
            render_pass_begin.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            render_pass_begin.clearValueCount = 1;
            render_pass_begin.pClearValues = &clear;
            cmd.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
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
        renderable_index++;
        __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "frame_index: %llu", context.frame->frame_index);
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing material %s, using default", renderable->material_id.c_str());
            continue;
        }   
        

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing mesh %s", renderable->mesh_id.c_str());
            continue;
        }
        ave::resource::ShaderRuntime const* shader = nullptr;
        // Fallback to the material's loaded shader if not explicitly specified on the renderable
        if (!shader && material != nullptr && material->shader_id != 0) {
            shader = shader_mgr.GetShader(material->shader_id);
        }

        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing shader");
            continue;
        }

        PipelineKey key = MakePipelineKey(context, /*pass_id*/ 0, shader->id, *mesh);
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: pipeline create failed for %s", renderable->debug_name.c_str());
            continue;
        }

        if (has_vk) {
            auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
            if (!pipeline) {
                continue;
            }

            // Bind pipeline + descriptors + vertex buffer.
            context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            if (frame_set_id_ != 0) {
                vk::DescriptorSet desc_set = desc_alloc.GetHandle(frame_set_id_);
                if (desc_set) {
                    context.command_buffer.bindDescriptorSets(
                        pipeline->BindPoint(),
                        pipeline->Layout(),
                        0, 1, &desc_set, 0, nullptr);
                }
            }

            vk::DeviceSize offset = 0;
            context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
            if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
                context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
                uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
                uint32_t const first_index = renderable->first_index;
                int32_t const vertex_offset = static_cast<int32_t>(renderable->first_vertex);
                context.command_buffer.drawIndexed(index_count, 1, first_index, vertex_offset, 0);
            } else {
                uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
                uint32_t const first_vertex = renderable->first_vertex;
                context.command_buffer.draw(vertex_count, 1, first_vertex, 0);
            }
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        if (context.vk->SupportsDynamicRendering()) {
            cmd.endRendering();
        } else {
            cmd.endRenderPass();
        }
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
    uint32_t const object_count = static_cast<uint32_t>(view.renderables.size());
    
    // Resize culling visibility array to match current renderables count
    if (g_culling_visibility.size() != object_count) {
        g_culling_visibility.assign(object_count, 1u); // Default all visible
    }

    if (object_count == 0) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    uint64_t const frame_index = context.frame ? context.frame->frame_index : 0;
    uint32_t const buf_idx = static_cast<uint32_t>(frame_index % 2);

    if (has_vk) {
        // --- 1. Read Back Previous Results (from 2 frames ago, using this frame's buffer index) ---
        // Since we are double-buffering, and the fence for this frame-in-flight (buf_idx) has just been waited on,
        // the visibility_buffers_[buf_idx] is guaranteed to have finished GPU execution!
        if (frame_index >= 2 && visibility_buffers_[buf_idx].IsInitialized()) {
            uint32_t const* mapped_vis = static_cast<uint32_t const*>(visibility_buffers_[buf_idx].MappedData());
            if (mapped_vis != nullptr) {
                uint32_t const elements_to_copy = std::min(object_count, visibility_buffers_[buf_idx].Size() / (uint32_t)sizeof(uint32_t));
                for (uint32_t i = 0; i < elements_to_copy; ++i) {
                    g_culling_visibility[i] = mapped_vis[i];
                }
                // Any extra new objects are default visible
                for (uint32_t i = elements_to_copy; i < object_count; ++i) {
                    g_culling_visibility[i] = 1u;
                }
            }
        }
    }

    // --- 2. CPU Fallback / Culling Calculations ---
    // If not running on Vulkan, or during the first two frames where we don't have GPU readback yet,
    // we do a quick CPU culling pass to keep the framerate high and correct.
    if (!has_vk || frame_index < 2) {
        if (context.frame) {
            glm::mat4 const view_proj = context.frame->view.view_projection;
            glm::vec4 planes[6];
            planes[0] = glm::vec4(view_proj[3][0] + view_proj[0][0], view_proj[3][1] + view_proj[0][1], view_proj[3][2] + view_proj[0][2], view_proj[3][3] + view_proj[0][3]);
            planes[1] = glm::vec4(view_proj[3][0] - view_proj[0][0], view_proj[3][1] - view_proj[0][1], view_proj[3][2] - view_proj[0][2], view_proj[3][3] - view_proj[0][3]);
            planes[2] = glm::vec4(view_proj[3][0] + view_proj[1][0], view_proj[3][1] + view_proj[1][1], view_proj[3][2] + view_proj[1][2], view_proj[3][3] + view_proj[1][3]);
            planes[3] = glm::vec4(view_proj[3][0] - view_proj[1][0], view_proj[3][1] - view_proj[1][1], view_proj[3][2] - view_proj[1][2], view_proj[3][3] - view_proj[1][3]);
            planes[4] = glm::vec4(view_proj[3][0] + view_proj[2][0], view_proj[3][1] + view_proj[2][1], view_proj[3][2] + view_proj[2][2], view_proj[3][3] + view_proj[2][3]);
            planes[5] = glm::vec4(view_proj[3][0] - view_proj[2][0], view_proj[3][1] - view_proj[2][1], view_proj[3][2] - view_proj[2][2], view_proj[3][3] - view_proj[2][3]);
            for (int i = 0; i < 6; ++i) {
                float length = glm::length(glm::vec3(planes[i]));
                if (length > 0.0f) {
                    planes[i] /= length;
                }
            }

            for (uint32_t i = 0; i < object_count; ++i) {
                auto const* r = view.renderables[i];
                if (!r) continue;
                glm::vec3 center = glm::vec3(r->world[3]);
                float radius = 1.5f; // Bounding radius
                glm::vec3 min_bounds = center - glm::vec3(radius);
                glm::vec3 max_bounds = center + glm::vec3(radius);
                bool visible = true;
                for (int p = 0; p < 6; ++p) {
                    float px = planes[p].x > 0.0f ? max_bounds.x : min_bounds.x;
                    float py = planes[p].y > 0.0f ? max_bounds.y : min_bounds.y;
                    float pz = planes[p].z > 0.0f ? max_bounds.z : min_bounds.z;
                    float dist = planes[p].x * px + planes[p].y * py + planes[p].z * pz + planes[p].w;
                    if (dist < 0.0f) {
                        visible = false;
                        break;
                    }
                }
                g_culling_visibility[i] = visible ? 1u : 0u;
            }
        }
    }

    // --- 3. GPU Dispatch (Record to Command Buffer) ---
    if (has_vk) {
        // Compile/load compute shader from embedded SPIR-V data if not loaded
        auto& shader_mgr = context.resources->GetShaderManager();
        if (g_culling_shader_id == 0) {
            g_culling_shader_id = shader_mgr.LoadComputeShaderFromData("culling", g_culling_shader_spirv, "main");
        }

        // Initialize/resize buffer data
        struct InstanceData {
            glm::vec4 position_radius; // xyz = position, w = radius
        };
        std::vector<InstanceData> cpu_instances(object_count);
        for (uint32_t i = 0; i < object_count; ++i) {
            auto const* r = view.renderables[i];
            cpu_instances[i].position_radius = glm::vec4(r ? glm::vec3(r->world[3]) : glm::vec3(0.0f), 1.5f);
        }

        uint32_t const inst_size = object_count * sizeof(InstanceData);
        uint32_t const vis_size = object_count * sizeof(uint32_t);

        // Ensure buffers are initialized and large enough
        if (!instances_buffers_[buf_idx].IsInitialized() || instances_buffers_[buf_idx].Size() < inst_size) {
            if (instances_buffers_[buf_idx].IsInitialized()) {
                instances_buffers_[buf_idx].Shutdown(*context.vk);
            }
            instances_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                .size = inst_size,
                .usage = vkfw::BufferUsage::Storage,
                .mappable = true
            });
        }
        if (!visibility_buffers_[buf_idx].IsInitialized() || visibility_buffers_[buf_idx].Size() < vis_size) {
            if (visibility_buffers_[buf_idx].IsInitialized()) {
                visibility_buffers_[buf_idx].Shutdown(*context.vk);
            }
            visibility_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                .size = vis_size,
                .usage = vkfw::BufferUsage::Storage,
                .mappable = true
            });
        }

        // Write instances buffer
        instances_buffers_[buf_idx].UpdateData(*context.vk, cpu_instances.data(), inst_size);

        // Write descriptor set
        auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
        auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();

        if (descriptor_set_ids_[buf_idx] == 0) {
            DescriptorSetLayoutKey culling_set_key;
            culling_set_key.bindings = {
                DescriptorBinding{
                    .binding = 0,
                    .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                    .descriptor_count = 1,
                    .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                },
                DescriptorBinding{
                    .binding = 1,
                    .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                    .descriptor_count = 1,
                    .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                }
            };
            uint32_t const set_layout_id = desc_cache.GetOrCreateLayout(culling_set_key);
            descriptor_set_ids_[buf_idx] = desc_alloc.AllocateDescriptorSet(set_layout_id);
        }

        // Update descriptors
        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 0, instances_buffers_[buf_idx].Handle(), 0, inst_size);
        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 1, visibility_buffers_[buf_idx].Handle(), 0, vis_size);

        // Get Compute Pipeline
        PipelineKey pipe_key;
        pipe_key.shader_id = g_culling_shader_id;
        pipe_key.layout_profile = 4;

        uint32_t const pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(pipe_key, context.compatibility_render_pass);
        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (pipeline) {
            vk::CommandBuffer cmd = context.command_buffer;

            // Bind compute pipeline
            cmd.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            // Bind descriptor set
            vk::DescriptorSet desc_set = desc_alloc.GetHandle(descriptor_set_ids_[buf_idx]);
            if (desc_set) {
                cmd.bindDescriptorSets(
                    pipeline->BindPoint(),
                    pipeline->Layout(),
                    0, 1, &desc_set, 0, nullptr
                );
            }

            // Push Constants (frustum planes + instance count)
            struct PushConstants {
                glm::vec4 planes[6];
                uint32_t total_instances;
            } pc{};
            
            if (context.frame) {
                glm::mat4 const view_proj = context.frame->view.view_projection;
                pc.planes[0] = glm::vec4(view_proj[3][0] + view_proj[0][0], view_proj[3][1] + view_proj[0][1], view_proj[3][2] + view_proj[0][2], view_proj[3][3] + view_proj[0][3]);
                pc.planes[1] = glm::vec4(view_proj[3][0] - view_proj[0][0], view_proj[3][1] - view_proj[0][1], view_proj[3][2] - view_proj[0][2], view_proj[3][3] - view_proj[0][3]);
                pc.planes[2] = glm::vec4(view_proj[3][0] + view_proj[1][0], view_proj[3][1] + view_proj[1][1], view_proj[3][2] + view_proj[1][2], view_proj[3][3] + view_proj[1][3]);
                pc.planes[3] = glm::vec4(view_proj[3][0] - view_proj[1][0], view_proj[3][1] - view_proj[1][1], view_proj[3][2] - view_proj[1][2], view_proj[3][3] - view_proj[1][3]);
                pc.planes[4] = glm::vec4(view_proj[3][0] + view_proj[2][0], view_proj[3][1] + view_proj[2][1], view_proj[3][2] + view_proj[2][2], view_proj[3][3] + view_proj[2][3]);
                pc.planes[5] = glm::vec4(view_proj[3][0] - view_proj[2][0], view_proj[3][1] - view_proj[2][1], view_proj[3][2] - view_proj[2][2], view_proj[3][3] - view_proj[2][3]);
                for (int i = 0; i < 6; ++i) {
                    float length = glm::length(glm::vec3(pc.planes[i]));
                    if (length > 0.0f) {
                        pc.planes[i] /= length;
                    }
                }
            }
            pc.total_instances = object_count;

            cmd.pushConstants(pipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants), &pc);

            // Dispatch
            uint32_t const group_count = (object_count + 15) / 16;
            cmd.dispatch(group_count, 1, 1);

            // Insert pipeline barrier: transition Storage Buffer (shader write) to Host read access
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = visibility_buffers_[buf_idx].Handle();
            barrier.offset = 0;
            barrier.size = vis_size;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eHost,
                vk::DependencyFlags{},
                nullptr,
                barrier,
                nullptr
            );
        }
    }

    // Log culling ratio
    uint32_t visible_count = 0;
    for (uint32_t i = 0; i < object_count; ++i) {
        if (g_culling_visibility[i] != 0) visible_count++;
    }
    __android_log_print(ANDROID_LOG_INFO, "CullingSystem", "GPU Culling: %u / %u visible (Ratio: %.2f%%)",
                        visible_count, object_count, (float)visible_count / (float)object_count * 100.0f);
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
    
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        vk::ClearValue clear{};

        if (context.vk->SupportsDynamicRendering()) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            cmd.beginRendering(rendering_info);
        } else {
            vk::RenderPassBeginInfo render_pass_begin{};
            render_pass_begin.renderPass = context.compatibility_render_pass;
            render_pass_begin.framebuffer = context.compatibility_framebuffer;
            render_pass_begin.renderArea = vk::Rect2D{{0, 0}, context.swapchain->Extent()};
            render_pass_begin.clearValueCount = 1;
            render_pass_begin.pClearValues = &clear;
            cmd.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
        }
    }

    for (auto const* item : view.ui_items) {
        if (!item) {
            continue;
        }
        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  ui: %s", item->debug_name.c_str());
    }

    if (has_vk) {
        vk::CommandBuffer cmd = context.command_buffer;
        if (context.vk->SupportsDynamicRendering()) {
            cmd.endRendering();
        } else {
            cmd.endRenderPass();
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
    (void)view;
}

} // namespace ave::render
