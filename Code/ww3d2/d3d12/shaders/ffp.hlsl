// Fixed-function pipeline for the native D3D12 backend.
//
// The engine describes its materials with Direct3D 9 fixed-function state: transforms,
// lights, a material, texture coordinate generation, two texture stage combiners, alpha
// test and vertex fog. The backend packs that state into FFPConstants and this one
// shader evaluates it, following the D3D9 fixed-function rules.

#define MAX_LIGHTS 4
#define MAX_STAGES 2

// VertexFlags.x
#define VF_HAS_NORMAL       0x1
#define VF_HAS_DIFFUSE      0x2
#define VF_HAS_SPECULAR     0x4
// VertexFlags.y
#define LF_LIGHTING         0x1
#define LF_COLORVERTEX      0x2
#define LF_SPECULAR         0x4
#define LF_LOCALVIEWER      0x8
// VertexFlags.w
#define FF_FOG              0x1
#define FF_RANGEFOG         0x2

// D3DMATERIALCOLORSOURCE
#define MCS_MATERIAL 0
#define MCS_COLOR1   1
#define MCS_COLOR2   2

// D3DTSS_TCI_*
#define TCI_PASSTHRU                    0x00000
#define TCI_CAMERASPACENORMAL           0x10000
#define TCI_CAMERASPACEPOSITION         0x20000
#define TCI_CAMERASPACEREFLECTIONVECTOR 0x30000
#define TCI_SPHEREMAP                   0x40000

// D3DTTFF_*
#define TTFF_COUNT_MASK 0x0ff
#define TTFF_PROJECTED  0x100

// D3DTOP_*
#define TOP_DISABLE                    1
#define TOP_SELECTARG1                 2
#define TOP_SELECTARG2                 3
#define TOP_MODULATE                   4
#define TOP_MODULATE2X                 5
#define TOP_MODULATE4X                 6
#define TOP_ADD                        7
#define TOP_ADDSIGNED                  8
#define TOP_ADDSIGNED2X                9
#define TOP_SUBTRACT                   10
#define TOP_ADDSMOOTH                  11
#define TOP_BLENDDIFFUSEALPHA          12
#define TOP_BLENDTEXTUREALPHA          13
#define TOP_BLENDFACTORALPHA           14
#define TOP_BLENDTEXTUREALPHAPM        15
#define TOP_BLENDCURRENTALPHA          16
#define TOP_PREMODULATE                17
#define TOP_MODULATEALPHA_ADDCOLOR     18
#define TOP_MODULATECOLOR_ADDALPHA     19
#define TOP_MODULATEINVALPHA_ADDCOLOR  20
#define TOP_MODULATEINVCOLOR_ADDALPHA  21
#define TOP_BUMPENVMAP                 22
#define TOP_BUMPENVMAPLUMINANCE        23
#define TOP_DOTPRODUCT3                24
#define TOP_MULTIPLYADD                25
#define TOP_LERP                       26

// D3DTA_*
#define TA_SELECTMASK     0x0f
#define TA_DIFFUSE        0
#define TA_CURRENT        1
#define TA_TEXTURE        2
#define TA_TFACTOR        3
#define TA_SPECULAR       4
#define TA_TEMP           5
#define TA_COMPLEMENT     0x10
#define TA_ALPHAREPLICATE 0x20

// D3DCMPFUNC
#define CMP_NEVER        1
#define CMP_LESS         2
#define CMP_EQUAL        3
#define CMP_LESSEQUAL    4
#define CMP_GREATER      5
#define CMP_NOTEQUAL     6
#define CMP_GREATEREQUAL 7
#define CMP_ALWAYS       8

// D3DLIGHTTYPE (0 = disabled)
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
#define LIGHT_DIRECTIONAL 3

struct FFPLight
{
	float4 Diffuse;
	float4 Specular;
	float4 Ambient;
	float4 Position;     // view space
	float4 Direction;    // view space, normalised
	float4 Range;        // x range, y falloff, z cos(theta/2), w cos(phi/2)
	float4 Attenuation;  // x a0, y a1, z a2, w type
};

cbuffer FFPConstants : register(b0)
{
	row_major float4x4 WorldView;
	row_major float4x4 NormalMatrix;     // inverse transpose of WorldView
	row_major float4x4 Projection;
	row_major float4x4 TexTransform[MAX_STAGES];

	float4 MaterialDiffuse;
	float4 MaterialAmbient;
	float4 MaterialSpecular;
	float4 MaterialEmissive;
	float4 MaterialPower;                // x power
	float4 GlobalAmbient;
	float4 FogColor;
	float4 FogParams;                    // x start, y end, z 1/(end-start)
	float4 TextureFactor;
	float4 BumpEnvMatrix;                // 00 01 10 11 (stage 0)
	float4 BumpEnvLuminance;             // x scale, y offset (stage 0)

	uint4  VertexFlags;                  // x vertex, y lighting, z material sources, w fog
	uint4  StageColor[MAX_STAGES];       // x op, y arg1, z arg2, w arg0
	uint4  StageAlpha[MAX_STAGES];       // x op, y arg1, z arg2, w arg0
	uint4  StageCoords[MAX_STAGES];      // x texcoordindex, y transform flags, z result to temp
	uint4  PixelFlags;                   // x alpha func (0 = no test), y alpha ref

	FFPLight Lights[MAX_LIGHTS];
};

// Separate declarations: each stage has its own root descriptor table.
Texture2D    Texture0 : register(t0);
Texture2D    Texture1 : register(t1);
SamplerState Sampler0 : register(s0);
SamplerState Sampler1 : register(s1);

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal   : NORMAL;
	float4 Diffuse  : COLOR0;
	float4 Specular : COLOR1;
	float4 Tex0     : TEXCOORD0;
	float4 Tex1     : TEXCOORD1;
	float4 Tex2     : TEXCOORD2;
	float4 Tex3     : TEXCOORD3;
	float4 Tex4     : TEXCOORD4;
	float4 Tex5     : TEXCOORD5;
	float4 Tex6     : TEXCOORD6;
	float4 Tex7     : TEXCOORD7;
};

struct PSInput
{
	float4 Position : SV_Position;
	float4 Diffuse  : COLOR0;
	float4 Specular : COLOR1;
	float4 Tex0     : TEXCOORD0;
	float4 Tex1     : TEXCOORD1;
	float  Fog      : FOG;
};

float4 Vertex_Texcoord(VSInput input, uint index)
{
	switch (index) {
	case 0: return input.Tex0;
	case 1: return input.Tex1;
	case 2: return input.Tex2;
	case 3: return input.Tex3;
	case 4: return input.Tex4;
	case 5: return input.Tex5;
	case 6: return input.Tex6;
	default: return input.Tex7;
	}
}

float4 Material_Color(uint source, float4 material, float4 color1, float4 color2, bool has1, bool has2)
{
	if (source == MCS_COLOR1 && has1) return color1;
	if (source == MCS_COLOR2 && has2) return color2;
	return material;
}

// Texture coordinates for one stage, before the perspective divide (done per pixel).
float4 Stage_Texcoord(uint stage, VSInput input, float3 view_pos, float3 view_normal)
{
	const uint tci = StageCoords[stage].x;
	const uint flags = StageCoords[stage].y;
	const uint mode = tci & 0xffff0000;

	float4 tc;
	if (mode == TCI_CAMERASPACENORMAL) {
		tc = float4(view_normal, 1.0f);
	} else if (mode == TCI_CAMERASPACEPOSITION) {
		tc = float4(view_pos, 1.0f);
	} else if (mode == TCI_CAMERASPACEREFLECTIONVECTOR) {
		tc = float4(reflect(normalize(view_pos), view_normal), 1.0f);
	} else if (mode == TCI_SPHEREMAP) {
		const float3 r = reflect(normalize(view_pos), view_normal);
		const float m = 2.0f * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0f) * (r.z + 1.0f));
		tc = float4(r.x / m + 0.5f, r.y / m + 0.5f, 0.0f, 1.0f);
	} else {
		// Vertex coordinates arrive as (u, v, 0, 1). Direct3D 9 extends 2D coordinates
		// to (u, v, 1, 0) before a texture transform, which the engine relies on for
		// translations stored in the third row.
		tc = Vertex_Texcoord(input, tci & 0xffff);
		if ((flags & TTFF_COUNT_MASK) != 0) {
			tc = float4(tc.xy, 1.0f, 0.0f);
		}
	}

	const uint count = flags & TTFF_COUNT_MASK;
	if (count != 0) {
		tc = mul(tc, TexTransform[stage]);
		if (flags & TTFF_PROJECTED) {
			// Move the divisor into w; the pixel shader divides.
			const float q = (count == 2) ? tc.y : (count == 3) ? tc.z : tc.w;
			tc = float4(tc.xy, 0.0f, q);
		} else {
			tc.w = 1.0f;
		}
	} else {
		tc.w = 1.0f;
	}
	return tc;
}

PSInput VSMain(VSInput input)
{
	PSInput output;

	const float4 view_pos4 = mul(float4(input.Position, 1.0f), WorldView);
	const float3 view_pos = view_pos4.xyz;
	output.Position = mul(view_pos4, Projection);

	const bool has_normal = (VertexFlags.x & VF_HAS_NORMAL) != 0;
	const bool has_diffuse = (VertexFlags.x & VF_HAS_DIFFUSE) != 0;
	const bool has_specular = (VertexFlags.x & VF_HAS_SPECULAR) != 0;
	float3 view_normal = float3(0.0f, 0.0f, 0.0f);
	if (has_normal) {
		view_normal = mul(input.Normal, (float3x3)NormalMatrix);
		const float len = length(view_normal);
		view_normal = (len > 0.0f) ? view_normal / len : view_normal;
	}

	const float4 vertex_diffuse = has_diffuse ? input.Diffuse : float4(1.0f, 1.0f, 1.0f, 1.0f);
	const float4 vertex_specular = has_specular ? input.Specular : float4(0.0f, 0.0f, 0.0f, 0.0f);

	if (VertexFlags.y & LF_LIGHTING) {
		const bool color_vertex = (VertexFlags.y & LF_COLORVERTEX) != 0;
		const uint sources = VertexFlags.z;
		const bool c1 = color_vertex && has_diffuse;
		const bool c2 = color_vertex && has_specular;
		const float4 ambient = Material_Color((sources >> 0) & 3, MaterialAmbient, input.Diffuse, input.Specular, c1, c2);
		const float4 diffuse = Material_Color((sources >> 2) & 3, MaterialDiffuse, input.Diffuse, input.Specular, c1, c2);
		const float4 specular = Material_Color((sources >> 4) & 3, MaterialSpecular, input.Diffuse, input.Specular, c1, c2);
		const float4 emissive = Material_Color((sources >> 6) & 3, MaterialEmissive, input.Diffuse, input.Specular, c1, c2);

		float3 light_ambient = GlobalAmbient.rgb;
		float3 light_diffuse = float3(0.0f, 0.0f, 0.0f);
		float3 light_specular = float3(0.0f, 0.0f, 0.0f);
		const float3 to_eye = (VertexFlags.y & LF_LOCALVIEWER) ? -normalize(view_pos) : float3(0.0f, 0.0f, -1.0f);

		[unroll]
		for (int i = 0; i < MAX_LIGHTS; ++i) {
			const FFPLight light = Lights[i];
			const uint type = (uint)light.Attenuation.w;
			if (type == 0) {
				continue;
			}

			float3 l;
			float atten = 1.0f;
			if (type == LIGHT_DIRECTIONAL) {
				l = -light.Direction.xyz;
			} else {
				const float3 delta = light.Position.xyz - view_pos;
				const float dist = length(delta);
				if (dist > light.Range.x) {
					continue;
				}
				l = delta / max(dist, 1e-6f);
				atten = 1.0f / max(light.Attenuation.x + light.Attenuation.y * dist + light.Attenuation.z * dist * dist, 1e-6f);
				if (type == LIGHT_SPOT) {
					const float rho = dot(-l, light.Direction.xyz);
					if (rho <= light.Range.w) {
						atten = 0.0f;
					} else if (rho < light.Range.z) {
						atten *= pow(saturate((rho - light.Range.w) / max(light.Range.z - light.Range.w, 1e-6f)), light.Range.y);
					}
				}
			}

			light_ambient += atten * light.Ambient.rgb;
			const float n_dot_l = has_normal ? max(dot(view_normal, l), 0.0f) : 0.0f;
			light_diffuse += atten * n_dot_l * light.Diffuse.rgb;
			if ((VertexFlags.y & LF_SPECULAR) && n_dot_l > 0.0f) {
				const float3 h = normalize(l + to_eye);
				light_specular += atten * pow(max(dot(view_normal, h), 0.0f), MaterialPower.x) * light.Specular.rgb;
			}
		}

		output.Diffuse = float4(saturate(emissive.rgb + ambient.rgb * light_ambient + diffuse.rgb * light_diffuse), saturate(diffuse.a));
		output.Specular = float4(saturate(specular.rgb * light_specular), saturate(specular.a));
	} else {
		output.Diffuse = vertex_diffuse;
		output.Specular = vertex_specular;
	}

	output.Tex0 = Stage_Texcoord(0, input, view_pos, view_normal);
	output.Tex1 = Stage_Texcoord(1, input, view_pos, view_normal);

	output.Fog = 1.0f;
	if (VertexFlags.w & FF_FOG) {
		const float distance = (VertexFlags.w & FF_RANGEFOG) ? length(view_pos) : abs(view_pos.z);
		output.Fog = saturate((FogParams.y - distance) * FogParams.z);
	}
	return output;
}

float4 Stage_Arg(uint arg, float4 current, float4 diffuse, float4 specular, float4 tex, float4 temp)
{
	float4 value;
	switch (arg & TA_SELECTMASK) {
	case TA_DIFFUSE:  value = diffuse; break;
	case TA_TEXTURE:  value = tex; break;
	case TA_TFACTOR:  value = TextureFactor; break;
	case TA_SPECULAR: value = specular; break;
	case TA_TEMP:     value = temp; break;
	default:          value = current; break;
	}
	if (arg & TA_COMPLEMENT) {
		value = 1.0f - value;
	}
	if (arg & TA_ALPHAREPLICATE) {
		value = value.aaaa;
	}
	return value;
}

// One combiner operation. Returns the result for all four channels; the caller picks
// colour or alpha.
float4 Stage_Op(uint op, float4 a0, float4 a1, float4 a2, float4 current, float4 diffuse, float4 tex)
{
	switch (op) {
	case TOP_SELECTARG1:               return a1;
	case TOP_SELECTARG2:               return a2;
	case TOP_MODULATE:                 return a1 * a2;
	case TOP_MODULATE2X:               return a1 * a2 * 2.0f;
	case TOP_MODULATE4X:               return a1 * a2 * 4.0f;
	case TOP_ADD:                      return a1 + a2;
	case TOP_ADDSIGNED:                return a1 + a2 - 0.5f;
	case TOP_ADDSIGNED2X:              return (a1 + a2 - 0.5f) * 2.0f;
	case TOP_SUBTRACT:                 return a1 - a2;
	case TOP_ADDSMOOTH:                return a1 + a2 - a1 * a2;
	case TOP_BLENDDIFFUSEALPHA:        return lerp(a2, a1, diffuse.a);
	case TOP_BLENDTEXTUREALPHA:        return lerp(a2, a1, tex.a);
	case TOP_BLENDFACTORALPHA:         return lerp(a2, a1, TextureFactor.a);
	case TOP_BLENDTEXTUREALPHAPM:      return a1 + a2 * (1.0f - tex.a);
	case TOP_BLENDCURRENTALPHA:        return lerp(a2, a1, current.a);
	case TOP_PREMODULATE:              return a1;
	case TOP_MODULATEALPHA_ADDCOLOR:   return float4(a1.rgb + a1.a * a2.rgb, a1.a);
	case TOP_MODULATECOLOR_ADDALPHA:   return float4(a1.rgb * a2.rgb, a1.a);
	case TOP_MODULATEINVALPHA_ADDCOLOR:return float4((1.0f - a1.a) * a2.rgb + a1.rgb, a1.a);
	case TOP_MODULATEINVCOLOR_ADDALPHA:return float4((1.0f - a1.rgb) * a2.rgb, a1.a);
	case TOP_DOTPRODUCT3:              return saturate(4.0f * dot(a1.rgb - 0.5f, a2.rgb - 0.5f)).xxxx;
	case TOP_MULTIPLYADD:              return a0 + a1 * a2;
	case TOP_LERP:                     return lerp(a2, a1, a0);
	default:                           return current;
	}
}

float4 Sample_Stage(uint stage, float4 tc)
{
	const float2 uv = tc.xy / tc.w;
	return (stage == 0) ? Texture0.Sample(Sampler0, uv) : Texture1.Sample(Sampler1, uv);
}

bool Alpha_Test(uint func, float alpha, uint reference)
{
	const uint a = (uint)round(saturate(alpha) * 255.0f);
	switch (func) {
	case CMP_NEVER:        return false;
	case CMP_LESS:         return a < reference;
	case CMP_EQUAL:        return a == reference;
	case CMP_LESSEQUAL:    return a <= reference;
	case CMP_GREATER:      return a > reference;
	case CMP_NOTEQUAL:     return a != reference;
	case CMP_GREATEREQUAL: return a >= reference;
	default:               return true;
	}
}

float4 PSMain(PSInput input) : SV_Target
{
	float4 tc[MAX_STAGES] = { input.Tex0, input.Tex1 };
	const float4 diffuse = input.Diffuse;
	const float4 specular = input.Specular;
	float4 current = diffuse;
	float4 temp = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float luminance = 1.0f;

	[unroll]
	for (uint stage = 0; stage < MAX_STAGES; ++stage) {
		const uint color_op = StageColor[stage].x;
		if (color_op == TOP_DISABLE) {
			break;
		}

		float4 tex = Sample_Stage(stage, tc[stage]);
		tex.rgb *= luminance;
		luminance = 1.0f;

		if (color_op == TOP_BUMPENVMAP || color_op == TOP_BUMPENVMAPLUMINANCE) {
			// Perturb the next stage's coordinates with this stage's du/dv.
			if (stage + 1 < MAX_STAGES) {
				const float2 duv = tex.xy;
				const float2 offset = float2(dot(duv, BumpEnvMatrix.xz), dot(duv, BumpEnvMatrix.yw));
				tc[stage + 1].xy += offset * tc[stage + 1].w;
			}
			if (color_op == TOP_BUMPENVMAPLUMINANCE) {
				luminance = saturate(tex.z * 2.0f * BumpEnvLuminance.x + BumpEnvLuminance.y);
			}
			continue;
		}

		const float4 c0 = Stage_Arg(StageColor[stage].w, current, diffuse, specular, tex, temp);
		const float4 c1 = Stage_Arg(StageColor[stage].y, current, diffuse, specular, tex, temp);
		const float4 c2 = Stage_Arg(StageColor[stage].z, current, diffuse, specular, tex, temp);
		const float4 color = Stage_Op(color_op, c0, c1, c2, current, diffuse, tex);

		float alpha = current.a;
		const uint alpha_op = StageAlpha[stage].x;
		if (color_op == TOP_DOTPRODUCT3) {
			alpha = color.a;
		} else if (alpha_op != TOP_DISABLE) {
			const float4 a0 = Stage_Arg(StageAlpha[stage].w, current, diffuse, specular, tex, temp);
			const float4 a1 = Stage_Arg(StageAlpha[stage].y, current, diffuse, specular, tex, temp);
			const float4 a2 = Stage_Arg(StageAlpha[stage].z, current, diffuse, specular, tex, temp);
			alpha = Stage_Op(alpha_op, a0, a1, a2, current, diffuse, tex).a;
		}

		const float4 result = saturate(float4(color.rgb, alpha));
		if (StageCoords[stage].z != 0) {
			temp = result;
		} else {
			current = result;
		}
	}

	if (VertexFlags.y & LF_SPECULAR) {
		current.rgb = saturate(current.rgb + specular.rgb);
	}

	if (PixelFlags.x != 0 && !Alpha_Test(PixelFlags.x, current.a, PixelFlags.y)) {
		discard;
	}

	if (VertexFlags.w & FF_FOG) {
		current.rgb = lerp(FogColor.rgb, current.rgb, input.Fog);
	}
	return current;
}
